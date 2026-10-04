# Numerical Methods

## 1. Overview

Two distinct ODE systems are integrated in this project, deliberately kept
separate:

| | Stage | State | Independent variable | Purpose |
|---|---|---|---|---|
| Newtonian | 1 | `(position, velocity)` ∈ ℝ⁶ | time `t` | control-experiment baseline |
| Geodesic | 2–7 | `(u, du/dφ)` ∈ ℝ² | angle `φ` | actual photon propagation |

Both use fixed-form Runge-Kutta 4 as the workhorse integrator; the
geodesic solver additionally uses a curvature-adaptive step size. The
underlying ODE evaluation (`Accel`, `RK4Step`, `AdaptivePhiStep` in
`src/Physics/GeodesicMath.h`) is literally shared, at the source level,
between the CPU tracer and the CUDA kernel — see §5.

## 2. Stage 1: Newtonian integrators

`src/Physics/NewtonianIntegrator.cpp` implements three methods over the
same state `(position, velocity)` under `a = -r̂/r²` (unit-`GM` gravity,
see `docs/physics.md` §2 for the unit derivation):

- **Explicit (forward) Euler** — 1st order, included specifically as a
  *deliberately bad* baseline for the stability comparison, not because
  it's a reasonable production choice.
- **Velocity Verlet** — 2nd order, symplectic (approximately conserves
  the Hamiltonian over long integrations rather than systematically
  gaining or losing energy).
- **Classic RK4** — 4th order, used as the accurate reference.

### Why this comparison matters

Symplectic integrators (Verlet) and high-order non-symplectic ones (RK4)
fail differently. Euler's local truncation error is `O(h²)` per step,
`O(h)` globally, and critically it has no structure that keeps a closed
orbit closed — energy drifts monotonically, so a circular orbit
*visibly spirals* over time. Verlet's local error is worse than RK4's in
absolute terms but it doesn't drift — a circular orbit stays circular
(oscillating in radius, not spiraling). RK4 is both high-order *and*
well-behaved over the timescales this project uses. `tests/test_integrator.cpp`
measures exactly this distinction (see `docs/validation.md`): over one
full circular orbit at the same step count, Euler's radial drift is
roughly four orders of magnitude larger than RK4's.

### Analytic reference: closed-form Kepler orbits

Because Stage 1 is pure `1/r²` gravity, the *exact* trajectory for any
initial condition is a conic section, computed in closed form
(`src/Physics/KeplerOrbit.cpp`) from the specific energy and angular
momentum (semi-latus rectum `p = L²/GM`, eccentricity
`e = √(1 + 2EL²/GM²)`, periapsis direction from the Laplace-Runge-Lenz
vector). This is the ground truth the three integrators are checked
against — any deviation from it is *purely* numerical integration error,
not a modeling error, which is what makes it a clean stability/accuracy
comparison rather than a vague "looks about right" check.

## 3. Stage 2+: the geodesic solver

### 3.1 Step scheme

Classic RK4 is applied to the 2-variable system `(u, v = du/dφ)`:

```
du/dφ = v
dv/dφ = -u + (3/2) r_s u²
```

one fixed-form RK4 step per iteration (`GeodesicMath::RK4Step`), with the
step size `h = Δφ` determined adaptively (§3.2) rather than fixed.

### 3.2 Adaptive step size

The step shrinks where the ODE's right-hand side (`Accel(u, r_s)`) is
large in magnitude — physically, this is exactly the near-photon-sphere
region for a near-critical impact parameter, where the trajectory's
curvature in `φ` changes fastest and a fixed step would lose accuracy
fastest:

```
scale = 1 / (1 + curvatureGain · |Accel(u, r_s)|)
h = clamp(basePhiStep · scale, minPhiStep, maxPhiStep)
```

This is an engineering heuristic (`curvatureGain = 40`, chosen empirically,
not derived from an error bound), not a formal embedded-error-estimate
adaptive scheme like RKF45/Dormand-Prince — documented as such in
`GeodesicSolver.cpp` rather than presented as more rigorous than it is.
It is effective in practice because it ties directly to the actual local
dynamics rather than to a proxy like radius alone.

### 3.3 Configurable parameters, and why each exists

`Physics::GeodesicConfig` (`GeodesicSolver.h`):

| Parameter | Purpose |
|---|---|
| `maxSteps` | Hard bound on integration length. Without it, a ray with `b` extremely close to `b_crit` spirals near the photon sphere indefinitely (physically correct — it's asymptotically approaching an unstable circular orbit — but a real program needs a budget). This is exactly what produces the `UNRESOLVED` classification (§3.4) rather than an infinite loop. |
| `minPhiStep` / `maxPhiStep` | Bound the adaptive step from both directions: `minPhiStep` prevents the curvature term from shrinking the step to numerical zero (which would make `maxSteps` exhaust without making physical progress); `maxPhiStep` prevents the step from growing large enough to blow past a thin feature (the disk plane, the escape/capture threshold) between one evaluation and the next. |
| `captureMultiplier` | Capture is tested at `r ≤ captureMultiplier · r_s` (default `1.001`), not exactly at `r_s` — approaching the horizon, `f(r) → 0` and several formulas in the impact-parameter/redshift machinery divide by `√f(r)`; stopping fractionally outside avoids that division blowing up for a ray that would be captured anyway. |
| `escapeRadius` | How far out counts as "reached the background." Must be `≫ b` for the deflection-angle extraction (`docs/physics.md` §7) to be valid, and `≫` the accretion disk's outer radius so disk photons are never mistaken for escaped ones. |
| `adaptiveStep` | Escape hatch to fixed-step integration, used by a couple of tests that want to reason about step count directly. |

### 3.4 Fate classification and why it matters

Every traced ray ends in exactly one of:

- **ESCAPED** — reached `r ≥ escapeRadius` while still moving outward in
  the relevant sense.
- **CAPTURED** — reached `r ≤ captureMultiplier · r_s`.
- **UNRESOLVED** — exhausted `maxSteps` without resolving either way.

`UNRESOLVED` is not a bug case to be eliminated; it is a *physically
meaningful* category. A ray with `b` infinitesimally close to `b_crit`
takes arbitrarily long (in `φ`) to resolve — in the idealized `b = b_crit`
limit, it never does, asymptotically approaching the photon sphere
forever. Silently reclassifying every `UNRESOLVED` ray as "probably
captured" (or "probably escaped") would misrepresent that boundary; the
project's Stage 3 renderer treats `UNRESOLVED` the same as `CAPTURED` for
color purposes (both fail to reach the background, i.e. both are "the
shadow" — see `docs/physics.md` §6), but the classification itself stays
distinct through the solver and is reported separately in every debug
view and test.

### 3.5 Numerical pitfalls actually encountered, and their fixes

Two real bugs were found and fixed during development (not merely
anticipated in the abstract):

1. **Escape-overshoot misclassified as numerical breakdown.** At large
   `r₀` (e.g. `10⁶`), `du/dφ` stays `O(1/b)` even as `u → 0`, so a single
   fixed-size `φ` step can overshoot straight past `u = 0` into a small
   *negative* `u` before the `u ≤ 1/escapeRadius` check ever fires. The
   original code treated any `u ≤ 0` as a NaN-style numerical failure
   (`UNRESOLVED`); the fix reorders the checks so a finite negative `u` is
   recognized as "escaped" (since `r` passed through `+∞` within that
   step), reserving `UNRESOLVED` for genuine non-finite values. Caught by
   `tests/test_deflection.cpp` initially failing at every impact
   parameter with an *escaped* classification but a false `UNRESOLVED`
   report.
2. **Under-resolved bending region at large impact parameter.** The
   angular width over which deflection actually accumulates near closest
   approach shrinks like `r_s/b`; a fixed step size fine enough for small
   `b` badly under-resolves large `b`. Fixed by scaling the step to the
   impact parameter (`min(baseStep, 0.05·r_s/b)`) in the validation tests
   specifically. This is a caller-side tuning choice, not a change to the
   solver itself — the solver's adaptive-step heuristic (§3.2) is tied to
   curvature, not to impact parameter, so callers integrating far from
   the photon sphere need to pick their own base step size appropriately.

### 3.6 Radial special case

A purely radial photon (`b = 0` to floating-point precision) is handled
separately: the general `u(φ)` formulation is singular there (`1/b²`
diverges, and `φ` itself is undefined for a trajectory with no angular
momentum). Physically this case is trivial — a radial null geodesic moves
at constant `dr/dλ` — so it's stepped directly in `r` rather than through
`φ`. (The GPU kernel does not special-case this; see
`docs/validation.md` for the documented, narrow consequence.)

## 4. Coordinate systems

Three coordinate representations are in active use, converted between
explicitly rather than conflated:

- **Schwarzschild `(r, φ)`**, equatorial-plane-reduced — what the ODE
  is actually expressed in.
- **The photon's own orbital-plane basis** `(r̂, φ̂)` at `φ = 0`
  (`rHat`, `tangentialDir` in `GeodesicSolver.cpp`) — fixed for the
  whole trajectory, used to reconstruct 3D Cartesian positions via
  `pos = r·(cos φ · r̂ + sin φ · φ̂)`.
- **Ordinary 3D Cartesian sim-space** — what cameras, the accretion
  disk's global equatorial plane, and the renderer all use. A ray's
  orbital plane is picked per-ray from `cross(origin, direction)`, so an
  arbitrary camera ray not confined to any particular plane is still
  handled by the same 2D-reduced solver.

## 5. GPU parallelization (Stage 7)

**Design: one CUDA thread per ray/pixel.** Every ray's trace is fully
independent (no shared state, no inter-ray communication), making this an
embarrassingly parallel problem well suited to a flat one-thread-per-pixel
kernel (`cuda/geodesic_kernel.cu`) with a `16×16` thread block.

**Shared source, not duplicated logic.** `src/Physics/GeodesicMath.h`
marks its ODE primitives (`Accel`, `RK4Step`, `AdaptivePhiStep`) with a
`BH_HOST_DEVICE` macro (`__host__ __device__` under `nvcc`, a no-op
otherwise). Both `GeodesicSolver.cpp` (CPU, MSVC-compiled) and
`geodesic_kernel.cu` (GPU, `nvcc`-compiled) call the *same* header for the
actual numerical step — only the surrounding control flow differs
(CUDA device code can't use `std::vector`/`std::function`/exceptions the
way the CPU orchestration does). `Physics::Schwarzschild::LapseFactor` is
marked the same way, since the disk redshift calculation needs it on both
sides.

**Host/device boundary.** `cuda/GeodesicKernel.h` is a plain-C++ header
(no CUDA syntax) declaring the kernel launcher and its POD input/output
structs, so it's includable from ordinary MSVC-compiled code
(`RayTracing/CUDATracer.cpp`) without leaking CUDA syntax across the
build-system boundary. The launcher's *definition* lives in the `.cu`
file, compiled by `nvcc` into its own static library
(`bh_cuda_tracer`), linked into the app only when `-DBH_ENABLE_CUDA=ON`.

**CPU fallback.** `BH_ENABLE_CUDA` is `OFF` by default — Stages 1–6 (and
Stage 7's CPU path) build and run with no CUDA toolkit installed at all.
`RayTracing::CPUTracer` implements the identical per-pixel algorithm on
the CPU, parallelized across image rows with `std::execution::par`. Both
tracers share the exact same function signature
(`CameraFrame, RenderSettings, BackgroundSampler, DiskSampler → pixels`),
so the app UI swaps between them with a single toggle, and a direct
pixel-for-pixel comparison is one button click away (§"Validate: Compare
CPU vs GPU" in the app; measured results in `docs/validation.md`).

**Known GPU-specific limitation.** The purely-radial special case (§3.6)
is not implemented on the GPU kernel; a ray landing in that
(floating-point-measure-zero) configuration falls through to the main
`b`-parametrized path, where near-zero `b` either still integrates
plausibly or fails the `isfinite()` guard and is marked `Unresolved` for
that one pixel. In a full-screen render this affects at most a handful
of pixels directly along the camera's exact forward axis — documented in
`cuda/geodesic_kernel.cu` rather than silently accepted.

## 6. What is *not* attempted

No embedded-error-estimate adaptive integrator (RKF45, Dormand-Prince) is
implemented — the curvature-heuristic step control (§3.2) is simpler and,
for this specific ODE, sufficient, but a formal error-controlled scheme
would be a natural next step if higher precision were needed near
`b = b_crit`. No arbitrary-precision arithmetic is used; all integration
is IEEE double precision, which is why `captureMultiplier` and the
disk-crossing linear interpolation (`docs/physics.md` §9) exist as
deliberate tolerances rather than exact boundary hits.
