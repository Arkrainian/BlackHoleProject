# Video Script: "Singularity — A Black Hole Simulator That Actually Does the Physics"

This is the **extended, equation-heavy cut** — written for a devlog /
physics-explainer audience that wants the actual math on screen, not just
the result. Target length: ~9:30–10:30 at ~140 words/minute (nearly double
the original 5-minute cut — see Production Notes at the end for a trim
path back down if a shorter version is needed). Screen-recording cues are
in **[brackets]**; text meant to appear as an on-screen equation overlay
is in **`[EQUATION: ...]`**; spoken narration is plain text.

Every equation named below is implemented, literally, in the codebase —
see `docs/physics.md` for the full derivation of each one and
`docs/validation.md` for the automated test that checks it against a
closed-form analytical answer. Nothing here is "for the video only."

---

## 0:00–0:30 — Cold open: the shadow forming

**[Screen: launch the app. It opens on the default scene — TON 618,
accretion disk on, slowly orbiting. Let it breathe for 3-4 seconds before
talking.]**

> This is Singularity. Not a texture, not a pre-rendered image — every
> pixel you're looking at right now is the result of a physics equation
> being solved, live, as you watch. That dark circle isn't drawn. It's
> what happens when you actually trace rays of light through curved
> spacetime and ask: which ones come back, and which ones don't?

**[Zoom in slowly on the shadow's edge, where the disk visibly wraps
around behind it.]**

> That's the part that should look wrong, and doesn't. The far side of
> the disk, bent completely around the hole so you can see it twice. This
> is what it would actually look like if you flew up to a real black
> hole. We know that because of Interstellar's Gargantua — and that was
> real GR ray tracing too.

**[Screen: brief title card — "Singularity" — then cut to the project's
source tree / build script.]**

> Singularity is built entirely in C++, with the heavy ray-tracing math
> accelerated on the GPU using NVIDIA's CUDA — that combination is what
> makes solving a physics equation for every pixel, every frame, actually
> possible in real time.

---

## 0:30–1:00 — Units: one equation that works for every mass

**[Screen: Advanced mode, the mass slider, dragging it from a stellar mass
up to TON 618's 60 billion solar masses.]**

> Before any of the physics, one trick makes all of it tractable. Every
> internal calculation is done in units where gravity's constant and the
> speed of light both equal 1, and distance is measured in multiples of
> `GM/c²`.

**`[EQUATION: G = c = 1,  length unit = GM/c²  →  Schwarzschild radius r_s = 2 exactly]`**

> That means a stellar black hole and TON 618 are, internally, *the exact
> same math problem* — just rescaled for display afterward. It's the
> standard move in numerical relativity, and it's why this app can jump
> between a 10-solar-mass hole and a 60-billion-solar-mass one without
> changing a single line of integration code.

---

## 1:00–2:15 — The Schwarzschild metric and the geodesic equation

**[Screen: switch to Advanced mode, scroll to the "Black Hole" panel —
show the mass slider, the Schwarzschild radius readout.]**

> Here's the actual idea. Around a non-rotating black hole, spacetime
> itself is described by one equation — the Schwarzschild metric.

**`[EQUATION: ds² = -f(r)dt² + dr²/f(r) + r²(dθ² + sin²θ dφ²),   f(r) = 1 - r_s/r]`**

> Light doesn't travel in straight lines through it; it follows "null
> geodesics" — paths where that interval, `ds²`, is exactly zero. Because
> the metric is spherically symmetric, every one of those paths stays in
> a single flat plane through the center — so the solver only ever has to
> integrate a 2D problem, radius and angle, even for a fully free 3D
> camera ray.

**[Screen: briefly show `GeodesicSolver.cpp` / `GeodesicMath.h` in the
editor — scroll past the RK4 step function, don't read code line by
line, just let it register as "this is real code, not a shader hack."]**

> The code doesn't integrate all four coordinates directly — turning
> points make that awkward. Instead it substitutes `u = 1/r` and reduces
> the whole problem to one second-order equation in the angle `φ`:

**`[EQUATION: d²u/dφ² = -u + (3/2) r_s u²]`**

> which comes from the first-integral form

**`[EQUATION: (du/dφ)² = 1/b² - u²(1 - r_s u)]`**

> where `b` is the ray's impact parameter — how close it would pass if
> spacetime weren't bent at all. That `(3/2) r_s u²` term is the entire
> difference between Newton and Einstein here: delete it, and you get
> ordinary flat-space optics. Keep it, and you get everything — the
> photon sphere, the Einstein ring, light bent clean around the back of
> the hole.

**[Screen: show the Stage-1-vs-Stage-2 side-by-side comparison if the
build has it — Newtonian 1/r² trajectories next to the real geodesic.]**

> This project actually built the Newtonian version first, on purpose —
> as a control. Same starting rays, same impact parameters, ordinary
> inverse-square gravity instead of the real metric. Running both side by
> side is how you *prove* the extra bending isn't an art choice; it's a
> direct consequence of one additional term in one equation.

> That equation is solved with RK4 integration and an adaptive step size
> that automatically gets finer the closer a ray passes to the hole — the
> difference between a ray that resolves cleanly and one that blows up
> numerically right at the edge of the photon sphere.

---

## 2:15–3:00 — The photon sphere, the critical impact parameter, and the shadow

**[Screen: show the shadow with the photon-sphere radius overlaid or
labeled, if the UI has a debug toggle for it.]**

> Treat that equation as describing a ball rolling in a potential well,
> and the well has a peak — a point of unstable equilibrium — at exactly:

**`[EQUATION: r_ph = 3GM/c² = 1.5 r_s   (the photon sphere)]`**

> A photon launched exactly tangentially there orbits forever, unstably.
> Every ray that comes in with an impact parameter below a critical value

**`[EQUATION: b_crit = 3√3 · GM/c² ≈ 5.196  (sim units)]`**

> gets captured; every ray above it escapes. That single number, `b_crit`,
> *is* the black hole's shadow — and it's worth being precise about what
> "shadow" even means here, because the simulator treats two things that
> get casually conflated everywhere else as genuinely different:

> The event horizon, at `r = r_s`, is a property of spacetime itself. The
> shadow is what a specific observer, standing at a specific distance,
> actually sees — and its angular size depends on where you're standing:

**`[EQUATION: sin(θ_shadow) = b_crit·√f(r₀) / r₀]`**

> Fly in close, and the shadow visibly swells well past the horizon's own
> naive angular size — a factor of over two at just ten Schwarzschild
> radii out, confirmed by an automated test, not eyeballed.

**[Screen: toggle the "Validate: Compare CPU vs GPU" button, or show the
test suite running in a terminal.]**

> And none of this is "trust me, it's physics." There are 201 automated
> tests that check this code against the actual closed-form analytical
> answers — the real deflection angle, the real ISCO velocity, the real
> critical impact parameter. If the math is wrong, a test fails. Every one
> currently passes.

---

## 3:00–3:45 — Gravitational lensing, deflection, and the Einstein ring

**[Screen: show a ray bending past the hole at a large impact parameter,
ideally with the bend angle visualized.]**

> Zoom out far enough, and this same equation reduces to the famous
> weak-field light-bending formula Eddington's 1919 eclipse expedition
> used to first confirm general relativity:

**`[EQUATION: α ≈ 4GM/(bc²) = 2 r_s/b    (leading order)]`**

> with a next-order correction, `(15π/16)(r_s/b)²`, that the simulator's
> own validation suite shows up as the actual residual between the full
> numerical answer and this leading-order formula — not integration
> error, the genuine next term in the expansion.

> There's no screen-space warp or distortion map anywhere in this
> pipeline. For every pixel, a ray is launched backward from the camera
> and integrated through curved spacetime; wherever it escapes, that's
> what gets sampled. Multiple images, the ring, all of it falls out of
> that loop — nobody told the renderer to draw a ring.

**[Screen: line up a point-light source directly behind the black hole
from the camera's perspective — show the Einstein ring forming.]**

> Put a point of light directly behind the hole, and symmetry demands a
> ring form at exactly the angle where `b ≈ b_crit`. This simulator
> doesn't draw that ring — it places the light source on the camera's own
> forward axis and lets dozens of independent rays, launched from
> completely different pixels around the shadow, all bend by coincidence
> onto the same point. The ring is what that coincidence looks like.

---

## 3:45–4:45 — The accretion disk: real temperature, real redshift, real beaming

**[Screen: show the accretion disk, toggle "relativistic effects" on and
off to show the color/brightness shift.]**

> The disk isn't colored by eye. It follows a real Shakura-Sunyaev (1973)
> temperature profile — the standard model for a thin, hot accretion
> disk:

**`[EQUATION: T(r) ∝ (r_in/r)^(3/4) · √(1 - √(r_in/r))]`**

> zero right at the inner edge, peaking a little beyond it, then falling
> off. A 10-solar-mass hole's disk runs around ten million kelvin — X-ray
> hot, like a real stellar black hole binary. TON 618, the default here,
> runs *cooler*, in the UV, because temperature scales as mass to the
> negative one-quarter power:

**`[EQUATION: T ∝ M^(-1/4)]`**

> Bigger black hole, colder disk. That's backwards from intuition, and
> it's correct.

> Orbital speed in that disk isn't arbitrary either. For a circular orbit
> in Schwarzschild spacetime, the *locally measured* speed a nearby static
> observer would clock is:

**`[EQUATION: v(r) = 1/√(r - r_s)    (sim units)]`**

> Plug in the innermost stable orbit, `r = 3r_s`, and you get `v = 0.5` —
> exactly half the speed of light, a standard textbook fact that this
> simulator reproduces as a numerical check, not a hardcoded value.

> And the disk's final color combines two real relativistic effects into
> one frequency-shift factor:

**`[EQUATION: g = √f(r) · 1/(γ(1 - v·cosψ))]`**

> gravitational redshift from climbing out of the well, multiplied by the
> ordinary special-relativistic Doppler shift of hot gas orbiting at a
> meaningful fraction of light speed. Feed that shifted temperature back
> into a Stefan-Boltzmann brightness law, and the approaching side of the
> disk brightens while the receding side dims — relativistic beaming,
> falling straight out of the redshift math with no separate "make one
> side brighter" rule bolted on.

---

## 4:45–6:15 — Kerr: spin, frame dragging, and Carter's constant

**[Screen: drag the spin slider up from 0. Show the shadow deforming,
getting asymmetric.]**

> Turn on spin, and the entire model switches: now it's solving the Kerr
> metric, parameterized by a dimensionless spin, `a*`, from 0 up to the
> realistic astrophysical limit of 0.998:

**`[EQUATION: ds² = -(1-2r/Σ)dt² - (4ar sin²θ/Σ)dt dφ + (Σ/Δ)dr² + Σdθ² + (A sin²θ/Σ)dφ²]`**
**`[EQUATION: Σ = r² + a²cos²θ,   Δ = r² - 2r + a²,   A = (r²+a²)² - a²Δsin²θ]`**

> The important new term is that `dt dφ` cross term — frame dragging:
> spacetime itself gets dragged around with the hole's rotation. That's
> why the shadow stops being a perfect circle. Light traveling *with* the
> spin and light traveling *against* it genuinely orbit at different
> radii.

> A general Kerr photon path isn't confined to one flat plane the way
> Schwarzschild's is, so there's no single clean angle to integrate
> against anymore. Instead the code uses Carter's 1968 separation of the
> Hamilton-Jacobi equation — the same formulation used by real
> astrophysical GR raytracers like GRay and GYOTO:

**`[EQUATION: Σ²(dr/dλ)² = R(r) = [E(r²+a²) - aLz]² - Δ[(Lz-aE)² + Q]]`**
**`[EQUATION: Σ²(dθ/dλ)² = Θ(θ) = Q - cos²θ[Lz²/sin²θ - a²E²]]`**

> Three conserved quantities — energy `E`, angular momentum `Lz`, and
> Carter's constant `Q` — turn one impossibly tangled 3D equation of
> motion into three independent, well-behaved one-variable ODEs. That
> `Q` is the whole reason a Kerr photon orbit can weave up and down in
> latitude instead of staying pinned to the equator — it has no
> Schwarzschild analog at all.

> The horizons, the ergosphere, and the innermost stable orbit all split
> apart under spin too, checked against the real literature values
> (Bardeen, Press & Teukolsky, 1972):

**`[EQUATION: r± = M ± √(M² - a²)     (outer / inner horizon)]`**

> Frame dragging drags the co-rotating innermost stable orbit all the way
> down to the horizon itself at maximum spin, and pushes the
> counter-rotating one out to `9M` — the same frame dragging, pulling in
> two different directions depending which way the orbit goes.

**[Screen: show the CUDA kernel file briefly — `cuda/geodesic_kernel.cu`
— then back to the live spinning shadow.]**

> All of that — the separated Hamilton-Jacobi integration, the full
> Kerr metric, run per-pixel — is accelerated on the GPU through CUDA, the
> exact same solver code as the CPU path, just compiled for the device
> and run in parallel across every pixel on screen simultaneously.

---

## 6:15–7:00 — Two black holes, and the trick that makes it possible

**[Screen: enable binary black hole mode, show two shadows orbiting each
other.]**

> Two black holes orbiting each other has no exact solution in general
> relativity at all — nobody has one, for anybody's computer. So this
> uses a well-known physicist's trick: each mass bends light with *twice*
> the plain Newtonian deflection,

**`[EQUATION: a_i = 2GM_i / r²     (toward each mass, summed over both)]`**

> Doubling the Newtonian force is exactly the move that recovers GR's real
> weak-field bending angle, `4GM/(bc²)`, from a cheap, easily-superposed
> `1/r²` law — the orbital motion of the two holes themselves is still
> exact Newtonian two-body mechanics, Kepler's third law in closed form;
> it's only the *light-bending* that's approximate, and it's labeled that
> way everywhere in the code, not quietly smoothed over.

---

## 7:00–7:45 — Flying there yourself: real gravity under an arcade flight model

**[Screen: switch to Jet mode, "Pilot Endurance." Fly toward the hole.]**

> You can also just fly a ship down there. The flight model is
> deliberately arcade for handling — direct rate-control steering, not
> torque and moment of inertia — but the gravity pulling on it is not
> simplified at all. It's the exact general-relativistic proper
> acceleration a stationary observer would have to fight to hold position
> at a given radius:

**`[EQUATION: a(r) = (GM/r²) / √(1 - r_s/r)]`**

> That denominator is everything: it reduces to ordinary `1/r²` gravity
> far away, and it diverges — goes to infinity — as `r` approaches the
> horizon. No amount of thrust can hold station arbitrarily close to a
> black hole. That's not a scripted "you die here" zone; it's this one
> equation doing exactly what it's supposed to do.

**[Screen: show the speed readout, mention the cap.]**

> There's also a hard speed cap, set as a fraction of light speed —
> because without one, flying near a supermassive hole like TON 618
> breaks the numbers in the other direction: one simulation "unit" of
> distance there is almost a hundredth of a light-year, so ordinary
> game-speed thrust would be thousands of times faster than light. The cap
> keeps the physics honest even when the scale gets absurd.

---

## 7:45–8:45 — Tidal disruption: real differential gravity, real spring lattice

**[Screen: fly a star or planet into the black hole, watch it stretch and
shatter.]**

> Stars and planets aren't scripted to stretch and break — they're built
> from dozens of individual particles, each one independently free-falling
> under that exact same divergent gravity equation. "Spaghettification" is
> what happens when the near-side particles get pulled measurably harder
> than the far-side ones; nobody animates the stretch, the stretch is the
> gravity gradient made visible.

> Planets add one more piece: a spring lattice connecting each particle to
> its neighbors, resisting that stretch. A spring breaks permanently once
> its strain crosses the material's own strength threshold, and whatever's
> left over becomes a tracked fragment with its own real mass and
> velocity — no momentum is invented when something breaks apart. Two
> textbook formulas are shown on screen for comparison — the Newtonian
> tidal acceleration and the classical Roche limit —

**`[EQUATION: Δa_tidal ≈ 2GM_bh·R_p/r³        r_Roche ≈ 2.44 R_p (M_bh/M_p)^(1/3)]`**

> but neither one actually *triggers* the destruction. What breaks is
> whatever the spring network and the real stresses along the trajectory
> actually do — the formulas are there to check your intuition against,
> not to drive the simulation.

---

## 8:45–9:45 — A real universe outside the black hole

**[Screen: press "M," open the Star Map, scroll the list.]**

> And it's not just one black hole. There's a real, bounded catalog of
> actual stars, black holes, and galaxies at their real measured distances
> and masses. Press M, pick Sagittarius A*, the actual black hole at the
> center of our own galaxy, and the scene re-renders at *its* real mass —
> four million Suns instead of sixty billion.

**[Screen: travel to a star, e.g. Proxima Centauri. Show the Star System
View with the orbiting exoplanet.]**

> Travel to a star instead, and you get a real exoplanet — Proxima
> Centauri b — orbiting at a period derived live from Kepler's third law,
> using the star's actual catalog mass:

**`[EQUATION: P[years] = √( a[AU]³ / M[M☉] ) )]`**

> not a hardcoded number. The two independently have to agree, and the
> test suite checks that they do, within the rounding of the published
> catalog figures — Proxima b comes out to 11.26 days against a real
> ~11.19.

**[Screen: travel to the Alpha Centauri triple system. Let it visibly
integrate — show the tight binary spinning while the third star barely
moves.]**

> This one's my favorite: Alpha Centauri isn't one star, it's three. Two
> orbit each other tightly; a third, Proxima, orbits the pair from
> absurdly far away. Three mutually gravitating bodies have *no* general
> closed-form solution — that's literally "the three-body problem" — so
> this isn't an animation. It's a live Newtonian N-body integration,

**`[EQUATION: Σ mᵢrᵢ = 0   (barycenter),    a_i = Σⱼ≠ᵢ G mⱼ (rⱼ-rᵢ)/|rⱼ-rᵢ|³]`**

> solving the same unsolved problem real astrophysics deals with, every
> single frame.

---

## 9:45–10:15 — Why this, not a shader

**[Screen: pull back to the default TON 618 view one more time.]**

> None of this is faked for the shot. The lensing is a real integrated
> geodesic — Schwarzschild's `d²u/dφ²` equation, or Kerr's separated
> Hamilton-Jacobi system, solved frame by frame. The disk temperature is a
> real Shakura-Sunyaev profile. The exoplanet period is really derived
> from Kepler's law, not looked up. The three-body chaos is really
> chaotic. Where something *is* an approximation — the binary black hole
> lensing's doubled-Newtonian trick, the procedural star textures, the
> floored flight speed near a supermassive hole — it's labeled as one,
> right in the code and right in the docs, not quietly smoothed over.

---

## 10:15–10:30 — Close

**[Screen: back to the shadow, slow zoom out.]**

> It started as a Schwarzschild ray tracer. It's grown into a small,
> honestly-scoped universe you can fly through, written in C++ and
> accelerated with CUDA. All of it's real physics, checked against 201
> automated tests, and all of it's open — link's in the description.

**[End card: repo URL, GitHub stars/fork callout.]**

---

## B-roll from existing project assets

Real files already in this repo, not yet called out in the shot list
above. None of these need to be staged — they either exist on disk or are
UI toggles already in the app.

- **`docs/images/accretion_disk.png`** — an actual earlier in-app
  screenshot (Stage 1+2 controls panel visible in-frame), warm disk glow
  wrapping the shadow. Usable as a quick "early build" nostalgia cut near
  the close (9:45–10:15), or as a still insert if a live capture of that
  exact framing is hard to reproduce.
- **`docs/images/einstein_ring.png`** — not the pretty final render: a
  stark black-and-white debug/validation image showing dozens of discrete
  ray arcs forming the ring. Cut to this *specifically* at 3:00–3:45 right
  after "nobody told the renderer to draw a ring" — seeing the raw
  per-ray debug output is more convincing than the final shaded frame.
- **`resources/jet/endurance.glb` + `endurance_basecolor.png`** — the
  ship model and its texture map. A close-up orbit around the loaded
  model, or a 1-2s flash of the basecolor texture itself, grounds the
  7:00–7:45 jet section's "not scripted" claims in an actual asset.
- **`scenes/my_scene.json` / `my_scene2.json`** — real saved presets with
  tuned values (e.g. `diskGlareIntensity: 1.12`, a 66-billion-solar-mass
  hole). Loading one on screen during 0:30–1:00 is a quick, true
  demonstration that scene state is real save/load, not one hardcoded
  look.
- **`tests/*.cpp`** (18 files: `test_schwarzschild`, `test_kerr`,
  `test_deflection`, `test_shadow`, `test_tidal_flare`, `test_multibody`,
  `test_universe`, etc.) — for the "201 automated tests" beat at
  2:15–3:00, a quick scroll through this file list (or the test binary's
  own output) is stronger than a bare pass/fail count: it shows the tests
  are organized by physics domain, not one undifferentiated blob.
- **`src/Universe/StarCatalog.h` / `ExoplanetCatalog.h` /
  `MultiStarSystem.h`** — the header comment explicitly states the
  catalog is real published data, "not procedurally generated." A 2-3s
  scroll through this file during 8:45–9:45 backs up "a real, bounded
  catalog" with the actual source, not just the UI result.

## Additional features worth capturing (not currently in the timed script)

These exist and work in the app today but aren't in the 0:00–10:30 cut
above — worth a short dedicated beat, or cutting one of the existing
"textbook" sections short to make room, per a script doctor's call.

- **Photon Inspector** (`App.cpp`, "Photon Inspector" panel) — traces one
  precisely-targeted ray and shows its exact path. Visually, this is the
  single ray version of the shadow/lensing explanation in 2:15–3:00 and
  3:00–3:45 — showing one ray being inspected, bent, and its exit angle
  read off, makes "a ray is launched backward from the camera and
  integrated through curved spacetime" (currently just narrated) into
  something the viewer watches happen.
- **Scientific Plots panel** (`App.cpp`, deflection-angle-vs-impact-
  parameter sweep) — a live-generated plot of deflection angle against
  impact parameter. This is the actual graph behind the "residual between
  the full numerical answer and the leading-order formula" claim at
  3:00–3:45 — showing the plot itself, not just stating the claim, is a
  strict upgrade to that beat.
- **Spacetime curvature grid visualization** — an optional overlay that
  renders the curved grid itself rather than just its lensing effect on
  light. Strong fit for 1:00–2:15 right after the metric is introduced:
  seeing the grid warp around the hole makes "spacetime itself is
  described by one equation" concrete before the ray-tracing explanation
  starts.
- **Simple vs. Advanced mode toggle** — the app defaults to a "Simple"
  mode with four one-click presets (Black Hole, Einstein Ring, Accretion
  Disk, Newtonian vs. Einstein) before Advanced mode exposes every
  scientific control. A quick cut through the four Simple presets near
  0:00–0:30 or in the close (9:45–10:15) would show the app also works
  for a non-technical viewer, not just the equation-heavy framing this
  script leans into.
- **Planetary self-gravity reaccumulation** — per HANDOFF.md, broken-off
  debris fragments aren't just discarded; they're tracked bodies that can
  mutually re-accrete under their own gravity. Worth a few extra seconds
  in the 7:45–8:45 tidal disruption section if a capture shows fragments
  visibly clumping back together post-breakup, since it's a further
  "nothing is scripted" proof point beyond the initial shatter.
- **Float-vs-double GPU performance fact** (HANDOFF.md) — switching the
  CUDA kernel from double to float precision was a measured ~9.5x speedup
  (105 fps vs. 11 fps at identical settings) on consumer hardware. A
  possible stat-card overlay for the CUDA mention at 0:00–0:30 or
  4:45–6:15, if the video wants one concrete performance number rather
  than just "accelerated on the GPU."

---

## Production notes (not narrated)

- **Total word count**: ~1,900 words at ~140 wpm ≈ 13:30 of pure
  narration before trimming. Every equation overlay also costs 2-3
  seconds of silent hold time for it to register on screen — budget
  roughly 10:30 all-in with pacing, not a strict 13:30.
- **If a shorter cut is needed**: the original 5-minute version (no
  equation overlays, physics explained in words only) is the fallback —
  see git history for that script, or cut the Kerr/Carter's-constant
  section (4:45–6:15) and the tidal disruption section (7:45–8:45)
  entirely; those two are the most "textbook" sections and the ones
  viewers are likeliest to tolerate losing.
- Every physics claim above, and every equation overlay, is backed by an
  actual file/test in this repo — see `docs/physics.md` §§2-19 and
  `docs/validation.md` for the exact derivations/numbers if a script
  doctor wants to double-check a line (or an equation's exact form)
  before it's recorded.
- Equation overlays should be typeset (LaTeX/KaTeX render or a clean
  slide), not read verbatim as code-style ASCII — the `[EQUATION: ...]`
  blocks above are the content to typeset, not the final on-screen
  formatting.
- The screen-recording cues assume a smooth 60fps capture of the live
  app, not static screenshots — several beats (spin slider, Alpha
  Centauri integrating, the Kerr shadow deforming) only land if the
  motion is actually visible.
