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

## 10. Limitations

This is explicitly a **Schwarzschild** (non-rotating, uncharged) model.
It does not attempt:

- **Black hole spin / Kerr spacetime.** No frame dragging, no ergosphere,
  no asymmetric shadow. The architecture keeps `BlackHole` mass-only by
  design so a spin parameter and a Kerr metric module could be added
  later without restructuring the geodesic solver's interface, but no
  Kerr physics is implemented here.
- **Magnetic fields or full MHD.** The accretion disk is a static,
  analytic temperature/velocity profile, not a simulated plasma.
- **Realistic disk microphysics.** No opacity, no radiative transfer, no
  actual spectral emission model — `AccretionDisk::EmittedColor` is an
  explicitly-labeled perceptual approximation of blackbody color (§ in
  `AccretionDisk.cpp`), not a spectral integration.
- **Flux calibration.** Brightness values are visually tuned (a documented
  `kVisualNormalization` constant), not tied to physical luminosity units.

Where an approximation or a simplification is made anywhere in the code,
it is labeled as such at the point it's made, rather than presented as
exact.
