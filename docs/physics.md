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

## 12. Stars and planets: tidal disruption

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

## 13. Limitations

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
- **Stars and planets** (§12): no self-gravity for either; planets add an
  explicit spring-cohesion model but no differentiated interior, no
  self-gravity among fragments, and no GPU acceleration; both check only the
  single-hole Schwarzschild radius for capture.

Where an approximation or a simplification is made anywhere in the code,
it is labeled as such at the point it's made, rather than presented as
exact.
