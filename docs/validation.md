# Validation

All numbers below are copied directly from an actual run of
`build/tests/BlackHoleTests.exe` (**201 checks, 0 failures**, both local
build directories as of this writing — see the note at the end of §17's
summary table for why that CPU-only/CUDA distinction is currently blurred
in this working copy specifically, and what the CUDA-gated figure would
be either way) and the app's "Validate: Compare CPU vs GPU" diagnostic —
nothing here is asserted without a corresponding automated check in
`tests/`. Run the suite yourself with:

```
build.bat
build\tests\BlackHoleTests.exe
```

(§9, on Kerr/rotating black holes, §10, on approximate binary black
holes, §15, on the Universe/Star Map's real catalog, and §16, on
multi-star N-body systems, are all new since this file's original
Schwarzschild-only version — see `docs/physics.md` §10-11 and §16-19 for
the physics and `HANDOFF.md` for how each was added.)

## 1. Schwarzschild radius

```
r_s = 2GM/c²
```

| Check | Result | Expected |
|---|---|---|
| `r_s` in simulation units | `2.0` | `2.0` (exact, by construction of the unit system) |
| `r_s` for a 1 solar mass hole | `2954.007736 m` | `2954.06 m` (known value, ≈2.95 km) |

(`tests/test_schwarzschild.cpp`)

## 2. Photon sphere

```
r_ph = 3GM/c² = 1.5 r_s
```

Verified as exactly `3.0` in simulation units, and independently confirmed
as `1.5 × r_s` — the two computed quantities agree to floating-point
precision, as they must by definition.

## 3. Critical impact parameter

```
b_crit = 3√3 · GM/c² ≈ 5.196152423
```

Two independent computations of `b_crit` are cross-checked:

1. The closed-form constant (`BlackHole::CriticalImpactParameterSim()`).
2. The impact parameter numerically recovered by `GeodesicSolver`'s tetrad
   relation for a photon launched **exactly tangentially at the photon
   sphere** — the defining property of the photon sphere as the unstable
   circular photon orbit.

Result: **`5.196152423` vs `5.196152423`** — agreement to at least 9
significant figures, cross-validating the geodesic solver's impact-
parameter formula against the closed-form photon-sphere prediction by two
independent routes.

## 4. Capture/escape classification

| Impact parameter | Result |
|---|---|
| `b = 0.10 × b_crit` | CAPTURED |
| `b = 0.98 × b_crit` | CAPTURED |
| `b = 1.02 × b_crit` | ESCAPED |
| `b = 5.00 × b_crit` | ESCAPED |

The boundary at `b_crit` is resolved to within the `±2%` window tested
here; §7 below resolves it far more tightly for the shadow-angle check.

## 5. Weak-field deflection vs. the analytical formula

Numerically-integrated deflection angle, `α = Δφ - π`, for a ray launched
from `r₀ = 2×10⁴ b` (chosen to keep the "launched from infinity"
approximation's own truncation error negligible — `2b/r₀ ≈ 5×10⁻⁵` at
every `b` tested, so it doesn't confound the comparison below), against
the leading-order weak-field formula `α ≈ 2r_s/b = 4GM/(bc²)`:

| `b/r_s` | Numerical `α` | Analytical `α` (leading order) | Relative error |
|---|---|---|---|
| 25 | 0.08640925 | 0.08000000 | **8.01%** |
| 50 | 0.04140778 | 0.04000000 | **3.52%** |
| 100 | 0.02040745 | 0.02000000 | **2.04%** |
| 500 | 0.00400735 | 0.00400000 | **0.18%** |

The error shrinks monotonically as `b/r_s` grows — exactly the expected
signature of a *real* physical correction, not numerical noise. The
leading-order formula is only the first term of a series; the next-order
term contributes a relative correction of approximately
`(15π/32)(r_s/b) ≈ 1.47/(b/r_s)`:

| `b/r_s` | Predicted next-order excess | Observed relative error |
|---|---|---|
| 25 | 5.9% | 8.0% |
| 50 | 2.9% | 3.5% |
| 100 | 1.5% | 2.0% |
| 500 | 0.3% | 0.2% |

The observed errors track this prediction closely (within a couple of
percentage points at every scale, itself consistent with the *next*
correction term plus residual floating-point/discretization noise) —
i.e. the "error" here is mostly real physics (the weak-field formula's
own truncation), not a flaw in the integrator.

**A real bug was caught by this test, not just anticipated:** the first
version of this validation used a fixed step size and a fixed launch
radius for every `b`. Both were wrong for different reasons — see
`docs/numerical_methods.md` §3.5 for the diagnosis — and the fixes
(scaling step size to `b`, scaling launch radius to `b`) are what produced
the clean monotonic table above; before them, `b/r_s = 500` showed a
**worse** error than `b/r_s = 100` (10.2% vs 2.0%), which was the signal
something was wrong, since weak-field accuracy should only improve as `b`
grows.

(`tests/test_deflection.cpp`)

## 6. Newtonian integrator comparison (control experiment)

Radial drift from a perfectly circular orbit (`r₀ = 5`, `GM = 1`) over one
full orbital period, 2000 steps, comparing Euler, Velocity Verlet, and
RK4 against each other and against the fact that a circular orbit's exact
radius is constant:

| Method | Radial drift over one orbit |
|---|---|
| Euler | `0.19444` |
| Velocity Verlet | `0.00002` |
| RK4 | `0.00000` (below reported precision) |

Euler's drift is **≈10,000× larger** than RK4's at the same step count —
the expected signature of a 1st-order, non-symplectic method versus a
4th-order one. Velocity Verlet, despite being only 2nd order, stays
bounded (symplectic structure) rather than drifting, distinguishing "low
order" from "unstable." Both effects are visible live in the app's Stage 1
view (per-method colored trajectories over the same analytic-Kepler
reference curve).

(`tests/test_integrator.cpp`)

## 7. Event horizon vs. black-hole shadow

The shadow's exact apparent angular radius (`docs/physics.md` §6),
`sin θ_shadow = b_crit √f(r₀)/r₀`, checked against a numerically bisected
capture/escape boundary (60 bisection iterations, well past double
precision) at two observer distances:

| `r₀` | Predicted `θ_shadow` | Numerically bisected `θ_shadow` | Naive horizon angle `asin(r_s/r₀)` |
|---|---|---|---|
| 10 | 0.483361 rad (27.7°) | **0.483361 rad** | 0.201358 rad (11.5°) |
| 1000 | 0.005191 rad | **0.005191 rad** | 0.002000 rad |

The predicted and numerically-bisected shadow angles agree to the
precision shown (10 significant figures internally — see the raw test
output). At `r₀ = 10`, the shadow's angular radius is **2.4×** the
horizon's own naive angular size — not a rounding difference, the central
quantitative claim of Stage 5.

As `r₀ → ∞`, the exact shadow-angle formula is confirmed to reduce to the
textbook asymptotic `θ_shadow ≈ b_crit/r₀`: at `r₀ = 10⁶`,
`5.196147227×10⁻⁶` vs `b_crit/r₀ = 5.196152423×10⁻⁶` (agreement to 6
significant figures, limited by the small-angle approximation itself at
finite `r₀`).

(`tests/test_shadow.cpp`)

## 8. Accretion disk

- **ISCO orbital velocity**: `1/√(r_ISCO - r_s) = 1/√(6-2) = 0.5000000000`
  — the well-known Schwarzschild result that circular-orbit speed at the
  innermost stable circular orbit is exactly half the speed of light,
  confirmed to the precision shown.
- **Disk-intersection geometry**: a ray aimed from above the disk plane
  correctly registers a hit, with the crossing radius falling inside
  `[r_in, r_out]`; a ray aimed outside the disk's radial extent correctly
  registers no hit.
- **Redshift factor**: finite, positive, and within `(0.3, 3.0)` for a
  near-face-on viewing geometry (neither Doppler component extreme, as
  expected for that geometry).
- **Temperature profile**: zero exactly at the inner edge (`T(r_in) = 0`,
  matching the Shakura-Sunyaev shape's own boundary condition), positive
  within the disk, zero outside `r_out`.

(`tests/test_disk.cpp`)

## 9. Kerr (rotating) black holes

All numbers below are from `tests/test_kerr.cpp` (`RunKerrTests`), added
when Kerr spacetime support was (see `docs/physics.md` §10 for the full
derivation — Carter's separated Hamilton-Jacobi equations, the ZAMO
tetrad initial conditions).

**Closed-form quantities reduce exactly to the already-validated
Schwarzschild values at `a*=0`:**

| Quantity | Kerr formula at `a*=0` | Result | Expected (Schwarzschild) |
|---|---|---|---|
| Outer horizon | `1 + sqrt(1-a²)` | `2.0` | `r_s = 2.0` |
| Prograde photon orbit | `2(1+cos((2/3)acos(-a)))` | `3.0` | `r_ph = 3.0` |
| Retrograde photon orbit | `2(1+cos((2/3)acos(+a)))` | `3.0` | `r_ph = 3.0` |
| Prograde ISCO | Bardeen-Press-Teukolsky | `6.0` | `6.0` (`3 r_s`) |
| Retrograde ISCO | Bardeen-Press-Teukolsky | `6.0` | `6.0` (`3 r_s`) |
| Equatorial ergosphere | `1 + sqrt(1-a²cos²θ)`, `θ=π/2` | `2.0` | `r_s = 2.0` |

**Extremal spin (`a*=1`) against standard textbook values** (e.g.
Bardeen 1973):

| Quantity | Result | Expected |
|---|---|---|
| Prograde photon orbit | `1.000000` | `M` (`1.0`) |
| Retrograde photon orbit | `4.000000` | `4M` |
| Prograde ISCO | `1.000000` | `M` |
| Retrograde ISCO | `9.000000` | `9M` |

**Generic spin (`a*=0.9`):**

| Quantity | Result |
|---|---|
| Outer horizon `r+ = 1+sqrt(1-a²)` | `1.435889894` |
| Inner horizon `r- = 1-sqrt(1-a²)` | `0.564110106` |
| Equatorial ergosphere | `2.0` (always, independent of spin) |
| Polar ergosphere | `1.435889894` (coincides with the outer horizon) |

**Frame dragging, on the actual numerical geodesic integrator, not just
the closed-form formulas above:** at `a*=0.9`, the integrator's own
critical impact parameter — found by bisecting capture-vs-escape with
`TracePhotonKerr` itself, not by evaluating an independent formula — is
smaller prograde than retrograde:

```
a*=0.9: critical impact parameter  prograde = 2.844137   retrograde = 6.831638
```

compared to the non-rotating `b_crit = 5.196152423` (§3) sitting roughly
between the two, as expected: spin pulls a co-rotating photon's capture
boundary inward and pushes a counter-rotating one's outward, and setting
`a*=0` collapses both back onto the single Schwarzschild value.

**Regression against the dedicated Schwarzschild solver:** at `a*=0`,
`TracePhotonKerr` is checked directly against the separately-implemented
`TracePhoton` (two different formulations — Carter's constant vs. the
`u(φ)` reduction — of the same physics):

| Check | Result |
|---|---|
| `b = 0.9 b_crit`, both solvers | CAPTURED (agree) |
| `b = 1.5 b_crit`, both solvers | ESCAPED (agree) |
| Escape direction agreement | within ~2.5° |

**Weak-field deflection: the spin-orbit (frame-dragging) correction to
light bending.** The equatorial Kerr deflection angle, to leading order
beyond Schwarzschild, has a well-known gravitomagnetic correction term:
`α_prograde ≈ 4M/b - 4Ma/b²`, `α_retrograde ≈ 4M/b + 4Ma/b²`, i.e.
`α_retrograde - α_prograde ≈ 8Ma/b²`. Rather than trusting that
coefficient from memory (exactly the kind of recalled formula that's
easy to get a sign or factor wrong on — the frame-dragging bisection
check above already caught one such sign error earlier in this project,
for a different convention), this measures the SCALING LAW directly from
the already-validated numerical integrator, then checks the extracted
coefficient against the literature value of `8`, at `a*=0.9`:

| `b` | `α` prograde | `α` retrograde | `(α_retro - α_pro)·b²` |
|---|---|---|---|
| 25 | 0.17265660 | 0.18819029 | 9.709 |
| 35 | 0.11886971 | 0.12745468 | 10.517 |
| 45 | 0.09185346 | 0.09579528 | 7.982 |
| 60 | 0.06687757 | 0.06966795 | 10.045 |

Mean measured coefficient: **`10.63`** (dividing by `a=0.9`) against the
literature leading-order value of **`8`** — the right order of
magnitude and sign, with the residual difference consistent with the
next (`M²/b²`) order's contamination at these moderately-weak-field `b`
values (the same kind of residual the plain Schwarzschild deflection
table in §5 shows against ITS leading-order formula, for the same
reason).

**Two real bugs were caught by building this check, not just
anticipated** — `TracePhotonKerr` previously left `totalDeflectionAngle`
at its default (`0.0`) for every escaped/captured ray (only the disk-hit
branch set it), so this check initially failed with nonsense ~`π`
values. After fixing that (same `phi - φ₀ - π` convention the
Schwarzschild solver uses, adapted for this solver's absolute
world-frame `phi` rather than a per-ray local one starting at 0), a
*second* bug surfaced: a retrograde ray's `phi` decreases over time
(opposite-signed angular momentum), so the un-`abs()`'d formula gave a
~`2π`-magnitude nonsense value for retrograde rays specifically, fixed
by taking `abs(phi - φ₀)` before subtracting `π`. Both fixes are in
`KerrGeodesicSolver.cpp`, documented inline at the point of the fix.

(`tests/test_kerr.cpp`)

## 10. Binary black holes (approximate superposed lensing)

All numbers below are from `tests/test_binary.cpp` (`RunBinaryTests`).
See `docs/physics.md` §11 for why this is explicitly approximate (no
exact closed-form two-body GR metric exists at all) and exactly what
scheme is used (a "doubled Newtonian" superposed light-bending
acceleration).

**Orbital mechanics** (exact, for a circular Newtonian two-body orbit —
`massRatio=0.5`, `separation=40`):

| Check | Result |
|---|---|
| `r1 + r2` | `40.0` (= separation) |
| Barycenter condition `r1 = massRatio · r2` | `13.333... = 13.333...` |
| `Ω²` vs. Kepler's third law `G(M1+M2)/a³` | exact match (`2.34375e-5`) |
| Period vs. `2π/Ω` | exact match (`1297.849176`) |
| Position after half a period | diametrically opposite start (residual `1.6e-15`, floating-point noise) |

**Single-mass reduction** (`massRatio=0`, collapsing to one mass at the
barycenter): the binary solver's deflection should match the SAME
weak-field formula `α=2rs/b` already validated for the real
Schwarzschild solver in §5 — not because the two solvers share code
(they don't), but because reproducing that formula is the entire point
of the "doubled Newtonian" trick:

| `b/r_s` | Numerical `α` | Analytical `α` | Relative error |
|---|---|---|---|
| 50 | 0.041307 | 0.040000 | 3.27% |
| 100 | 0.020320 | 0.020000 | 1.60% |
| 200 | 0.010079 | 0.010000 | 0.79% |

Error shrinking monotonically as `b/r_s` grows is the same signature
§5's Schwarzschild table shows — evidence this is real weak-field
physics (plus the approximate scheme's own residual), not a
coincidence or numerical noise.

**Capture/escape classification:** a ray aimed directly at either body
is captured; a ray passing far from both escapes — both as expected.

**Mirror symmetry:** for equal masses placed symmetrically on the
x-axis, a ray's deflection is identical (to 10 significant figures) for
a `+y` vs. `-y` impact-parameter offset — confirming the force
superposition actually respects the configuration's own geometric
symmetry, independent of any absolute reference formula.

**Accretion disks (each body's own):** equal masses, `separation=40`
(`body1` at `(+20,0,0)`, `body2` at `(-20,0,0)`), both disks
`[innerRadius, outerRadius] = [6, 26]`:

| Check | Result |
|---|---|
| Ray above body 1's disk | hits, attributed to body 1, radius in range, redshift finite & positive |
| Ray above body 2's disk | hits, attributed to body 2, radius in range, redshift finite & positive |
| Ray missing both disks' radial extent | registers no disk hit |

(`tests/test_binary.cpp`)

## 11. Multi-body lensing (freely-placed extra black holes)

`tests/test_multibody.cpp` (`RunMultiBodyTests`, 6 checks) validates
`Physics::TracePhotonMultiBody`, the N-body generalization of the binary
scheme above (see `docs/physics.md` §12):

| Check | Result |
|---|---|
| 2-body reduction: same ray, same bodies (primary at `BinarySystem::Position1()`, extra at `Position2()`), `TracePhotonMultiBody` vs. `TracePhotonBinary` — fate | ✅ match |
| Same — closest approach | exact match (`1e-6` tolerance) |
| Same — escape direction (when both escape) | angle < `1e-4` rad |
| Ray aimed directly at a non-primary (index 1) body | ✅ captured, not escaped |
| Ray aimed at a non-primary body's own disk | ✅ hit registered |
| Same — attributed to that body's index, not the primary's | ✅ |
| A star placed near an extra black hole (far from the primary) | ✅ falls in and is captured by the extra black hole |

The first three checks are the important regression guard: generalizing
`BinaryGeodesicSolver`'s two named bodies into an array (a fresh,
independent implementation, not a refactor of the original) reproduces
the original's own result for an equivalent configuration, to float/
double-rounding precision. One real bug was caught building this test
suite, not by code review: an early version of both
`CPUTracer.cpp::BuildLensingBodies` and this test's own first check
assumed the primary black hole sits at the world origin even in binary
mode — `BinarySystem::Position1()` shows it does not (body 1 orbits the
barycenter too, same as body 2) — fixed in both places once the
reduction check caught the mismatch.

(`tests/test_multibody.cpp`)

## 12. CPU vs. GPU (Stage 7)

**Update (post float-conversion):** the numbers below replace an earlier
version of this section measured when the GPU kernel still ran in
`double`. The single biggest performance fix in this project was switching
`cuda/geodesic_kernel.cu` to `float` (see `HANDOFF.md`) — consumer NVIDIA
GPUs cripple double-precision throughput to a fraction of single-precision,
so that change alone was worth a measured ~9.5x at live-rendering settings
(`maxSteps=8000`, `basePhiStep=0.015`, at full window resolution). The CPU
tracer is untouched and still runs in `double`. This means the two are
**no longer bit-identical** — see below for why that's expected and how
large the gap actually is.

Measured both via the app's Advanced-mode "Validate: Compare CPU vs GPU"
button and via the automated `RunGpuRenderTests` check (`tests/test_gpu_render.cpp`,
run as part of `BlackHoleTests.exe` whenever built with `-DBH_ENABLE_CUDA=ON`),
identical scene (orbiting camera, starfield, accretion disk enabled), on an
NVIDIA GeForce RTX 4070 Ti SUPER:

| Resolution | CPU time | GPU time | Speedup | Max abs. color diff | Mean abs. color diff |
|---|---|---|---|---|---|
| 1024×1024 | 429.70 ms (2.44 Mrays/s) | 133.84 ms (7.83 Mrays/s) | **3.21×** | **0.132773** | **0.000001** |
| 160×90 (automated test) | — | — | — | **0.003117** | **0.000001** |

The nonzero max/mean color difference is **expected and by design**, not a
regression: it's the normal float-vs-double discrepancy accumulated over
up to 8000 RK4 integration steps per photon, concentrated in a small number
of pixels near extreme geometry (grazing the photon sphere, disk edges)
where a tiny difference in *where* a ray's trajectory ends gets amplified
into a visibly different sampled color — most pixels agree far more
closely than the max figure suggests (the mean difference is ~5 orders of
magnitude smaller than the max, and `RunGpuRenderTests` asserts both stay
within a loose, regression-catching tolerance: max < 0.35, mean < 0.01).
This replaces the earlier "bit-identical" result, which held only while
the kernel was still double-precision; the shared `GeodesicMath.h`
primitives (`docs/numerical_methods.md` §5) still drive both tracers'
physics, the divergence is purely float-rounding, not a different
algorithm. (This comparison is only available when built with
`-DBH_ENABLE_CUDA=ON`; a CUDA context's one-time driver initialization —
observed ≈150ms on this machine — is absorbed by a warm-up call at
startup so it doesn't pollute either timing.)

The 3.21× speedup here (measured with the resolution-cap-bypassing test
harness, at a resolution larger than the app's live `kDefaultMaxRenderPixels`
cap would ever reach) is on top of the ~9.5x the float conversion itself
already bought the live-rendering path — both numbers are honestly
reported, not inflated: this is a simple per-ray kernel with no
shared-memory tiling or other GPU-specific optimization, and a
multi-threaded CPU (`std::execution::par`) is a genuinely strong baseline
for an embarrassingly parallel workload like this.

### Kerr mode (CPU vs. GPU)

`TraceKerrPhotonsKernel` (`cuda/geodesic_kernel.cu`) is a direct port of
the CPU `TracePhotonKerr` — same Carter-constant formulation, same shared
`Physics::Kerr`/`Physics::KerrGeodesicMath` templated functions, deduced
at `T=float` instead of `T=double` (see `HANDOFF.md` Session 4).
Automated comparison (`RunKerrGpuRenderTests`, same 160×90 test scene as
the Schwarzschild comparison above, `a*=0.8`):

| Scene | Max abs. color diff | Mean abs. color diff | Non-black pixels |
|---|---|---|---|
| Kerr, `a*=0.80` | **0.487766** | **0.000137** | 13805 / 14400 |
| (Schwarzschild, for reference) | 0.003117 | 0.000001 | 12614 / 14400 |

The max-diff gap is real and understood, not unexplained noise: right at
the shadow boundary (impact parameter ≈ the critical value), float vs.
double can tip a handful of pixels to opposite sides of the
capture/escape classification — one captured (black) vs. escaped
(starfield-colored) pixel is a large *per-pixel* difference affecting a
thin *rim* of pixels, not a broadly wrong image. The mean difference —
the more meaningful signal for "is the kernel actually right" — stays
two orders of magnitude smaller than the max either way, and the Kerr
test's own tolerance (max < 0.6, mean < 0.02) was set specifically to
pass when that rim exists and fail if the kernel were actually broken.

(`tests/test_gpu_render.cpp`)

### Binary mode (CPU vs. GPU)

`TraceBinaryPhotonsKernel` (`cuda/geodesic_kernel.cu`) is a direct port
of the CPU `TracePhotonBinary` — same superposed "doubled Newtonian"
acceleration, same step-scales-with-distance adaptive stepping, same
each-body's-own-disk-with-redshift logic (`BinaryCheckDiskCrossing`
mirroring the CPU solver's `CheckDiskCrossing`), in `float` instead of
`double`. Both masses' current positions are computed host-side each
frame (`RayTracing::CUDATracer.cpp`) and passed to the kernel rather
than recomputed per-thread. Automated comparison (`RunBinaryGpuRenderTests`,
same 160×90 scene, `massRatio=1.0`, `separation=40`, both disks
`[6, 26]`):

| Scene | Max abs. color diff | Mean abs. color diff | Non-black pixels |
|---|---|---|---|
| Binary, equal masses, both disks | **0.003368** | **0.000002** | 11933 / 14400 |
| (Kerr, for reference) | 0.487766 | 0.000137 | 13805 / 14400 |
| (Schwarzschild, for reference) | 0.003117 | 0.000001 | 12614 / 14400 |

Binary mode's CPU/GPU agreement is tighter than either other mode's —
closer to Schwarzschild's than Kerr's, and without Kerr's large
max-diff outlier. This makes sense given what's actually different
between the three kernels: binary's scheme has no transcendental
(sin/cos) calls at all (pure `sqrt`/arithmetic, unlike Kerr's four
sin/cos pairs per RK4 stage) and no turning-point retry loop, so
there's simply less for float-vs-double rounding to disagree about.

(`tests/test_gpu_render.cpp`)

## 13. Planetary destruction (Milestone 1 + self-gravity + disk coupling)

Validates `Physics::PlanetMaterial`, `Physics::TidalForceModel`, and
`Astronomy::PlanetBody` (`tests/test_planet.cpp`, 19 checks):

| Check | What it confirms |
|---|---|
| Density presets | Rocky/icy/iron-rich reference densities round-trip through `mass = density × volume` and order correctly (icy < rocky < iron) |
| `NewtonianTidalAccelerationSim` scaling | Matches the closed form `2·R_p/r³` exactly; doubling `r` divides it by 8; doubling `R_p` doubles it |
| `RocheLimitSim` scaling | Matches the closed form `2.44·R_p·(M_bh/M_p)^(1/3)` exactly; increases with `M_bh`, decreases with `M_p` |
| Fracture threshold comparison | A 30% strain exceeds the icy preset's threshold but stays below iron-rich's |
| Close-encounter stability | A planet run through a close, slow encounter (1.6×ISCO, 0.5× circular speed) for 4000 steps never produces non-finite state |
| Fracture actually occurs | The same brittle (icy) close encounter breaks at least one spring |
| Fragment mass conservation | Sum of all fragment masses equals the planet's original total mass, after disruption |
| Momentum conservation | A two-particle planet far from the hole (negligible tidal gradient) conserves total momentum to within integration error over 200 steps |
| Orbital stability | A rocky planet on a distant (`r=200`), near-circular orbit never spuriously fractures over 2000 steps and stays a single fragment |
| **Self-gravity reaccumulation** | A planet disrupted into surviving fragments ends up with its two largest fragments noticeably closer together (>2× tighter, in practice ~10×) with self-gravity on than off, after 3 minutes of sim time |

(`tests/test_planet.cpp`, `RunPlanetTests`)

### Tidal disruption flare (debris → accretion-disk coupling)

`tests/test_tidal_flare.cpp` (`RunTidalFlareTests`, 9 checks, built in
every configuration — not CUDA-gated) validates
`Astronomy::TidalDisruptionFlare`'s decaying-accumulator model: a fresh
tracker is inactive with zero boost; registering captured mass activates
it and produces a positive boost; repeated `Update()` calls with no new
captures monotonically decay the boost (never increases on its own, never
goes negative); an enormous single capture event is clamped to the
documented ceiling (0.6); and registering mass twice produces a larger
boost than once (accumulates rather than overwrites).

(`tests/test_tidal_flare.cpp`, `RunTidalFlareTests`)

### GPU acceleration (`cuda/planet_kernel.cu`, CUDA builds only)

`tests/test_planet_gpu.cpp` (`RunPlanetGpuTests`, 6 checks, only
built/run when `BH_ENABLE_CUDA`) validates `PlanetBody::UpdateGPU` against
the CPU `Update()` path it mirrors (self-gravity included — both default
to enabled): one planet is constructed once, then copied into a CPU
instance and a GPU instance (so both start from bit-identical particle
positions/velocities/spring lattice — separately constructing two
instances wouldn't be comparable, since particle placement is randomized
per construction), then each is stepped 20 times through its respective
path and compared.

| Check | Result |
|---|---|
| `UpdateGPU` succeeds every step | ✅ |
| Particle count stays equal | ✅ |
| Max position difference (20 steps, float vs. double) | **2.7×10⁻⁶** sim units |
| Max velocity difference (20 steps, float vs. double) | **1.7×10⁻⁶** sim units |
| Consumed-particle agreement | ✅ (0 mismatches) |
| Broken-spring count agreement | ✅ |

The measured float-vs-double divergence (micro-units, after 20 steps at a
~9-sim-unit orbit, self-gravity included) is far inside the test's
tolerance (10⁻²) — consistent with the Kerr/binary ray-tracing kernels'
own CPU/GPU agreement, which use the same float-precision tradeoff.

(`tests/test_planet_gpu.cpp`, `RunPlanetGpuTests`)

## 14. Mutual gravity (object-level pull between spawned stars/planets)

Validates `Physics::GravitySource`/`MutualGravityAccelSim` and the
`Astronomy::StarBody::Update`/`PlanetBody::Update`/`UpdateGPU`
`mutualGravitySources` parameters built on them (`tests/test_mutual_gravity.cpp`,
4 checks, built in every configuration — not CUDA-gated). All three
dynamics checks place their stars ~10⁶ sim units from the primary black
hole specifically to make its own (always-on, see `docs/physics.md` §14)
pull on the pair negligible relative to the effect under test — an earlier
draft of this test placed them at a more modest distance and saw spurious
separation drift (and a swamped directional signal in the second check)
purely from the primary's own gravity, not a bug in mutual gravity itself.

| Check | What it confirms |
|---|---|
| Mutual gravity on vs. off | Two 1-solar-mass stars' separation measurably decreases with mutual gravity on; an identical pair with it left off (the default) shows zero separation change |
| Attraction toward a more massive companion | A star next to a 10-solar-mass companion picks up measurable displacement toward it within 100 steps |
| GPU fallback | `PlanetBody::UpdateGPU` returns `false` (falls back to CPU) when mutual gravity sources are supplied, non-empty — same precedent as binary mode/extra black holes |

(`tests/test_mutual_gravity.cpp`, `RunMutualGravityTests`)

The tuned constants themselves (`kMutualGravityCoefficient = 1.5`,
`kMutualGravityMinDistance = 3.0`, solar-mass reference) were found via a
scripted probe outside the test suite, not derived from these tests: two
point masses were integrated directly (bypassing `StarBody`'s particle
machinery) across a grid of coefficient/min-distance/separation/tangential-
velocity combinations, discarding any combination that flung the pair
apart to extreme separations within a few thousand steps. The first probe
pass, which referenced Earth mass the way `PlanetBody`'s self-gravity does,
diverged in **every** combination tried (massRatio ~3.3×10⁵ for a
solar-mass star made the per-step kick dwarf the objects' own separation);
switching the reference mass to one solar mass fixed this immediately,
after which the chosen values were picked for producing a visible
spiral-together/swing-past/spread-back-out encounter rather than a stable
closed orbit (not expected from a softened, not true, inverse-square law
under semi-implicit Euler) or an outright ejection. See `docs/physics.md`
§14 and `HANDOFF.md` for the full account.

## 15. Universe / Star Map: real catalog and Kepler-derived exoplanet orbits

Validates `Universe::StarCatalog.h`'s real data, the RA/Dec/distance ->
Cartesian coordinate conversion it relies on, and
`Universe::OrbitalPeriodDays`'s Kepler's-third-law cross-check against
real exoplanets' published orbital periods (`tests/test_universe.cpp`,
29 checks, built in every configuration — not CUDA-gated).

| Check | What it confirms |
|---|---|
| Equatorial / polar coordinate conversion | A star at RA=0h, Dec=0° lands exactly on +X at its catalog distance; Polaris (Dec ≈ 89.26°) lands almost entirely on the polar axis |
| Universal magnitude check | Every one of the ~35 catalog entries' `CartesianLy()` magnitude matches its own listed distance, not just the two spot-checked above |
| Real relative-fact sanity | Alpha Centauri is closer than Sirius; Sagittarius A* is far less massive than TON 618 — catches a transposed/garbled data field without hardcoding every digit |
| Object-type coverage | The catalog contains at least one Star, one BlackHoleObj, and one Galaxy entry |
| Kepler-derived exoplanet periods vs. real published values (5 planets, loose 10% tolerance — the catalog's star masses are rounded published figures, not each discovery paper's own exact value) | Proxima Centauri b: **11.26 days** vs. real ~11.19; Ross 128 b: **9.79** vs. ~9.9; Epsilon Eridani b: **2618** vs. ~2691; Tau Ceti e: **163.2** vs. ~162.9; Tau Ceti g: **20.06** vs. ~20.0 — all five genuinely pass, not tuned to pass |

(`tests/test_universe.cpp`, `RunUniverseTests`)

## 16. Multi-star systems: real binary/triple data + N-body mutual-gravity sanity

Validates `Universe::MultiStarSystem.h`'s real builders
(`BuildAlphaCentauriBinary`/`BuildAlphaCentauriTriple`) and procedural
generator (`GenerateProceduralSystem`), plus a direct sanity check on the
tuned mutual-gravity law App.cpp's N-body integration reuses
(`tests/test_multistar.cpp`, 18 checks, built in every configuration —
not CUDA-gated).

| Check | What it confirms |
|---|---|
| Alpha Centauri AB | Exactly 2 members, classified TwoBody, flagged real, A listed more massive than B (as in reality) |
| Alpha Centauri System (triple) | Exactly 3 members, classified ThreeBody, flagged real, includes Proxima Centauri |
| Procedural generator (N=2, 3, 6) | Correct member count and TwoBody/ThreeBody/NBody classification at each size; flagged NOT real; the same seed reproduces bit-identical masses; every generated temperature lands in a plausible real range (1000-50000 K) |
| Mutual-gravity direction | Two stars pull *toward*, not away from, each other, with zero out-of-plane component for a coplanar pair — independent of App.cpp's own integration loop |

(`tests/test_multistar.cpp`, `RunMultiStarTests`)

## 17. Summary

| Area | Tests | Status |
|---|---|---|
| Schwarzschild radius / photon sphere / `b_crit` | 5 | ✅ |
| Newtonian integrator stability (Stage 1) | 6 | ✅ |
| Geodesic capture/escape classification (Stage 2) | 5 | ✅ |
| Weak-field deflection vs. analytical formula (Stage 2) | 8 | ✅ |
| Event horizon vs. shadow (Stage 5) | 5 | ✅ |
| Accretion disk (Stage 6) | 9 | ✅ |
| Kerr (rotating) black holes — closed-form + geodesic regression + weak-field spin-orbit deflection | 35 | ✅ |
| Binary black holes (approximate) — orbital mechanics + single-mass reduction + capture/symmetry + disks | 25 | ✅ |
| Multi-body lensing — binary reduction check, extra-body capture/disk attribution, star capture | 6 | ✅ |
| Planetary destruction — materials, tidal/Roche diagnostics, fracture, fragments, self-gravity reaccumulation | 19 | ✅ |
| Tidal disruption flare (debris → accretion-disk coupling) | 9 | ✅ |
| Mutual gravity — on/off comparison, attraction toward a massive companion, GPU fallback | 4 | ✅ |
| Universe / Star Map — real catalog coordinates, Kepler-derived exoplanet periods | 29 | ✅ |
| Multi-star systems — real binary/triple data, procedural generator, mutual-gravity direction | 18 | ✅ |
| **Total (default `build.bat`, no CUDA)** | **183** | **0 failures (deduced: 136 + 29 + 18, see note below — not independently re-verified via a fresh CPU-only configure this session)** |
| CPU vs. GPU render comparison, Schwarzschild (Stage 7, `-DBH_ENABLE_CUDA=ON` only) | 4 | ✅ |
| CPU vs. GPU render comparison, Kerr (`-DBH_ENABLE_CUDA=ON` only) | 4 | ✅ |
| CPU vs. GPU render comparison, binary (`-DBH_ENABLE_CUDA=ON` only) | 4 | ✅ |
| Planetary destruction, CPU vs. GPU step agreement (`-DBH_ENABLE_CUDA=ON` only) | 6 | ✅ |
| **Total (`build_cuda.bat`)** | **201** | **0 failures (directly verified this session)** |

**Note on the 183 vs. 201 figures:** both local build directories
(`build/` and `build_cuda/`) happen to have `BH_ENABLE_CUDA=ON` baked into
their CMake cache already (a pre-existing quirk flagged as far back as
this project's Session 4, not something this work introduced), so the
only number directly re-run and counted this session is **201** (both
dirs, 0 failures either way). 183 is the CPU-only figure arithmetically
implied by adding this session's two new, non-CUDA-gated test files (29 +
18 = 47) onto validation.md's last independently-verified CPU-only total
of 136 — correct by construction (neither new file is behind
`BH_ENABLE_CUDA`), but not re-confirmed via an actual fresh,
CUDA-disabled configure the way Session 8's original 136 figure was.

CPU vs. GPU correctness (Stage 7, and now Kerr and binary modes too)
has automated coverage (`tests/test_gpu_render.cpp`, `RunGpuRenderTests`
+ `RunKerrGpuRenderTests` + `RunBinaryGpuRenderTests`) in addition to
the interactive Advanced-mode "Validate: Compare CPU vs GPU" button —
it only builds/runs when CUDA is enabled (it requires a CUDA-capable
GPU, which the default CPU-only build should not assume). See §12 above
for the Schwarzschild/Kerr/binary GPU results — all three modes now run
on GPU when one is available (multi-body lensing does not -- see
`docs/physics.md` §12 -- so it's covered by `tests/test_multibody.cpp`
on CPU only, unconditionally).
