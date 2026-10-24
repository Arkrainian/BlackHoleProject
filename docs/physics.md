# Physics

## 1. Research question

How does general relativity alter the propagation of light around a
Schwarzschild black hole compared with Newtonian predictions, and can that
difference be demonstrated by direct numerical integration rather than by
a pre-baked visual effect?

Stage 1 of this project integrates Newtonian test-particle trajectories
under ordinary `1/r²` gravity as a deliberate control experiment. Stage 2
onward integrates the true Schwarzschild null-geodesic equation. The two
are rendered side by side (same starting conditions, same impact
parameters) so the divergence between them — captured photons, stronger
bending, the photon sphere, the Einstein ring — is something the simulator
*produces*, not something it is told to draw.

## 2. Units and conventions

Physical constants: `G = 6.67430e-11 m³ kg⁻¹ s⁻²`, `c = 2.99792458e8 m/s`
(`src/Physics/Constants.h`).

Internally the simulator works in **geometrized units scaled to the current
black hole**: `G = c = 1`, and the length unit is chosen so that
`GM/c² = 1`. In that convention:

- Schwarzschild radius: `r_s = 2` (always, exactly, regardless of mass)
- Photon sphere: `r_ph = 3 = 1.5 r_s`
- Critical impact parameter: `b_crit = 3√3 ≈ 5.196`

A `Constants::SimulationUnits` helper carries the one number needed to
convert a dimensionless simulation distance back to physical units
(`metersPerUnit = GM/c²`) for display — e.g. the UI reports the
Schwarzschild radius in kilometers for a user-chosen mass in solar masses,
while every internal computation stays in the mass-independent sim-unit
convention. This is the standard trick in numerical GR: it reduces "a
10-solar-mass hole" and "Sgr A*" to the *same* dimensionless integration
problem.

## 3. The Schwarzschild metric

For a non-rotating, uncharged mass `M`, in Schwarzschild coordinates
`(t, r, θ, φ)`:

```
ds² = -f(r) dt² + dr²/f(r) + r²(dθ² + sin²θ dφ²),      f(r) = 1 - r_s/r
```

`f(r)` (the metric's lapse function) appears throughout the project —
`Physics::Schwarzschild::LapseFactor` — in the impact-parameter tetrad
relation, the shadow-angle formula, and the accretion-disk redshift
calculation.

Because the metric is spherically symmetric, every geodesic (timelike or
null) lies in a single plane through the origin. This is not an
approximation — it follows from the conservation of angular momentum — and
it is why the solver only ever integrates a **2D reduced problem** (`r, φ`
within that plane) even for a fully general 3D camera ray
(`GeodesicSolver.cpp` picks the plane per-ray from `cross(origin, direction)`
and reconstructs the true 3D path from it).

## 4. Null geodesics and the photon orbit equation

A photon's motion conserves two quantities from the metric's Killing
vectors:

```
E = f(r) dt/dλ            (energy, per unit affine parameter λ)
L = r² dφ/dλ               (angular momentum)
b = L/E                    (impact parameter)
```

Rather than integrating all four coordinates in the affine parameter `λ`
(which requires care at radial turning points, where `dr/dλ → 0`), this
project uses the standard substitution `u = 1/r` and reduces to a single
second-order ODE in `φ`:

```
(du/dφ)² = 1/b² - u²(1 - r_s u)                 (first integral)
d²u/dφ² = -u + (3/2) r_s u²                      (equation actually integrated)
```

This is the textbook form (see Wald, *General Relativity*; Carroll,
*Spacetime and Geometry*, ch. 5) for Schwarzschild null geodesics. It is
preferred here for two concrete reasons:

- **`φ` is always well-behaved as the independent variable.** Since
  `dφ/dλ = L/r²` never changes sign for `L ≠ 0`, `φ` increases monotonically
  along the *entire* trajectory — including through a radial turning point,
  where `dr/dλ = 0` but `dφ/dλ` stays finite. The naive affine-parameter
  form has no such guarantee and needs special handling exactly at turning
  points.
- **It exposes the effective potential directly.** `V(u) = u²(1 - r_s u)`
  and its critical point give the photon sphere and critical impact
  parameter in closed form (§5) — useful both for the physics and as a
  validation target.

The `b → 0` (purely radial) case is singular in this formulation (`1/b²`
diverges) but is also physically trivial: `d²r/dλ² = 0` for `L = 0`, so
`GeodesicSolver.cpp` steps that case directly instead of going through `φ`.

### 4.1 Initial conditions from a general camera ray

A photon is launched from an arbitrary 3D position `origin` with an
arbitrary 3D direction `direction` — not necessarily equatorial, not
necessarily aimed radially. To turn that into `(b, u₀, du/dφ|₀)`, the solver
uses the **static observer's orthonormal tetrad** at `r₀ = |origin|`:

```
e_t̂ = f(r)^(-1/2) ∂_t,   e_r̂ = f(r)^(1/2) ∂_r,   e_φ̂ = (1/r) ∂_φ
```

Decomposing the ray's direction into local radial/tangential components
`(n_r̂, n_φ̂)` (with `n_r̂² + n_φ̂² = 1`) gives the *exact* impact parameter,
valid at any `r₀` outside the horizon — not a flat-space approximation
that only works far away:

```
b = r₀ n_φ̂ / √f(r₀)
```

This same relation, `n_φ̂(r) = b√f(r)/r`, holds at *any* point along the
geodesic (not just the launch point), because `b` is conserved — Stage 6
reuses this fact to recover the photon's local propagation direction at
its accretion-disk crossing point without re-deriving anything.

## 5. Photon sphere and critical impact parameter

The effective potential `V(u) = u² - r_s u³` has a maximum at
`dV/du = 0 → u = 1/(3GM/c²)`, i.e. the **photon sphere**:

```
r_ph = 3GM/c² = 1.5 r_s
```

This is the unstable circular photon orbit — a photon launched exactly
tangentially at `r_ph` stays in (unstable) orbit. It is the natural
validation point for the whole formulation (§ of `docs/validation.md`):
the impact parameter of a tangential ray launched exactly at `r_ph` should
equal the **critical impact parameter**

```
b_crit = 3√3 · GM/c²  =  (3√3/2) r_s  ≈  5.196152...  (sim units)
```

`b_crit` is the exact separatrix: `|b| < b_crit` → captured, `|b| >
b_crit` → escapes (in the idealized limit of a ray launched from `r → ∞`).

## 6. Event horizon vs. black-hole shadow

These are explicitly **not** the same thing, and the project treats them
as such rather than conflating them:

- The **event horizon**, `r = r_s`, is a property of spacetime itself —
  coordinate-invariant, independent of any observer.
- The **shadow** is the dark disk a specific observer, at a specific
  distance, actually sees — its apparent angular radius depends on where
  you're standing.

For a static observer at `r₀`, the shadow's angular radius follows from
the same tetrad relation as §4.1: the shadow boundary is exactly where
`b = b_crit`, so

```
sin(θ_shadow) = b_crit √f(r₀) / r₀
```

This reduces to the familiar `θ_shadow ≈ b_crit/r₀` only as `r₀ → ∞`; at
finite `r₀` it is measurably — and, close in, dramatically — larger than
a naive flat-space estimate of the horizon's own angular size,
`asin(r_s/r₀)`. `tests/test_shadow.cpp` makes this quantitative: at
`r₀ = 10` (sim units), the shadow subtends `27.7°` while the horizon's
naive angular size is `11.5°` — a factor of ~2.4, not a rounding
difference. See `docs/validation.md` for the numbers.

## 7. Gravitational lensing and the deflection angle

For a ray launched from very large `r₀` (`r₀ ≫ r_s`, `b`), the total
turning angle of the trajectory, `Δφ`, relates to the classic deflection
angle by `α = Δφ - π` (a straight line that misses the origin entirely
would have `Δφ = π` exactly; any additional turning *is* the deflection).

In the weak-field limit (`b ≫ r_s`), this matches the standard
post-Newtonian light-bending formula:

```
α ≈ 4GM/(bc²) = 2 r_s / b        (leading order)
```

with the next-order correction (from a Taylor expansion of the full
elliptic-integral solution) contributing an additional

```
α ≈ 2r_s/b + (15π/16)(r_s/b)² + ...
```

`docs/validation.md` compares the numerically-integrated deflection
against both the leading-order formula and this correction across a range
of impact parameters — the residual between "numerical" and "leading-order
analytical" is shown to track the known next-order term, not to be
integration error.

**Stage 3's lensing** is produced by backward ray tracing: for each pixel,
a ray is launched from the camera and integrated through curved spacetime;
wherever it escapes, the background (a procedural starfield, or a single
point source for the Einstein-ring test) is sampled in the ray's escape
direction. No screen-space warp or pre-computed distortion map is
involved — the lensing pattern, including multiple imaging near the
photon sphere and the Einstein ring, is an emergent consequence of tracing
real geodesics.

## 8. The Einstein ring

When a point source, the black hole, and the observer are exactly
collinear, symmetry demands the lensed image form a ring around the
shadow at the angular radius where `b ≈ b_crit`. This project does not
draw that ring: `SamplePointSource` places a small point source exactly
along the camera's own forward axis (beyond the black hole), which keeps
camera → hole → source collinear automatically for *any* orbit angle, and
the ring appears in the rendered image purely because rays near the
critical impact parameter, launched from many different pixels around the
shadow, all get bent enough to reach that one source direction.

## 9. Accretion disk (Stage 6)

A geometrically thin, optically thick disk in the equatorial plane, with
a Shakura & Sunyaev (1973) radial temperature profile:

```
T(r) ∝ (r_in/r)^(3/4) √(1 - √(r_in/r)),      r_in ≤ r ≤ r_out
```

(zero at the inner edge, peaking a little beyond it, falling off as
`r^(-3/4)` further out — the standard alpha-disk shape; see e.g. Frank,
King & Raine, *Accretion Power in Astrophysics*, ch. 5).

**Orbital velocity.** For a circular equatorial timelike geodesic in
Schwarzschild spacetime, the coordinate angular velocity matches the
Newtonian/Keplerian form exactly, `dφ/dt = √(GM/r³)` — a genuine feature
of Schwarzschild spacetime, not an approximation. Projecting into the
local static observer's orthonormal frame (proper azimuthal distance over
proper time) gives the *locally measured* orbital speed:

```
v(r) = √(GM/(r - r_s))          (sim units, GM = 1: v = 1/√(r - r_s))
```

As a sanity check with a well-known closed-form answer: at the innermost
stable circular orbit, `r = ISCO = 3 r_s = 6GM/c²`, this gives
`v = 1/√(6-2) = 0.5` — the local orbital speed at the ISCO is *exactly*
half the speed of light, a standard Schwarzschild fact, confirmed
numerically in `tests/test_disk.cpp`.

**Redshift and beaming.** At a ray's disk-crossing point, the total
frequency ratio (observed at infinity / emitted in the disk's local rest
frame) combines the static gravitational redshift with the special-
relativistic Doppler factor for the disk's motion:

```
g = √f(r) · 1/(γ(1 - v cos ψ))
```

where `ψ` is the angle, in the local static observer's frame, between the
photon's propagation direction and the disk material's velocity direction
— both recovered from quantities the geodesic solver already tracks (the
photon's local direction from the conserved `b`, per §4.1; the disk's
direction from the azimuthal tangent at the 3D crossing point). Feeding
the redshifted temperature `T_obs = g·T_emit` into a `T⁴` (Stefan-
Boltzmann) brightness law reproduces relativistic beaming — the
approaching side of the disk brightens, the receding side dims — without
a separate ad-hoc beaming formula bolted on afterward.

## 10. Kerr (rotating) black holes

`BlackHole::SetSpin`/`Spin()` adds the dimensionless Kerr parameter
`a* = Jc/(GM²) ∈ [-0.998, 0.998]` (the upper clamp is the standard
"Thorne limit" for a realistic accreting hole, and also where the formulas
below stay numerically well-behaved — the inner and outer horizons
coincide exactly at `|a*| = 1`). `a* = 0` is the Schwarzschild hole
described in §1-9 above, unchanged.

**Metric** (Boyer-Lindquist `t, r, θ, φ`, `Physics::Kerr` in
`KerrMetric.h`):

```
ds² = -(1 - 2r/Σ) dt² - (4ar sin²θ/Σ) dt dφ + (Σ/Δ) dr² + Σ dθ² + (A sin²θ/Σ) dφ²
Σ = r² + a²cos²θ,   Δ = r² - 2r + a²,   A = (r²+a²)² - a²Δ sin²θ
```

**Closed-form quantities** (checked against standard textbook/Bardeen et
al. 1972 values in `tests/test_kerr.cpp`, including the a*=0 Schwarzschild
limit and the a*=1 extremal limit):

- Outer/inner horizons: `r± = M ± √(M² - a²)`.
- Equatorial ergosphere: always `r = 2M` (independent of spin); at the
  poles it coincides with the outer horizon.
- Equatorial photon orbit / ISCO, separately for the prograde
  (co-rotating) and retrograde branches (Bardeen, Press & Teukolsky 1972):
  frame dragging pulls the prograde orbit inward (down to `r=M`, `r=M` at
  extremal spin) and pushes the retrograde one outward (`r=4M`, `r=9M` at
  extremal spin).

**Geodesic integration** (`Physics::TracePhotonKerr`,
`KerrGeodesicSolver.cpp`/`KerrGeodesicMath.h`): a general Kerr photon
orbit is *not* planar (unlike Schwarzschild, where spherical symmetry
guarantees it), so there is no single monotonic angle to integrate
against the way §4 uses `φ`. Instead this uses Carter's (1968) separation
of the Hamilton-Jacobi equation — the standard formulation for Kerr null
geodesics (Bardeen, Press & Teukolsky 1972 §II; also used by e.g. Chan,
Psaltis & Özel's "GRay", Dexter & Agol's `geokerr`, Vincent et al.'s
GYOTO):

```
Σ² (dr/dλ)² = R(r) = [E(r²+a²) - aLz]² - Δ[(Lz-aE)² + Q]
Σ² (dθ/dλ)² = Θ(θ) = Q - cos²θ[Lz²/sin²θ - a²E²]
Σ (dφ/dλ) = -(aE - Lz/sin²θ) + a[E(r²+a²)-aLz]/Δ
```

with conserved energy `E`, angular momentum `Lz`, and Carter's constant
`Q`. Each is a decoupled first-order ODE in a single variable, so the only
numerical subtlety beyond Schwarzschild is handling the `±` sign choice at
a turning point (`R(r)=0` or `Θ(θ)=0`): the solver detects an overshoot
past a root (the trial step would make the potential go negative) and
retries from the pre-step state with the sign flipped and the step
halved, rather than letting `sqrt(max(0, ...))` silently clip the
overshoot and lose the reflection.

Initial `(E, Lz, Q)` are derived from the camera's local ZAMO
(zero-angular-momentum observer) tetrad — the physically appropriate local
frame at any `r` outside the horizon, including inside the ergosphere
where a *static* observer (§4.1's Schwarzschild tetrad) doesn't exist.
`E_local = 1` is an arbitrary normalization (as with Schwarzschild's `b`,
only ratios like `Lz/E` matter); at `a=0` this reduces exactly to the
Schwarzschild tetrad relation `b = r₀n_φ̂/√f(r₀)`, checked as a direct
regression in `tests/test_kerr.cpp`.

**Spatial embedding approximation.** The solver treats a Cartesian camera
position's `(r, θ)` as literal spherical coordinates (`x = r sinθ cosφ`,
etc.) rather than the Boyer-Lindquist coordinates' true oblate-spheroidal
embedding into flat 3-space. This only affects how a 3D look direction and
the final escape direction are read off; every conserved quantity and all
of the actual geodesic dynamics depend only on the `(r, θ)` *values*,
which are exact. This is the same simplification most real-time Kerr
renderers make, and it becomes exact in the far field (large `r`) and at
`a=0` regardless of `r`.

**Accretion disk redshift in Kerr mode.** Rather than re-deriving §9's
local-tetrad Doppler decomposition for Kerr's more involved ZAMO geometry,
the disk uses the standard closed-form result for a photon emitted by
matter on a *prograde* circular equatorial Kerr geodesic orbit and
received by a distant observer (Cunningham 1975; Luminet 1979):

```
g = √(1 - 3/r + 2aΩ) / (1 + Ωλ),      Ω = 1/(r^1.5 + a),  λ = Lz/E
```

`Ω` is the disk material's Keplerian angular velocity and `λ` the
photon's own conserved specific angular momentum — reduces to the same
physics as §9's formula at `a=0` via an independent derivation (a
cross-check, not a shared code path). The Kerr disk is assumed prograde
(co-rotating with the spin) unconditionally — physically standard, since
Lense-Thirring/Bardeen-Petterson torquing aligns a real accretion disk
with the hole's spin over time — so unlike the Schwarzschild disk's
`diskClockwise` flag (a pure visualization choice with no physically
preferred direction there), Kerr's rotation sense isn't independently
configurable.

**GPU.** `cuda/geodesic_kernel.cu`'s `TracePhotonKerrDevice` /
`TraceKerrPhotonsKernel` mirror the CPU solver above step for step (same
shared `Physics::Kerr`/`Physics::KerrGeodesicMath` templated functions,
deduced at `T=float` instead of `T=double` — not a reimplementation),
launched from `RayTracing::RenderImageCUDA` whenever
`RenderSettings::spin != 0`; the CPU and GPU paths pick the same branch
from the same `spin` field, so Kerr mode is GPU-accelerated exactly like
Schwarzschild mode when a CUDA device is present, with an automated
CPU-vs-GPU comparison (`tests/test_gpu_render.cpp`'s
`RunKerrGpuRenderTests`) alongside the existing Schwarzschild one.

**Validation:** `docs/validation.md` §9 covers the closed-form
horizon/ISCO/photon-orbit/ergosphere quantities, an a=0 regression
against the Schwarzschild solver, and a weak-field deflection check
against the known spin-orbit (frame-dragging) correction to light
bending, `α_retrograde - α_prograde ≈ 8Ma/b²`.

## 11. Binary black holes (approximate)

`Physics::BinarySystem` / `Physics::TracePhotonBinary`
(`BinarySystem.h`, `BinaryGeodesicSolver.{h,cpp}`) add a second,
orbiting black hole. This is explicitly, unavoidably an **approximate**
model — unlike Schwarzschild and Kerr above, there is no exact
closed-form metric for two orbiting black holes in general relativity
at all, so "approximate" here isn't a simplification of some more exact
method this project chose not to implement; it's the best tractable
option.

**Orbital mechanics (exact, for what it models):** the two masses move
on a circular Newtonian two-body orbit about their common barycenter
(fixed at the origin, the same point every other scenario in this app
orbits/centers on). Body 1 *is* the configured black hole (`GM1=1` in
sim units, same convention as `BlackHole.h`); `massRatio` is body 2's
mass relative to it. Positions, angular velocity, and period all follow
the standard two-body Kepler's-third-law result
(`Ω² = G(M1+M2)/separation³`) in closed form — exact for a circular
orbit, though real orbiting black holes also inspiral and radiate
gravitational waves, neither of which is modeled (the orbit here is a
fixed circle, not a decaying one).

**Photon propagation (the actual approximation):** each mass bends
light via an acceleration of **twice** the Newtonian value,
`2GMᵢ/r²`, directed toward that mass, summed across both masses and
applied to a photon moving at constant coordinate speed. Doubling the
Newtonian deflection is the standard, well-known trick for recovering
GR's correct weak-field light-bending angle (`4GM/(bc²)` — the exact
formula `tests/test_deflection.cpp` already validates for the real
Schwarzschild solver) from a cheap, easily-superposed Newtonian-shaped
force law. `tests/test_binary.cpp` checks this directly: with one
mass's contribution zeroed out, the solver's single-mass deflection
matches the known weak-field formula to within a few percent, improving
monotonically at larger impact parameter — the same character of
agreement (and the same known next-order PPN residual) the real
Schwarzschild weak-field table shows.

**What this does and doesn't capture:**

- **Qualitatively plausible, not quantitatively exact, near either
  horizon.** This is fundamentally a weak-field scheme; it does not
  reproduce the real photon sphere, the exact critical impact
  parameter, or frame dragging near either mass. It will still capture
  a ray that gets close enough (the `1/r²` acceleration still diverges
  near `r=0`), giving a plausible-looking shadow, just not an
  exact one.
- **"Frozen lens":** both masses' positions are read once, at launch,
  and held fixed for that photon's entire flight — the standard
  simplification for light-crossing times much shorter than the
  orbital period, not a continuously-updating field.

**Accretion disks:** each body has its own thin equatorial disk, both
coplanar with the orbital plane (`z=0`) — a simplification, not a claim
that a real binary disk is always exactly aligned with the orbit. A ray
crossing `z=0` within either body's `[innerRadius, outerRadius]` is
attributed to that body (`PhotonTraceResult::diskBodyIndex`, 0 or 1),
which the renderer uses to pick the right `AccretionDisk` instance —
each body has its own real Kelvin scale, since it has its own mass.
Body 2's radii scale from the shared `diskSettings` by its Schwarzschild
radius relative to body 1's, so the same "radius in units of `r_s`" the
UI slider sets applies to both disks even though they're physically
different sizes. The redshift calculation reuses the same
Schwarzschild-shaped local-orbital-speed (`v=1/√(r-rs)`) and
gravitational-redshift (`√(1-rs/r)`) formulas the exact Schwarzschild
solver uses, evaluated locally around whichever body owns the disk —
but unlike that solver (which reconstructs the photon's local direction
from a conserved impact parameter), this one uses the already-tracked
velocity vector directly, since this scheme tracks it explicitly at
every step anyway. `tests/test_binary.cpp` checks both disks are hit
correctly, attributed to the right body, and return a finite positive
redshift.

**GPU.** `cuda/geodesic_kernel.cu`'s `TracePhotonBinaryDevice` /
`TraceBinaryPhotonsKernel` mirror the CPU solver above step for step
(same superposed-acceleration scheme, in `float` instead of `double`),
launched from `RayTracing::RenderImageCUDA` whenever
`RenderSettings::binaryEnabled` is set — both masses' current positions
are computed host-side each frame (the "frozen lens" snapshot) and
passed down rather than recomputed per-thread. Binary mode's CPU/GPU
agreement is even tighter than Kerr's (`docs/validation.md` §11) —
this scheme has no trig and no turning-point retry loop, so there's
less for float-vs-double rounding to disagree about.

Binary mode takes priority over Kerr spin when both are configured (a
rotating member of a binary isn't modeled) — see the Advanced-mode
"Binary Black Holes" panel.

**Stars and planets in binary mode.** `Physics::BinarySystem::
GravityAccelSim`/`IsCaptured` extend the single-hole `StaticObserverGravityAccelSim`
law to sum both bodies' own pull at their own current positions (same
"doubled Newtonian" superposition the photon solver above already uses),
and check capture against both bodies' own horizons rather than a single
`length(position) < rs` at the origin. `Astronomy::StarBody::Update` and
`Astronomy::PlanetBody::Update`/`UpdateGPU` take an optional
`BinarySystem*`; `App.cpp` passes the active one whenever binary mode is
on. Before this, a star or planet spawned in binary mode still only felt
a single phantom mass pulling it toward the (now-empty) barycenter,
regardless of where the two actual, visually rendered bodies were — a
real bug, not a documented approximation, fixed in Session 15 (see
`HANDOFF.md`). `PlanetBody::UpdateGPU` does not attempt a binary-aware
GPU kernel — it returns `false` (falls back to the CPU path) whenever a
binary system is passed in, so binary-mode planets always run on CPU
rather than silently running the wrong (single-hole) physics fast.

**Flamm's paraboloid (conceptual, not exclusive to binary mode).**
`Rendering::Renderer::DrawFlammParaboloid` draws the debug 3D view's
classic embedding-diagram "funnel": a wireframe surface of revolution
around a vertical axis through each active mass (the single configured
black hole normally, or both binary bodies, each with its own funnel,
when binary mode is active), with height `y = -2·√(rs·(ρ-rs))` at
planar distance `ρ` from that axis, for `ρ ≥ rs` — the standard,
textbook-exact 2D embedding of the Schwarzschild equatorial spatial
geometry (see e.g. Misner, Thorne & Wheeler), flaring from a vertical-
tangent throat right at the horizon (`ρ=rs`) out to nearly flat far
away. Rings are sampled with a quadratic ease-in on `ρ` so that throat
is resolved smoothly rather than as a sharp cone. **This is an
analogy, not a literal picture of spacetime:** a 2D surface embedded in
3D space standing in for 4D spacetime curvature is the classic textbook
embedding-diagram visualization used throughout popular and technical
treatments of GR alike — genuinely exact as far as it goes, but it is
not a rendering of spacetime itself, and the UI's own tooltip says so
explicitly. The surface isn't evaluated for `ρ < rs` at all (the real
embedding doesn't extend inside the horizon), so the funnel's rim
starts exactly at the throat rather than implying anything about the
interior.

## 12. Freely-placed extra black holes (N-body lensing)

**Extra black holes (`Physics::ExtraBlackHole`, `Physics::TracePhotonMultiBody`).**
Generalizes Binary mode's exactly-two-body scheme (§11) to however many
bodies are actually in the scene — the primary black hole, binary mode's
second body if also enabled, and any number of user-placed
`ExtraBlackHole`s (capped at `kMaxLensingBodies = 6` total, so every
fixed-size array involved stays simple stack storage). Unlike binary
mode's second body, an `ExtraBlackHole` isn't constrained to a circular
orbit — it's placed by flying the free-fly camera to a spot and spawning
it there (`App.cpp`'s "Extra Black Holes" panel), and stays exactly where
it's put (its `velocity` field defaults to zero; nothing pulls on it —
same "test-particle physics, no mutual gravity" rule every other spawned
object in this sim already follows).

The lensing itself uses the **exact same** doubled-Newtonian superposition
approximation §11 describes (each body's light-bending modeled as
`2GM/r²` toward it, summed across every active body) — `Physics::
TracePhotonMultiBody` (`src/Physics/MultiBodyGeodesicSolver.cpp`) is a
structurally independent reimplementation of `TracePhotonBinary`, not a
refactor of it (same "no shared integration code between solvers, only
the shared `PhotonTraceResult` return type" pattern Schwarzschild/Kerr/
Binary already use), generalized from two named body-1/body-2 locals to a
loop over a small array — confirmed to reduce to `TracePhotonBinary`'s own
result, for the same ray and equivalent bodies, by `tests/test_multibody.cpp`.
Every caveat §11 lists (frozen lens, no exact strong-field physics, no
gravitational radiation) applies here verbatim, now for however many
bodies are active rather than just two.

Only activates once **3 or more** bodies are in play — with 1 or 2, the
existing Schwarzschild/Kerr/Binary solvers run completely unchanged (see
`RayTracing::CPUTracer.cpp`'s dispatch), so turning this feature on never
alters single-hole, Kerr, or ordinary two-body binary behavior.

**GPU: not implemented.** `cuda/geodesic_kernel.cu` has no multi-body
kernel. Rather than silently ignoring extra black holes on a GPU-rendered
frame, `App.cpp`'s `PerformRender` falls back to the CPU tracer for any
frame where `extraBlackHoles` is non-empty — the same "fall back instead
of running the wrong physics fast" rule `PlanetBody::UpdateGPU` already
established for binary mode. A frame with extra black holes placed will
therefore render more slowly than usual; this is expected, not a bug.

**Stars, planets, and extra black holes.** `Astronomy::StarBody::Update`
and `PlanetBody::Update`/`UpdateGPU` each gained a parallel
`extraBlackHoles` parameter (alongside the existing `binary` one from
Session 15's binary-mode fix) — both route through the new
`Physics::CombinedGravityAccelSim`/`IsCapturedByAny` helpers
(`src/Physics/MultiBlackHoleSystem.h`), which sum gravity/capture across
the primary (binary-aware if applicable) plus every extra black hole. As
with binary mode, `PlanetBody::UpdateGPU` falls back to the CPU `Update()`
path whenever any extra black holes are placed.

## 13. Stars and planets: tidal disruption

**Stars (`Astronomy::StarBody`).** A star is modeled as a small cluster of
independent test particles (default 28), seeded uniformly inside a sphere
and given the star's bulk velocity with no internal dispersion. Every
particle free-falls under the exact same point-mass law everything else in
this sim uses, `Physics::StaticObserverGravityAccelSim` (Newtonian far away,
the exact GR static-observer acceleration `a(r) = (GM/r²)/√(1-rs/r)` near
the horizon — see `BlackHole.h`). "Spaghettification" is therefore an
emergent consequence of near-side particles measurably outpulling far-side
particles, not a scripted stretch. **What this deliberately omits:** the
star's own self-gravity and any material cohesion at all — every star here
is already past the point self-gravity would have lost; there is no
intact-until-disrupted phase, and no Roche-limit logic of any kind. This was
true from the feature's introduction and is recorded here for the first
time (previously only in code comments and `HANDOFF.md`).

**Planets (`Astronomy::PlanetBody`, Milestone 1).** A planet extends the
same free-falling-test-particle idea with an explicit cohesion force: each
particle also feels a spring force toward ~6 nearest-neighbor particles,
forming a lattice built once at construction. Deformation is a real
consequence of two competing forces — differential gravity pulling the
lattice apart, springs pulling it back together — rather than an unbound
swarm. A spring breaks permanently once its strain `|Δlength/restLength|`
exceeds the chosen material's `materialStrength` (`Physics::PlanetMaterial`);
connected components of the remaining lattice are tracked as fragments
(`Astronomy::PlanetFragment`), with mass/velocity/center-of-mass derived
directly from their member particles — no momentum is fabricated when a
spring breaks (verified by `tests/test_planet.cpp`).

Two diagnostic formulas (`Physics/TidalForceModel.h`) are displayed
alongside the simulation for comparison, but never gate or trigger
destruction themselves — what happens is always whatever the spring network
actually does:

- **Newtonian tidal acceleration**, leading-order in `R_p/r`:
  `Δa_tidal ≈ 2·G·M_bh·R_p/r³`. A weak-field approximation, explicitly not a
  relativistic tidal-tensor calculation.
- **Roche limit** (idealized fluid body): `r_Roche ≈ 2.44·R_p·(M_bh/M_p)^(1/3)`.
  An informative reference radius, not a destruction switch — real
  disruption in this sim depends on material strength and the actual spring
  stresses encountered along the trajectory, which can disagree with this
  estimate in either direction.

**GPU acceleration (`cuda/planet_kernel.cu`, `PlanetBody::UpdateGPU`).**
Unlike the ray tracer's kernels, this is not a per-pixel, launch-once-per-
frame-over-10⁵-10⁶-independent-rays workload — it's a small (tens to low
hundreds of elements by default), *stateful* particle/spring system that
must round-trip to the CPU every frame regardless (for rendering and the
union-find fragment rebuild, both of which stay CPU-side). At the default
particle counts, a GPU kernel launch here is **not** expected to be faster
than the equivalent CPU loop — both are O(particles + springs) per frame,
nothing quadratic, and GPU launch/copy overhead dominates at this scale
(measured, not assumed — see `HANDOFF.md`). The actual benefit is letting
particle counts run far higher than CPU-real-time while the physics
(gravity, spring forces, semi-implicit Euler integration, fracture check)
stays otherwise identical, just in `float` instead of `double` — the same
precision tradeoff already accepted for the Kerr/binary ray-tracing
kernels. `PlanetBody::UpdateGPU` automatically falls back to the CPU
`Update()` when CUDA isn't compiled in, no device is present, or the GPU
call itself fails — same automatic-fallback-only philosophy as the ray
tracer (no "force CPU"/"force GPU" toggle). `BuildSpringLattice` (one-time,
at construction) and `RebuildFragments` (union-find, sequential-shaped)
deliberately stay CPU-only in both paths.

**A correctness fix, worth recording explicitly.** `ApplySpringForces`
originally divided each spring's force by the particle's REAL kilogram
mass (`PlanetParticle::mass`, ~10²²–10²⁴ kg for an Earth-scale planet) to
turn it into an acceleration. `kBaseSpringStiffness` is a freely-tuned
sim-unit constant, not calibrated to real N/m, and
`Physics::StaticObserverGravityAccelSim` (the gravity every particle also
feels) is a pure sim-unit acceleration with no mass term at all
(test-particle convention) — dividing a sim-unit force by a real kilogram
mass made spring accelerations roughly 15–20 orders of magnitude smaller
than gravity, i.e. springs did essentially nothing. Deformation/fracture
was consequently driven almost entirely by unopposed differential gravity
(StarBody's own mechanism), not genuine spring resistance, despite this
doc's and the code's original claim that deformation was "a direct
consequence of stress in this lattice." Fixed by dividing by each
particle's MASS FRACTION of the planet (`mass / totalMassKg`, typically
~1/particleCount) instead — see `ApplySpringForces`'s own comment.
`kBaseSpringStiffness` and every preset/default spawn distance tuned
against the old (non-functional) spring force were re-tuned afterward
(scripted probes, not guesses) against the corrected physics, including
a drop from `400.0` to `0.025` — most of that drop is needed for explicit
semi-implicit-Euler numerical stability at the corrected force scale
(confirmed via probe: a distant, otherwise-stable planet would
spuriously shatter at higher values), not because the spring is meant to
be weak.

**Self-gravity (`PlanetBody::ApplySelfGravity`).** Particles/fragments
optionally attract each other directly — this is what lets separated
fragments drift back together after a fracture, which the spring lattice
alone cannot do once broken (spec 28.10). The physically-exact formula for
this sim's `GM_bh=1` unit convention works out to roughly
`(other particle's mass in kg / black hole's mass in kg) / r²` — at any
black hole mass this app's slider allows, that ratio against an
Earth-scale planet is far too small to ever visibly matter, and the ratio
itself spans many orders of magnitude across that slider range, so no
fixed multiplier on the real formula could land in a visible range
everywhere. Self-gravity therefore uses a standalone, tuned sim-unit force
constant (`kSelfGravityCoefficient`, same spirit as the spring system's
own `kBaseSpringStiffness`) scaled by `PlanetBodySettings::
selfGravityStrength` — a deliberately exaggerated, clearly-labeled effect,
not the real physics. It is also skipped entirely between particles closer
together than `2.5×initialRadius` (`kSelfGravityMinDistanceFactor`):
without that cutoff, self-gravity's `1/r²` law directly fights the spring
lattice at its own (short) rest lengths, and at the stiffness the springs
had to be tuned down to for numerical stability, that fight destabilizes
even an intact, otherwise-stable planet almost immediately (confirmed via
probe). Only beyond that scale — e.g. two genuinely separated fragments —
does it apply, which is exactly the reaccumulation effect it's for.

**Tidal disruption flare (`Astronomy::TidalDisruptionFlare`, debris → disk
coupling, spec 28.12/28.13).** Captured planetary mass temporarily boosts
`AccretionDiskSettings::eddingtonFraction` above the user's own slider
value, decaying back to baseline with a fixed exponential time constant.
This is an explicitly simplified stand-in for the real fallback-accretion
light curve a tidal disruption event actually produces (classically a
`t^-5/3` decline, Rees 1988) — not an attempt to reproduce that curve. The
boost is applied to a per-frame copy of the user's disk settings, never
mutating the slider-backed value itself, so it cleanly disappears as it
decays. Single/Kerr-hole scope only — not coupled in binary mode.

**What this milestone still omits** (see `HANDOFF.md` for the
continuation plan): differentiated internal layers behaving differently
(`PlanetComposition::Differentiated` is one averaged material, not a
core/mantle/crust structure); batching multiple simultaneously-spawned
planets into one combined GPU kernel launch (today it's one launch per
planet per frame, and self-gravity is the first O(N²) GPU kernel this
project has — still trivial at the current particle ceilings but the
thing to revisit first if those ceilings ever rise); and
Kerr/binary-hole-aware capture (both stars and planets check only the
single-hole Schwarzschild radius).

## 14. Mutual gravity between spawned stars/planets

Section 13 above establishes that stars and planets are test particles
under the black hole's field with no gravity of their own, beyond a
planet's own internal self-gravity (§13's own "Self-gravity" entry, which
is explicitly scoped to one planet's separated fragments pulling back
together — never one spawned object pulling on another). This section adds
exactly that missing case: spawned stars and planets now also pull on
*each other*, at the object level — each one reduced to a single point
mass at its own center of mass (`Physics::GravitySource`,
`Astronomy::StarBody::CenterOfMass`/`CurrentMassKg`,
`Astronomy::PlanetBody::CenterOfMassSim`/`CurrentMassKg`) — so two spawned
stars can fall into a binary-star-like mutual orbit. **Off by default**:
existing scenes behave identically unless the "Mutual Gravity" panel is
explicitly enabled.

Black holes are explicitly excluded and unaffected by this entirely: the
primary, the binary system's second body, and every extra black hole stay
exactly as fixed/scripted gravity sources as before — nothing new here
pulls on them, and they are not among the `GravitySource` entries spawned
objects pull on.

Same scale problem as self-gravity, same resolution. The real mutual pull
between any two spawned stars/planets, at any black hole mass this app's
slider allows, is many orders of magnitude too small to ever visibly
matter — and like self-gravity, the real ratio's scale depends on the
configured black hole mass, so no fixed multiplier on the real formula
could land in a visible range everywhere. `Physics::MutualGravityAccelSim`
(`src/Physics/GravitySource.h`) therefore uses a standalone, tuned sim-unit
force law — not the real `(other object's mass / black hole's mass) / r²`
formula — scaled by the UI's "Mutual gravity strength" slider, with a
reference mass of **one solar mass**, not Earth mass: stars (1 solar mass
by default) are the primary use case here, and the first tuning attempt,
which referenced Earth mass the way `PlanetBody`'s self-gravity does,
produced a `massRatio` for a solar-mass star of roughly 3.3×10⁵, which blew
the per-step velocity kick out to many times the objects' own separation
at ordinary spawn distances/timesteps — every coefficient/min-distance
combination tried against that reference flung the pair apart to huge
separations within a few hundred steps (scripted probe, see `HANDOFF.md`).
Referencing solar mass instead keeps `massRatio` near 1 for the common
star-star case (and correspondingly tiny — "even off is a physically
honest choice" territory — for Earth-mass planets pulling on each other).

Tuned via scripted probe (same methodology as self-gravity/the spring-force
fix): `kMutualGravityCoefficient = 1.5`, `kMutualGravityMinDistance = 3.0`.
Two 1-solar-mass stars spawned 20 sim units apart with a modest tangential
kick spiral together over several tens of sim-time units, swing past each
other at a real closest-approach distance well under the 3.0 softening
floor without the integrator diverging, and spread back out — a visible,
bounded "close encounter," not a runaway ejection or a stable closed orbit
(semi-implicit Euler against a softened, not true, inverse-square law
doesn't produce one). A pair left ~500 units apart stays essentially
unaffected, confirming unrelated/distant objects aren't destabilized by
turning this on. As with self-gravity's minimum-distance cutoff, the
softening floor only clamps the *force*, not the real distance — two
objects' centers can still pass closer than it; it exists purely to keep
the explicit integrator from blowing up as `1/r²` diverges, and models no
collision or merger.

No capture check is added for mutual sources (`Physics::GravitySource` has
no Schwarzschild radius) — two stars/planets passing close to each other
isn't an event horizon, and collision/merger response is out of scope.
`PlanetBody::UpdateGPU` falls back to the CPU path whenever mutual gravity
sources are supplied, non-empty — the GPU kernel only knows the
single-hole law, same precedent binary mode and extra black holes already
established.

## 15. Jet piloting mode (arcade flight model around the black hole)

A third camera mode (alongside Orbit and FreeFly, `Rendering::Camera::Mode::Jet`)
puts the user in a flyable ship (a GLB model, `Jet::JetRenderer`) with a
hybrid "semi-realistic force sim + arcade rate-control" flight model
(`Jet::FlightPhysics`, ported and adapted from an existing arcade flight
project) — real thrust/drag forces integrated into velocity, but rotation
is direct rate-control (input maps straight to angular velocity), not
torque/moment of inertia.

**Gravity is the one piece that's real, not arcade**: the ship falls
toward the configured black hole under the *exact* general-relativistic
"static observer" proper acceleration,

```
a(r) = (GM/r²) / sqrt(1 - r_s/r)
```

(the acceleration a hovering observer at fixed Schwarzschild radial
coordinate `r` must apply to stay there — see e.g. MTW *Gravitation* or
Hartle's *Gravity* for the standard derivation), not plain Newtonian
`1/r²` or a constant "down." It reduces to ordinary Newtonian gravity far
away and *diverges* approaching `r_s` — no amount of thrust can hold
station arbitrarily close to the horizon, which is the physically correct
behavior, not a scripted "you die near the hole" check.
`Physics::StaticObserverGravityAccelSim` (`src/Physics/BlackHole.h`) is
this exact formula, shared verbatim with `Astronomy::StarBody`'s tidal
particles (§13) — the same law pulls on anything free-falling in this
scene, not two independent reimplementations.

**The relativistic speed cap, and the real bug it produced at the app's
own default mass.** A hard ceiling, expressed as a fraction of light speed
(`maxSpeedFractionOfC`, default 0.2), is converted every frame into
however many sim-units/s that implies *for whichever black hole mass is
currently loaded* (`metersPerSimUnit = GM/c²` in meters,
`Physics/Constants.h`) and clamps velocity to it — without this, the
exact same numeric thrust/drag equilibrium that feels right for a
stellar-mass hole (where one sim unit is kilometers) becomes "cross
several light-years in a few seconds" once the hole is supermassive
(TON 618: one sim unit ≈ 0.01 light-years). Worked through with actual
numbers: at TON 618's mass, `GM/c² ≈ 9.75×10¹³ m`, so `0.2c` converts to
roughly `0.2 × 2.998×10⁸ / 9.75×10¹³ ≈ 6.1×10⁻⁷` sim-units/s — the jet
reaches that ceiling from its own thrust in microseconds and then sits
there, *effectively frozen on any human timescale*, not merely "slow."
This is the real physical consequence of the real cap at that real mass,
not a bug in the formula — but it made flight unplayable near the app's
own default black hole, so a floor was added:
`Jet::MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC` (150 sim-units/s,
`FlightPhysics.h`), applied as `max(relativisticCap, floor)`. At any
stellar/moderate-mass hole the relativistic cap is already far above 150,
so this changes nothing there; it only ever binds at supermassive scale,
where it is an **explicit, surfaced playability concession** — the app
shows an amber warning with the real implied c-multiple whenever the
floor is actually overriding the physics, rather than silently letting
the on-screen "0.2c" label misrepresent what's actually happening.

**Reverse thrust.** `thrust` is allowed to go negative, down to
`-MAX_THRUST × REVERSE_THRUST_FRACTION` (0.3 — capped well below forward
thrust, an arcade choice: enough to back out of a bad approach or hold
station, not to out-accelerate forward flight). `F_thrust = forward ×
thrust × thrustMultiplier` already pushes the ship backward for free once
`thrust` can go negative, but the existing "aerodynamic turn correction"
(an arcade feature that snaps velocity to align with the nose after a
turn) had to be fixed to target `forward × (airSpeed × sign(thrust))`
instead of unconditionally `+forward × airSpeed` — otherwise it fought
every reverse-thrust input by continuously re-aligning velocity to
*positive* forward regardless of which way the ship was actually being
pushed.

Scope, deliberately limited: no lift model (an arcade "velocity aligns
with the nose" correction stands in for it instead); air density is a
flat sea-level-equivalent constant everywhere (there is no atmosphere in
orbit around a black hole — this exists only to keep the ported arcade
thrust/drag feel unchanged, not as a physical claim); G-force is a flavor
readout (`CalculateGForce`), not a damage/blackout model.

## 16. Universe / Star Map: real catalog travel

A real, bounded (~35-entry) catalog of actual stars, black holes, and
galaxies (`src/Universe/StarCatalog.h`) — not a procedural galaxy
generator — lets the scene be re-pointed at a different real astronomical
object instead of only ever showing one hardcoded default. Figures are
standard published values (Hipparcos-era distances for nearby stars,
widely-cited literature masses for the black holes): real enough to be
meaningful, not research-grade astrometry.

**Coordinate conversion.** Each entry's right ascension (hours),
declination (degrees), and distance (light-years) convert to a shared
heliocentric equatorial Cartesian frame via the ordinary spherical-to-
Cartesian transform,

```
x = d · cos(δ) · cos(α),   y = d · cos(δ) · sin(α),   z = d · sin(δ)
```

(`CatalogObject::CartesianLy`, where `α` is right ascension in radians and
`δ` is declination in radians) — used only so the *real distance between
any two catalog entries* (not just from the Sun) can be computed by plain
vector subtraction, never for anything inside the ray tracer itself
(which has no concept of light-years at all).

**Travel is an explicit, labeled narrative jump — never silently claimed
to be real flight.** This app's camera/physics have no mechanism for
continuously crossing light-year distances (sim units are relative to
whichever black hole is configured and have no meaning at interstellar
scale, and §15's own relativistic speed cap makes clear why "just fly
there" isn't physically available even in principle at these distances
without literal years passing). "Travel" therefore re-points the existing
scene at the target object directly, and the real light-travel-time the
jump represents is always printed next to the control that performs it —
the same honesty standard §15's speed-cap warning holds itself to.
Traveling to a cataloged **black hole** calls the exact same
`BlackHole::SetMassSolarMasses`/`SetSpin` the mass/spin sliders already
call (TON 618, this app's original hardcoded default, is now simply the
catalog entry those sliders start on). Traveling to a **star** switches to
a separate, deliberately simple **Star System View**: this app's ray
tracer only ever models black holes (null geodesics through a
Schwarzschild/Kerr metric) — there is no equivalent GR renderer for an
ordinary star, so rather than silently mislabeling a black-hole render as
"visiting a star," the star is drawn as a plain, unlit, procedurally-
textured sphere (§18 below) on its own small, fixed, arbitrary visual
scale, explicitly independent of whichever black hole's `GM/c²` scale
happens to be loaded. **Galaxy entries are not travel targets** — too far
and too extended for this app's single-point-source renderer to
meaningfully visit; they exist for real-scale distance context only.

A small set of real confirmed/candidate exoplanets
(`src/Universe/ExoplanetCatalog.h`) orbit a handful of catalog stars in
Star System View — see §17.

## 17. Exoplanets: real Kepler's-third-law orbits

Most catalog stars have **no** exoplanet drawn around them in Star System
View, deliberately: a star with no real, published detection gets nothing
invented for it. The handful that do (Proxima Centauri b, Ross 128 b,
Barnard's Star b — flagged as a genuinely disputed detection, not a
blanket hedge — Epsilon Eridani b, Pollux b, and two Tau Ceti candidates)
have real published semi-major axes and masses.

Rather than hardcode each planet's own published orbital period as a
second, independently-sourced number, `Universe::OrbitalPeriodDays`
derives it from the catalog's own star mass via the ordinary Kepler's
third law,

```
P[years] = sqrt( a[AU]³ / M[M☉] )
```

(ignoring the planet's own mass relative to its host star, an excellent
approximation at these mass ratios) — which forces the catalog's star
masses and planets' semi-major axes to stay *mutually consistent* with
real physics, and is directly checked in `tests/test_universe.cpp`
against each planet's own real published period (within 10%, since the
catalog's star masses are themselves rounded published figures, not each
discovery paper's own exact value): Proxima Centauri b computes to 11.26
days against a real ~11.19; Ross 128 b to 9.79 against ~9.9; Epsilon
Eridani b to 2618 against ~2691; Tau Ceti e to 163.2 against ~162.9; Tau
Ceti g to 20.06 against ~20.0 — genuinely passing, not tuned to pass.

In Star System View, each planet's *relative* angular speed is real (a
closer-in planet genuinely orbits faster than a farther one, driven
directly by the formula above) — what is explicitly not real: the
absolute timescale (compressed so the outermost listed planet in a system
completes an orbit in 24 real seconds, not days/years — otherwise nothing
would visibly move in a sitting) and the orbital radii (spaced by relative
semi-major axis only, scaled to fit near the star's own already-not-to-
scale render size).

## 18. Procedural star surface textures

Star System View textures each star's sphere (and, in multi-star systems,
§19, every member) with a seeded "granulation" pattern — soft, randomly
placed, overlapping blobs (`Astronomy::GenerateStarTexture`,
`src/Astronomy/StarTexture.h`), wrapped at the U seam so it tiles
seamlessly around a sphere's longitude — tinted by the star's real
effective temperature via `Astronomy::ApproximateBlackbodyHue` (the same
perceptual blackbody-color approximation §9's accretion-disk coloring
already uses, factored out into `src/Astronomy/Blackbody.h` so both reuse
one implementation rather than drifting into two).

This is **deliberately not a photograph**, and deliberately not a
downloaded one either. Genuine resolved surface imagery exists for the
Sun (space telescopes image its granulation directly) but for essentially
no other star — even the best current ground-based interferometry
resolves only a handful of nearby giants into a few blurry pixels, not a
texture. Using a real Sun photograph for exactly one of ~35 catalog stars
and a flat color for the rest would be a *more* misleading inconsistency
than using neither, so every star gets the same honestly-labeled
procedural treatment: real color, invented surface detail.

## 19. Multi-star systems: real binary/triple, procedural N-body

The Star Map also offers 2-body, 3-body, and N-body (≥4) multi-star
systems, driven by a **genuine per-frame Newtonian N-body integration**
rather than a scripted animation — the same tuned mutual-gravity law §14
introduced for spawned stars/planets pulling on each other
(`Physics::MutualGravityAccelSim`), reused as-is (not retuned) and applied
to a system's own members pulling on *each other* instead.

- **2-body (real): Alpha Centauri AB.** The nearest star system's real
  inner pair — real masses, average real separation ~23.5 AU, real
  orbital period ~79.9 years (highly eccentric: 11.2–35.6 AU). Two
  mutually gravitating bodies *do* have a closed-form solution (the
  ordinary Kepler two-body problem) — this still integrates step by step
  like every system here, it just settles into an ordinary bound orbit.
- **3-body (real): the same system, plus its real third member, Proxima
  Centauri.** Bound but extraordinarily distant (current estimates:
  roughly 13,000 AU from the A–B pair, orbital period on the order of
  500,000+ years, with real substantial uncertainty in the exact orbit —
  among the least-constrained orbits of any known bound star). **Three or
  more mutually gravitating bodies have no general closed-form
  solution** — this is literally *the three-body problem* — so this
  system's real-time integration is the only way to show its motion at
  all, not a simplification of an available exact answer. For N≥3, the
  system's future evolution is genuinely chaotic, the same unsolved
  N-body problem real astrophysics deals with, not an artifact to "fix."
- **N-body (procedural), N up to 8.** Precisely-measured real bound
  systems of 4+ comparably-separated stars are rare and not something to
  present as verified catalog data from memory, so anything beyond the
  real triple above is explicitly procedural
  (`Universe::GenerateProceduralSystem`): masses drawn from a simplified
  plausible range (0.15–2.5 M☉, not a real initial-mass-function sample),
  temperatures from a simplified real mass-temperature power law for
  main-sequence stars, `T ≈ 5778 K × (M/M☉)^0.54` — an empirical fit, not
  a stellar-structure model.

**Initial conditions**, for any system size: members are laid out on a
circle at an arbitrary visual radius, then shifted so the *mass-weighted*
centroid sits exactly at the origin (`Σ mᵢrᵢ = 0`, the real barycenter
condition, true regardless of the arbitrary starting shape); each member
gets a tangential kick sized from its own local mutual-gravity
acceleration at its starting radius (`v = sqrt(|a| · r)`, a circular-orbit
estimate — exact for N=2, just a reasonable starting condition for N≥3's
chaos to evolve from), and the mass-weighted mean velocity is then zeroed
so the system's barycenter doesn't drift off-screen. No collision/escape
handling exists: `Physics::GravitySource`'s existing minimum-distance
floor (§14) keeps the force from diverging, so this won't crash, but a
visually extreme outcome (two members overlapping, one flung far away) is
a genuine possible consequence of real chaos, not guarded against.

## 20. Limitations

This was, until the Kerr and binary-black-hole additions above,
explicitly a **Schwarzschild** (non-rotating, uncharged) model; nothing
below is covered by either of those additions either:

- **Magnetic fields or full MHD.** The accretion disk is a static,
  analytic temperature/velocity profile, not a simulated plasma.
- **Realistic disk microphysics.** No opacity, no radiative transfer, no
  actual spectral emission model — `AccretionDisk::EmittedColor` is an
  explicitly-labeled perceptual approximation of blackbody color (§ in
  `AccretionDisk.cpp`), not a spectral integration.
- **Flux calibration.** Brightness values are visually tuned (a documented
  `kVisualNormalization` constant), not tied to physical luminosity units.
- **Stars and planets** (§13, §14): mutual gravity between spawned objects
  (§14) is an object-level, off-by-default, tuned-not-real effect, not
  particle-level N-body; planets add an explicit spring-cohesion model but
  no differentiated interior; both check only the primary/binary/extra
  black holes' horizons, never a rotating (Kerr) one; no collision/merger
  response when two objects' centers pass close together.
- **Extra black holes** (§12): no GPU-accelerated lensing (CPU fallback
  whenever any are placed); can't carry Kerr spin; nothing acts on them —
  black holes stay fixed/scripted sources even under §14's mutual gravity,
  no drift unless manually given a constant velocity.
- **Jet piloting** (§15): no lift model, flat sea-level-equivalent air
  density everywhere (no real atmosphere exists in orbit around a black
  hole), G-force is a flavor readout only; the relativistic speed-cap
  floor (§15) is an explicit, surfaced playability concession, not a
  claim that flight near a supermassive hole is still sub-light.
- **Universe / Star Map** (§16–§19): ~35 catalog stars/black holes/
  galaxies and 6 exoplanets is a small, hand-picked real set, not a bulk
  astronomical-catalog import; travel is an instantaneous narrative jump,
  never simulated continuous faster-than-light flight; Star System View
  and Multi-Star System View have no orbiting planets beyond the handful
  of real cataloged exoplanets, no moons, and no surface/on-foot
  exploration of any kind — this app's scope stops at space travel and
  orbital viewing.

Where an approximation or a simplification is made anywhere in the code,
it is labeled as such at the point it's made, rather than presented as
exact.
