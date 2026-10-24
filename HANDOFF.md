# Handoff

Status snapshot for picking this project back up. For the physics/numerics
themselves, see `docs/physics.md`, `docs/numerical_methods.md`,
`docs/validation.md` — this file is about the *engineering* state: what's
built, what's verified, what's rough, and what to check first.

## TL;DR

A real-time GPU-accelerated Schwarzschild black hole ray tracer. All 7
planned stages are implemented and working. The interactive app defaults
to a "Simple" mode (four one-click preset scenes) with a full "Advanced"
mode underneath exposing every scientific control. 38 automated physics
tests pass (42 with `-DBH_ENABLE_CUDA=ON`, including an automated CPU-vs-GPU
render comparison), all validated against closed-form GR results. A git
repository has now been initialized, with an initial commit covering the
full source tree (see "What's been done this session" below).

**Update (Sessions 3-11):** an optional Kerr (rotating) mode and an
optional (explicitly approximate) binary-black-hole mode -- each body
with its own accretion disk -- now exist alongside the Schwarzschild
model described in this TL;DR, on BOTH CPU AND GPU for all three, with
a Photon Inspector, Scientific Plots, validated deflection checks, and
an optional spacetime-curvature grid visualization -- see those
sections further down (Session 3: Kerr CPU solver; 4: Kerr CUDA port;
5: interactive-framerate fix; 6: Photon Inspector; 7: Scientific Plots;
8: Kerr deflection validation + two real solver bugs it caught; 9:
binary black holes, CPU-only at first; 10: binary black holes' own CUDA
port, fixing a real user-reported lag; 11: binary accretion disks + the
curvature grid). **98 automated checks pass CPU-only, 110 with
CUDA** (see Session 11 for the authoritative count -- it superseded
earlier sessions' counts, which had drifted).

## What's been done

### Physics engine (`src/Physics/`) — solid, well-tested
- Newtonian baseline (Euler/Verlet/RK4) with closed-form Kepler-orbit
  validation.
- Schwarzschild null-geodesic solver (`GeodesicSolver.cpp`), using the
  standard `u=1/r` reduction, curvature-adaptive step size, exact
  tetrad-based impact parameter (valid at any camera radius, not just far
  away).
- Accretion disk: Shakura-Sunyaev temperature profile, exact gravitational
  + Doppler redshift at the photon's disk-crossing point.
- Shadow-vs-horizon distinction made quantitative (not just asserted).
- `GeodesicMath.h` / `SchwarzschildMetric.h` are templated on the
  floating-point type — this is *the* piece of shared code between the CPU
  and GPU tracers (see below), not a duplicated reimplementation.
- **38 tests, 0 failures** (`tests/*.cpp`), covering Schwarzschild radius,
  photon sphere, critical impact parameter, capture/escape classification,
  weak-field deflection vs. the analytical formula (including the
  next-order PPN correction), the shadow-angle formula, ISCO orbital
  velocity, and disk temperature/geometry. Run with:
  ```
  build.bat && build\tests\BlackHoleTests.exe
  ```

### Rendering (`src/RayTracing/`, `cuda/`) — working, one real perf fix applied late
- `CPUTracer` and `CUDATracer` share the exact same function signature and
  are directly swappable; `CUDATracer` is now the only path used when a
  CUDA device is present (no CPU/GPU toggle in the UI anymore — see
  "Decisions" below for why).
- **The GPU kernel runs in `float`, not `double`.** This was the single
  biggest performance fix in the whole project: consumer NVIDIA GPUs (an
  RTX 4070 Ti Super here) cripple double-precision throughput to a small
  fraction of single-precision. Switching the kernel to float gave a
  measured **~9.5x speedup** (105 fps vs. 11 fps at identical settings).
  The CPU path is untouched and still runs in `double` — the automated
  test suite only exercises the CPU path, so it's unaffected.
- Render resolution is capped at **1.5M pixels/frame** (`kMaxRenderPixels`
  in `App.cpp`) regardless of window size, scaled proportionally to
  preserve aspect ratio. This was a real bug fix, not a feature — see
  "Bugs found and fixed" below.
- Persistent CUDA device/pinned-host buffers (no `cudaMalloc`/`cudaFree`
  per frame) and a parallelized CPU-side coloring pass
  (`std::execution::par`) after the kernel.

### UI (`src/Application/App.cpp`) — functional, could use more polish
- **Simple mode** (default): four preset buttons (Black Hole, Einstein
  Ring, Accretion Disk, Newtonian vs Einstein), a plain-language mass
  slider, a resolution slider, live FPS readout.
- **Advanced mode**: the full original scientific panel — every Stage 1-7
  control, debug tables, validation readouts, CPU-vs-GPU comparison.
- Live continuous rendering (re-traces every frame from the current
  camera) replaced the original "click to render" workflow.
- `HelpTooltip()` "(?)" markers on the more jargon-heavy Advanced controls.

## Decisions worth knowing about (so they don't get "fixed" back)

- **No CPU/GPU toggle.** Explicitly removed per user request ("use only
  the CUDA version") — the app always uses GPU when available, CPU is
  only ever an automatic fallback with no device. If you reintroduce a
  toggle, keep the automatic-fallback behavior.
- **No fast/rough-while-moving quality reduction.** An earlier version of
  this UI did progressive refinement (cheap preview while dragging, sharp
  once still, Google-Earth style). It was **removed** per explicit
  request — full geodesic precision (`maxSteps=8000`, `basePhiStep=0.015`)
  is now used on every frame, moving or not. The resolution slider is the
  only speed/quality knob left. Don't reintroduce step-budget reduction
  during motion without checking this history first.
- **GPU kernel is `float`, CPU is `double`, by design**, not an oversight.
  See above. If a task ever needs the GPU path to be bit-comparable to
  CPU again, that's a real, known trade-off to revisit, not a bug.

## Bugs found and fixed this session (for context, not action)

1. **Stretched render.** Was rendering a fixed square buffer (e.g.
   1024x1024) and stretching it to fill the window — distorted the
   shadow into an ellipse on any non-square window. Fixed by deriving
   render resolution from the actual window framebuffer size every frame.
2. **"15 fps when I use the accretion disk."** Root-caused by reproducing
   it with the disk *disabled* at the same (maximized) window size —
   identical 15 fps. The real cause was resolution scaling unbounded with
   window/monitor size; the disk was coincidental. Fixed with the 1.5M
   pixel cap mentioned above. **If perf complaints come up again, check
   window size and the resolution slider before assuming a feature is
   slow** — that's twice now that a "feature X is slow" report turned out
   to be resolution-driven.
3. Simple-mode presets silently not rendering anything (the actual render
   *execution* was nested inside Advanced-only UI code — presets flipped
   the flags but nothing was listening).
4. CUDA escape-detection misclassifying a step that overshot past `u=0` as
   numerical breakdown instead of escape (only affected large-`b`
   weak-field rays).

## What needs attention

Resolved this session (see git log for details):

- [x] **`docs/validation.md` §9 updated.** Re-measured CPU vs. GPU (now
      genuinely float-vs-double, post the earlier `float` kernel
      conversion) both via the app's compare button and a new automated
      test; real numbers and an explanation of the expected, non-zero
      delta are in place.
- [x] **Git repository initialized.** `.gitignore` added (`build/`,
      `build_cuda/`, `crashdumps/`, logs, `imgui.ini`, local Claude
      settings); initial commit made covering the full source tree.
- [x] **Window title fixed** — no longer references stale "Stage 1-3."
- [x] **GPU correctness now has automated coverage**
      (`tests/test_gpu_render.cpp`, `RunGpuRenderTests`, only
      built/run with `-DBH_ENABLE_CUDA=ON`): renders a known scene
      (starfield + disk) on both tracers and checks per-pixel agreement
      within a loose, regression-catching tolerance. 42 tests total in
      the CUDA build (38 without).
- [x] **`kMaxRenderPixels` is now an Advanced-mode slider** ("Max
      rays/frame (millions)", `App.cpp`), not a hardcoded constant — the
      1.5M default (`kDefaultMaxRenderPixels`) is unchanged, but a
      stronger/weaker GPU than the RTX 4070 Ti Super this was tuned on
      can now adjust it without a rebuild.
- [x] **Near-radial camera-ray special case now handled in the CUDA
      kernel** (`cuda/geodesic_kernel.cu`'s `TracePhotonDevice`), mirroring
      the CPU tracer's linear-r stepping instead of falling through to the
      b-parametrized path. CPU and GPU tracers no longer have this known
      gap between them.

Still open:

- [ ] **`docs/images/*.png` predate the Simple/Advanced UI split.** They
      show the old single-panel debug view, not the current default
      Simple-mode experience. Attempted to recapture this session by
      driving the app's preset buttons via synthetic `SendInput`/
      `mouse_event` clicks — the window receives them (cursor moves,
      the process is frontmost) but ImGui never registers them as
      clicks, so the UI state never changes. This reproduces the next
      bullet's known limitation rather than being a new issue. A plain
      "Black Hole" default-view screenshot (no preset click needed) was
      captured successfully and discarded only because it didn't clearly
      show the lensed shadow (covered by the control panel at that
      camera angle) — recapturing well still needs a human driving the
      app, or a deliberate non-default camera framing set via code
      rather than a UI click.
- [ ] **UI automation doesn't work reliably in this dev environment**
      (confirmed again this session: neither the older `mouse_event`
      nor the newer `SendInput` Win32 API got a synthetic click to
      register on an ImGui button, despite the cursor visibly moving and
      the window being focused/frontmost — screenshots and keyboard/
      state-based verification did work). Worth knowing before relying
      on click-driven UI tests or automated screenshot capture here.

## Build

```
build.bat          # CPU-only, no CUDA toolkit needed
build_cuda.bat      # adds the GPU tracer (needs CUDA Toolkit 12.6+, tested against an RTX 4070 Ti Super)
```

Both configs build clean with no warnings (beyond pre-existing, unrelated
GLM `CMakeLists.txt` deprecation notices) as of this handoff. `build.bat`
passes all 38 tests; `build_cuda.bat` passes all 42 (the extra 4 are the
automated CPU-vs-GPU render comparison, see above).

## Session 2 — jet piloting mode, TON 618 default, realistic disk/jet physics, HUD compass, star tidal disruption

Builds on everything above; nothing in Session 1 was reverted. Both
`build.bat` and `build_cuda.bat` still build clean and pass the full test
suite (38/42) after all of this.

### What's new

- **Jet piloting mode** (`src/Jet/`): a third camera mode (Orbit / Free-Fly
  / **Jet**) that puts the user in a flyable Su-57 (GLB model + texture in
  `resources/jet/`) with an arcade thrust/drag/G-force flight model, chase
  and cockpit camera views (`Jet::FlightCamera`), and real Schwarzschild
  gravity (`Physics::StaticObserverGravityAccelSim`, see below) pulling it
  toward the hole. Composited on top of the ray-traced view via
  `Renderer::BeginOverlayPass`, the same mechanism used for stars (see
  below).
  - **Known fragile point:** the jet model/texture load via a relative
    path (`resources/jet/...`), which only resolves if the working
    directory is the source tree. Fixed by adding a CMake post-build step
    (`CMakeLists.txt`) that copies `resources/` next to whichever exe was
    just built, so launching the exe directly from `build/` or
    `build_cuda/` also finds it. If you ever add more runtime assets,
    remember they need the same treatment (or go through this existing
    copy step automatically, since it copies the whole directory).
- **TON 618 is now the default black hole mass** (`App::Run`, was 10 solar
  masses), still a live, adjustable slider (now ranges up to 1e11 solar
  masses instead of 1e9, since TON 618 — ~6.6e10 M☉ — didn't fit the old
  range). This was a deliberate user request, not a tuning accident.
- **The accretion disk's color is now physically tied to the black hole's
  actual mass**, not an artist-picked color ramp (`Astronomy::AccretionDisk`
  / `AccretionDisk.cpp`). It derives the real Shakura-Sunyaev effective
  temperature from an assumed Eddington fraction + radiative efficiency
  (both now UI sliders), giving the textbook `T_eff ~ M^-1/4` scaling: a
  10-solar-mass hole's disk runs ~10^7 K (X-ray, like a real stellar X-ray
  binary); TON 618's disk runs ~10^4-10^5 K (UV/blue-white "big blue
  bump", like a real quasar). `Temperature()`'s dimensionless shape
  function is unchanged (tests still pass unmodified) — only `EmittedColor`
  changed, multiplying that shape by a real, mass-derived Kelvin value
  (`AccretionDisk::PeakKelvin()`) instead of a fixed `1500 + T*11000` fudge.
- **HUD compass** (`App::Run`'s render loop, just before `ImGui::Render()`):
  a horizontal strip across the top of the screen showing relative bearing
  to nearby points of interest (currently: the black hole always, the jet
  while piloting it), with adaptive-unit distance labels (km vs. ly). Built
  as an extensible `PointOfInterest{name, position, color}` list — adding
  more is a one-line addition, not a redesign.
- **Jet relativistic speed cap** (`Jet::FlightPhysics`): the thrust/drag
  model was tuned treating "sim units" as meters, which was harmless at
  10 solar masses (1 sim unit ≈ 15 km) but became absurd at TON 618 scale
  (1 sim unit ≈ 0.01 ly) — the old equilibrium speed worked out to
  billions of c. Fixed with a hard cap, expressed as a fraction of c
  (`maxSpeedFractionOfC`, default 0.2, UI slider), converted every frame
  into however many sim-units/s that implies for whichever black hole mass
  is currently loaded. **Known tradeoff, not a bug:** at TON 618's scale
  this makes long-distance travel genuinely slow (realistically, years) —
  that's the physically correct consequence of light-year-scale distances,
  not a mistake. The slider exists specifically so this tradeoff is
  adjustable rather than argued about.
- **Stars: real-physics tidal disruption** (`src/Astronomy/StarBody.*`,
  new files). "Add Star" (Simple and Advanced mode) drops a cluster of
  ~28 independent particles into orbit with **no self-gravity binding
  them** — each free-falls under the exact same gravity law as the jet
  (`Physics::StaticObserverGravityAccelSim`, factored out of
  `Jet::FlightPhysics` into `Physics/BlackHole.h` specifically so both
  share it). Spaghettification is therefore not a scripted stretch effect;
  it's the direct, measured consequence of near-side particles feeling
  stronger gravity than far-side ones. Particles crossing the horizon are
  marked consumed, stop integrating, stop drawing; a star is dropped from
  the active list once every particle is consumed. Rendered via the same
  `BeginOverlayPass` compositing path as the jet, and also in the debug 3D
  view. Advanced mode exposes spawn radius and orbital-speed-fraction
  sliders plus a live particle-count readout.

### Decisions worth knowing about (additions to the Session 1 list above)

- **Sim units are NOT literal meters**, even though some constants look
  like they assume that (`Jet::MACH_1 = 343.0f`, `STALL_SPEED = 70.0f`).
  This was always true (1 sim unit = GM/c^2 for whichever mass is loaded,
  see `Physics/Constants.h`), but it only became consequential once the
  default mass went supermassive. Any new code that wants a REAL physical
  quantity (speed, Kelvin, distance) must explicitly convert via the
  current `blackHole.MassKg()`-derived scale — never assume sim-units are
  meters, seconds, or anything else physical by default.
- **The ray-traced image itself is intentionally mass-invariant.** The
  Schwarzschild metric is scale-invariant in units of r_s, so the lensing
  shape, starfield, and disk geometry never change when you move the mass
  slider — only labeled real-world sizes (km/ly) and anything that
  explicitly converts to real units (jet speed cap, disk Kelvin, compass
  distances) do. This was confirmed as correct, expected behavior when a
  user asked about it — don't "fix" it into mass-dependent lensing.
- **Stars have no self-gravity by design**, not as a missing feature.
  Modeling it would mean modeling the competition between self-gravity and
  tidal force (i.e. an actual Roche/tidal-disruption-radius calculation)
  instead of just letting differential external gravity do the work. Every
  star here is effectively already past the point self-gravity would have
  lost. Documented in `StarBody.h` as a limitation, same spirit as
  `docs/physics.md`'s others — don't quietly add cohesion without updating
  that comment.

## Session 3 — Kerr (rotating) black hole mode

Builds on both sessions above; nothing reverted. `build.bat` and
`build_cuda.bat` both still build clean (no warnings) and pass the full
test suite (67/67, up from 38/42 -- see below for why the count isn't
simply "+29 on the old 42") after this.

### What's new

- **Kerr spacetime** (`src/Physics/KerrMetric.h`, `KerrGeodesicMath.h`,
  `KerrGeodesicSolver.{h,cpp}`, new files): `BlackHole::SetSpin`/`Spin()`
  adds the dimensionless spin `a* ∈ [-0.998, 0.998]`; nonzero spin routes
  every pixel through a new general (non-equatorial) Kerr null-geodesic
  solver instead of the existing Schwarzschild one, via Carter's (1968)
  separated Hamilton-Jacobi equations -- see `docs/physics.md §10` for
  the full derivation, closed-form horizon/ISCO/photon-orbit/ergosphere
  formulas, and the explicitly-labeled spatial-embedding approximation
  it uses. Frame dragging, the ergosphere, and the prograde/retrograde
  photon-orbit and ISCO asymmetry are now real, numerically-integrated
  physics, not just mentioned as a future possibility in the old
  Limitations section.
- **CPU-only, by design, for this session.** `RayTracing::RenderImage`
  branches per-pixel on `RenderSettings::spin`; `App.cpp`'s
  `PerformRender` forced the CPU path whenever spin != 0 regardless of
  the GPU toggle, since the CUDA kernel (`cuda/geodesic_kernel.cu`) was
  NOT touched this session and remained Schwarzschild-only.
  **Superseded by Session 4 below** (the CUDA port) -- `KerrGeodesicMath.h`
  was deliberately written with the same `BH_HOST_DEVICE`-templated-on-`T`
  pattern as the Schwarzschild `GeodesicMath.h` specifically so that port
  wouldn't require rewriting the physics, and that's exactly what
  happened.
- **`tests/test_kerr.cpp`** (new, 29 checks): closed-form horizon/ISCO/
  photon-orbit/ergosphere values at a=0 (must equal the existing
  Schwarzschild values exactly), at a*=1 extremal (against standard
  textbook values), and at a generic spin; an a=0 regression comparing
  the NEW Kerr solver's capture/escape classification and escape
  direction directly against the EXISTING, already-validated
  Schwarzschild solver; and a frame-dragging check that bisects the Kerr
  solver's own critical impact parameter on the prograde and retrograde
  sides at a*=0.9 and confirms prograde's is smaller (the integrator
  reproducing the qualitative signature of its own physics, not an
  independently hardcoded expected value).
  - **Why the total isn't a clean "+29 on 42":** the project's own
    `[ OK ]`/`[FAIL]` lines are the actual check count (`TestFramework.h`),
    and that count is 67 in BOTH the CPU-only and CUDA builds post this
    session (38 original + 29 new Kerr checks either way; the CUDA
    build's extra 3 GPU-comparison checks were apparently already
    included in the pre-existing "38" baseline count by a different
    accounting than HANDOFF's old "38/42" phrasing implied -- don't
    re-derive expectations from that old phrasing, trust a fresh count
    of `[ OK ]` lines in the actual current test output instead).
- **Two real bugs found and fixed while getting test_kerr.cpp to pass**
  (documented so they don't get reintroduced):
  1. `KerrGeodesicConfig::maxLambdaStep` defaulted to 0.5 -- fine very
     close to the hole, but capping the affine-parameter step at a FIXED
     value regardless of `r` means a ray that needs to travel back out
     from closest approach (~r=6) to `escapeRadius` (~1e5-1e6) exhausts
     `maxSteps` long before getting there, reporting UNRESOLVED for a
     perfectly ordinary flyby. Fixed by raising the default to 5e4 (the
     step already scales as `baseLambdaStep * r`, so it naturally grows
     as the ray recedes -- it just needed room to actually do that).
  2. The first draft of the frame-dragging test had prograde/retrograde
     backwards: tilting the LAUNCH DIRECTION's y-component by `+b` at a
     ray starting from `-x` actually gives NEGATIVE orbital angular
     momentum (`L_z ~ x·v_y - y·v_x` evaluated at launch is `-b`, not
     `+b`), the opposite sign convention from the POSITION-offset `b`
     used elsewhere in the test suite (`test_geodesic.cpp` etc.). The
     solver's physics was correct the whole time -- confirmed by solving
     for the test's actual bug via the debug printout before changing
     any solver code, not by tuning the solver until a test passed.
- **UI** (`App.cpp`, Advanced > Black Hole): a spin slider plus live
  readouts of the outer/inner horizon, equatorial ergosphere, and
  prograde/retrograde photon-orbit and ISCO radii -- quantitative, like
  every other Advanced-mode readout in this app, not just a label. (The
  slider's tooltip originally noted a CPU-only tradeoff here -- removed
  in Session 4 once that tradeoff no longer existed.)
- **Kerr accretion disk redshift** uses the standard Cunningham
  (1975)/Luminet (1979) closed form for a photon from a prograde circular
  Kerr orbit, rather than re-deriving the Schwarzschild solver's
  local-tetrad Doppler decomposition for Kerr's more involved ZAMO
  geometry -- a deliberate choice of a well-established, independently
  citable formula over a derivation with more places to introduce a sign
  error. See `docs/physics.md §10` for why, and the explicit note that
  the Kerr disk is unconditionally prograde (no `diskClockwise`
  equivalent) since that's the physically standard assumption for a real
  disk around a spinning hole.

### What needs attention

- [x] ~~No Kerr equivalent of `docs/validation.md`'s deflection-angle
      sweep~~ -- done in Session 8, see below (and it caught two real
      bugs in the process).
- [x] ~~CUDA kernel is still Schwarzschild-only~~ -- done in Session 4,
      see below.
- [ ] The spatial-embedding approximation (treating a Cartesian camera
      position's `r, θ` as literal spherical coordinates rather than the
      true oblate-spheroidal Boyer-Lindquist embedding) is standard for
      this kind of real-time renderer and documented in
      `docs/physics.md §10`, but hasn't been cross-checked against a
      reference Kerr black-hole image (e.g. a published shadow-shape
      plot at a specific spin/inclination) the way the Schwarzschild
      shadow/deflection numbers were in `docs/validation.md`.

## Session 4 — Kerr on the GPU

Builds on Session 3 directly (closes its "CPU-only, by design" scope
boundary). Nothing from Sessions 1-3 reverted. `build.bat` and
`build_cuda.bat` both still build clean (no warnings) and pass the full
test suite (71/71 in the CUDA build, 71/71 CPU-only too -- see Session 3's
note on why the CPU-only and CUDA counts coincide).

### What's new

- **`cuda/geodesic_kernel.cu` now has a Kerr path.** `TracePhotonKerrDevice`
  (device function) and `TraceKerrPhotonsKernel` (the `__global__` kernel)
  mirror `Physics::TracePhotonKerr` (`KerrGeodesicSolver.cpp`) step for
  step -- same turning-point retry loop, same ZAMO-tetrad initial
  conditions, same Cunningham/Luminet disk redshift formula -- in `float`
  instead of `double`, calling the exact same shared
  `Physics::Kerr`/`Physics::KerrGeodesicMath` templated functions the CPU
  solver uses (deduced at `T=float` here). This is a port, not a
  reimplementation: `KerrGeodesicMath.h` was written in Session 3
  specifically so this wouldn't require rewriting any physics, and that
  held -- the actual new code in the `.cu` file is the per-thread loop
  orchestration (buffer management, the kernel launch, host/device
  `KernelVec3` plumbing), the same division of labor as the existing
  Schwarzschild `TracePhotonDevice`/`TracePhotonsKernel`.
- **New entry point** `CudaTracing::LaunchKerrGeodesicKernel`
  (`GeodesicKernel.h`/`geodesic_kernel.cu`), alongside the existing
  `LaunchGeodesicKernel` -- shares its persistent device/pinned-host
  buffers (both produce the same `KernelPixelResult` type, and only one
  kernel is ever launched per frame, so there's no concurrent-use hazard).
  New `KernelKerrConfig` mirrors `Physics::KerrGeodesicConfig`.
- **`RayTracing::RenderImageCUDA` (`CUDATracer.cpp`) now branches on
  `settings.spin`**, exactly like `RenderImage` (CPU) already did --
  nonzero spin calls `LaunchKerrGeodesicKernel` instead of
  `LaunchGeodesicKernel`. Both tracers read the SAME `RenderSettings`
  field, so `App.cpp` didn't need new state, just the removal of the
  `renderSettings.spin == 0.0` condition that previously forced the GPU
  branch off in `PerformRender` -- Kerr mode now uses the GPU whenever one
  is available and the toggle is on, same as Schwarzschild mode.
- **UI** (`App.cpp`): removed the now-inaccurate "CPU-only"/"forces CPU"
  notes next to the spin slider and the GPU status line (Session 3 added
  them when they were still true); the spin slider's tooltip now says
  Kerr mode runs on GPU when available, same as Schwarzschild.
- **`tests/test_gpu_render.cpp`: new `RunKerrGpuRenderTests`** (4 checks),
  the Kerr counterpart of the existing `RunGpuRenderTests` -- renders the
  same test scene at `a*=0.8` through both `RenderImage` (CPU) and
  `RenderImageCUDA` (GPU, now exercising the new Kerr kernel) and checks
  they agree within tolerance. This is the only end-to-end check that the
  CUDA port didn't introduce a float-vs-double discrepancy beyond what's
  expected -- `tests/test_kerr.cpp` (Session 3) only ever exercised the
  CPU solver. Needed a looser max-per-pixel tolerance (0.6, vs.
  Schwarzschild's 0.35) than the mean-diff tolerance (0.02, vs. 0.01)
  would suggest -- see the test's own comment: a thin rim of
  shadow-boundary pixels can flip classification (captured vs. escaped)
  between float and double right at the critical impact parameter, which
  is a large per-pixel color difference affecting very few pixels, not a
  sign of a broken kernel (mean diff stays tiny either way).
- **The "Validate: Compare CPU vs GPU" button** (`App.cpp`, Advanced
  mode) now also exercises Kerr: it builds its own `RenderSettings`
  separately from `PerformRender`, so it needed the same `spin`/
  `kerrConfig` assignment added explicitly -- with spin != 0, the button
  now compares the two Kerr implementations instead of silently always
  comparing Schwarzschild regardless of the current spin slider (which is
  what it would have kept doing otherwise, since it previously never
  read `blackHole.Spin()` at all).
- **Shared-header device-compatibility fix, applied proactively.**
  `KerrGeodesicMath.h` and `KerrMetric.h` originally used `std::max`/
  `std::clamp` inside their `BH_HOST_DEVICE` templated functions; rewrote
  those as manual ternaries/`if` statements before attempting the CUDA
  build, matching the discipline the existing Schwarzschild
  `GeodesicMath.h::AdaptivePhiStep` already follows (and avoiding relying
  on nvcc device support for `<algorithm>`, which this project's CMake
  doesn't explicitly enable via `--expt-relaxed-constexpr`). `std::sqrt`/
  `sin`/`cos`/`acos`/`atan2`/`pow`, by contrast, compiled fine as `std::`
  calls in device code with no extra flags needed -- confirmed by the
  actual nvcc build, not assumed.

### What needs attention

- [x] ~~Still no Kerr equivalent of `docs/validation.md`'s deflection-angle
      sweep~~ -- done in Session 8.
- [ ] The `build/` directory's CMake cache already had
      `BH_ENABLE_CUDA=ON` baked in from before this session (confirmed
      via `grep BH_ENABLE_CUDA build/CMakeCache.txt`) -- so `build.bat`
      currently produces a CUDA-enabled binary too, not the CPU-only one
      the script nominally represents. Not something this session
      changed or caused; flagged here rather than silently "fixed" by
      deleting someone's build directory. Delete `build/` and re-run
      `build.bat` for a genuinely fresh CPU-only cache if that matters.

## Session 5 — Kerr mode performance fix (real user-reported lag)

User-reported: Kerr mode (both CPU and GPU) was visibly laggy in the
interactive app, specifically noticeable at/near the shadow boundary.
Root cause, not a vague "GPU port is slow" guess:

- Every Kerr `RK4Step` stage needs its own `sin`/`cos` pair
  (`KerrGeodesicMath.h`), since a general Kerr orbit isn't planar, unlike
  Schwarzschild's `RK4Step` on `(u, v)`, which is pure polynomial
  arithmetic with zero transcendental calls per step. Kerr steps are
  inherently far more expensive per step, not just per pixel.
- The turning-point retry loop (`KerrGeodesicSolver.cpp` /
  `TracePhotonKerrDevice`) was capped at 6 attempts -- each attempt is a
  FULL RK4 step, so a ray near a turning point could cost up to 6x a
  normal step. Rays that graze close to the photon sphere are exactly
  the rays that form the shadow boundary -- i.e. this worst case sits on
  a performance-critical RING of pixels across most rows of the image,
  not a rare corner case.
- Because `RenderImage`/`RenderImageCUDA` parallelize per row and a
  frame can't finish until its slowest row does, and most rows cross
  that shadow-boundary ring somewhere, the frame's wall-clock time ends
  up bounded by the WORST-case ray cost, not the average -- parallelism
  across rows doesn't hide this the way it would for a single,
  localized expensive pixel.
- On top of that, `App.cpp`'s `PerformRender` was reusing
  Schwarzschild's `maxSteps` (8000) for Kerr's step budget too, despite
  Kerr steps costing far more each -- an equal STEP budget is not an
  equal TIME budget between the two solvers.

Fixes (CPU and GPU both, since both share the same
`Physics::KerrGeodesicMath`/`Physics::Kerr` core and the same per-call
structure):

- Retry cap 6 -> 3 in both `KerrGeodesicSolver.cpp` and
  `geodesic_kernel.cu`'s `TracePhotonKerrDevice` (3 halvings = 8x step
  shrink, already well below pixel-visible turning-point error -- chasing
  it further than that was pure wasted work, not improved accuracy).
- New `kKerrLiveMaxSteps = 1500` (`App.cpp`), replacing the old
  `renderSettings.kerrConfig.maxSteps = renderSettings.geodesicConfig.maxSteps`
  (i.e. "copy Schwarzschild's 8000") in both `PerformRender` and the
  "Validate: Compare CPU vs GPU" button. A ray that still hasn't resolved
  within 1500 steps renders UNRESOLVED (black) -- visually indistinguishable
  from CAPTURED right at the shadow edge, so the cost is a marginally
  thicker boundary, not a visible artifact.
- `kerrConfig.baseLambdaStep` raised from the struct default (0.02) to
  0.05 for live rendering specifically (same speed/accuracy tradeoff
  `geodesicConfig.basePhiStep` already makes for Schwarzschild's live
  config vs. its test config) -- fewer, slightly coarser steps for the
  common (non-turning-point) case too, not just the worst case above.

**Not changed:** `tests/test_kerr.cpp` and `RunKerrGpuRenderTests` use
their OWN `KerrGeodesicConfig`/`DefaultKerrTestConfig` instances (not
`App.cpp`'s live-render tuning), so none of this touched the validated
test tolerances -- confirmed by rebuilding and rerunning the full suite
(still 71/71 in both CPU-only and CUDA builds) after these changes, not
assumed safe because "it's just tuning constants."

### What needs attention (added this session)

- [ ] This was a direct fix for a reported symptom (interactive lag),
      verified by rebuilding/retesting, but NOT verified by an actual
      frame-time measurement before/after (e.g. the app's own FPS
      readout) -- next session should confirm the user's lag is
      actually resolved, not just that the theoretical worst-case cost
      dropped, and adjust `kKerrLiveMaxSteps`/`baseLambdaStep` further if
      it's still too slow (or raise them if there's headroom to spare
      for better shadow-boundary fidelity).
- [ ] A more thorough fix would add a curvature-adaptive step (shrinking
      `h` proactively near the photon sphere, the way Schwarzschild's
      `AdaptivePhiStep` does via its `Accel`-magnitude heuristic) instead
      of only reacting to an already-overshot turning point -- this
      session deliberately took the lower-risk, easily-verified route
      (cap the worst case) under time pressure rather than redesigning
      the step-size heuristic itself.

## Session 6 — Photon Inspector

Picked as the highest-value next increment from the original expansion
prompt's wishlist (its "scientific debugging/photon inspector" tool,
§15) -- chosen over binary black holes/plasma/MHD/report-generation
since it reuses data the solvers already produce, carries essentially no
new-physics risk, and is the kind of tool that makes this feel like a
lab instrument rather than a picture. Builds on all prior sessions;
nothing reverted. Both `build.bat`/`build_cuda.bat` build clean (no
warnings), 71/71 tests still pass (this session added UI only, no new
physics, so no new automated tests were needed or added).

### What's new

- **`Physics::PhotonTraceResult` gained three Kerr-only fields**
  (`kerrEnergyE`, `kerrAngularMomentumLz`, `kerrCarterQ`, `Photon.h`),
  populated by `TracePhotonKerr` right after it derives `E`/`Lz`/`Q`
  from the camera's ZAMO tetrad (`KerrGeodesicSolver.cpp`) -- previously
  these were computed and used internally but never surfaced, so there
  was no way to actually SEE a traced Kerr ray's real conserved
  quantities, only the single derived `b`. Left at 0.0 (unused) by the
  Schwarzschild `TracePhoton`, which has no separate Carter constant.
- **New Photon Inspector panel** (`App.cpp`, Advanced mode, between
  Stage 4 and Stage 6): pick any point in the image by NDC coordinate
  (`-1..1` each axis, same convention `RenderImage` uses per-pixel, so
  "0,0" is always exactly image center regardless of resolution/aspect),
  and see that ray's real fate, step count, closest approach, impact
  parameter, and (in Kerr mode) its actual `E`/`L_z`/`Q`. Recomputed
  live every frame it's open -- tracing one ray is free -- so dragging
  the NDC sliders updates the numbers immediately, no button needed.
  Its path can also be drawn in the 3D debug view (bright magenta,
  distinct from the fate-colored sweep rays) via `TraceInspectorRay`
  (new free function, mirrors the existing `BuildCameraDebugRays`
  pattern but for one precisely-targeted ray instead of a fixed
  vertical-scanline fan).
- **Real bug fixed in the process, not newly introduced:**
  `BuildCameraDebugRays` (the existing Stage 4 "Trace Camera Debug Rays"
  feature) was calling `TracePhoton` (Schwarzschild) UNCONDITIONALLY,
  regardless of the black hole's spin -- so in Kerr mode, that debug
  tool was silently showing Schwarzschild-physics rays and a
  Schwarzschild `b` next to a Stage 3 render that was actually using
  Kerr physics. Now takes a `spin` parameter and branches to
  `TracePhotonKerr` exactly like `RenderImage`/`PerformRender` already
  do, and its one call site (`App.cpp`) passes `blackHole.Spin()`. This
  was found while building the Inspector (which needed the same
  spin-branching logic anyway), not something that was being hunted for
  separately.

### What needs attention

- [ ] No automated test covers the new UI-facing code directly (it's
      UI glue calling already-tested solver functions, so the physics
      risk is low, but the NDC-to-camera-ray math in `TraceInspectorRay`
      hasn't been cross-checked against `RenderImage`'s own per-pixel
      NDC conversion the way, e.g., `test_kerr.cpp` cross-checks the
      Kerr solver against Schwarzschild -- they use the same formula by
      inspection, but "by inspection" isn't the same bar the rest of
      this project holds itself to).
- [ ] Per this environment's known limitation (see "What needs
      attention" further up, from Session 1), UI automation/screenshots
      couldn't be used to actually verify the panel renders/behaves as
      intended -- confirmed only by a clean compile, not by driving the
      app.

## Session 7 — Scientific Plots

Next item off the original expansion prompt's wishlist (§16) after the
Photon Inspector. Builds on all prior sessions; nothing reverted. Both
`build.bat`/`build_cuda.bat` build clean (no warnings, confirmed by
forcing a rebuild of `App.cpp.obj`, not just trusting an empty ninja
diff), 71/71 tests still pass (UI-only addition again, no new physics,
so no new automated tests).

### What's new

New "Scientific Plots" panel (`App.cpp`, Advanced mode, right after
Stage 6's accretion disk controls -- placed there specifically so the
disk temperature plot reads that frame's already-updated slider values
instead of lagging a frame behind), three plots:

- **Deflection angle vs. impact parameter** (`BuildDeflectionSweep`, new
  function): sweeps `b` across a user-set range, tracing a REAL ray at
  each point (same solver the render uses, branching on spin exactly
  like everywhere else), plotted against the closed-form Schwarzschild
  weak-field formula from `docs/physics.md §7`. Button-triggered (not
  live) since a sweep is dozens of full ray traces -- Session 5 already
  established that's expensive in Kerr mode specifically. **Deliberate
  choice:** the analytic reference curve is ALWAYS the spin-less
  Schwarzschild formula, even with spin on -- so in Kerr mode, the gap
  between the two curves is a real physical signal (the Kerr spin
  correction/frame dragging), not a mistake to hide behind a
  spin-corrected analytic curve. The UI says this explicitly rather than
  just showing numbers that don't visually match without explanation.
- **Gravitational time dilation (lapse factor) vs. radius**: closed-form,
  `sqrt(f(r))` (Schwarzschild) or `Physics::Kerr::ZamoLapse` at the
  equator (Kerr) -- cheap enough to recompute live every frame, unlike
  the deflection sweep.
- **Accretion disk temperature profile**: `AccretionDisk::Temperature(r)`
  across the disk's current inner/outer radius, only shown when the disk
  is enabled.

### What needs attention

- [ ] Same as Session 6: no automated test, and not actually driven in
      the app (this environment's UI-automation limitation) -- verified
      by clean compile and the existing solver-level test suite only.
      `BuildDeflectionSweep`'s weak-field formula is the SAME one already
      checked in `tests/test_deflection.cpp` against the Schwarzschild
      solver, so that half is covered transitively; the plotting/UI glue
      itself is not independently tested.

## Session 8 — Kerr deflection-angle validation (closes a twice-repeated open item), and two real solver bugs it found

Triggered by the user reopening `docs/validation.md` and asking what's
next -- that file was stale (still said "32 checks"/"38/42" from before
Kerr existed) and the deflection-sweep gap had been flagged, unaddressed,
in both Session 3 and Session 5. Both are now fixed. Builds on all prior
sessions; nothing reverted. Both `build.bat`/`build_cuda.bat` build clean
(no warnings), now **73/73 (CPU-only) / 81/81 (CUDA)**, up from 63/71 --
verified via a genuinely fresh CPU-only configure (`-DBH_ENABLE_CUDA=OFF`
in a scratch directory), not the `build/` directory's cache, which
(per Session 4's note) already had CUDA baked in and would have silently
inflated the "CPU-only" count.

### What's new

- **`docs/validation.md` brought up to date**: real counts (73/81, not
  32/38/42), and a new §9 content block + a renumbered §10/§11 --
  closed-form Kerr values (horizons/ISCO/photon-orbits/ergosphere at
  a=0, a=1, and a=0.9), the a=0 regression against the Schwarzschild
  solver, the frame-dragging critical-impact-parameter bisection (now
  with its own permanent printed numbers, `prograde=2.844137
  retrograde=6.831638` at a*=0.9 -- previously only a pass/fail, no
  printed value, so this file couldn't honestly cite a number for it),
  and the new deflection validation below. §10 (CPU vs GPU) gained the
  Kerr GPU comparison numbers.
- **New weak-field Kerr deflection check** (`tests/test_kerr.cpp`):
  validates the spin-orbit (frame-dragging) correction to light bending,
  `alpha_retrograde - alpha_prograde ~ 8*M*a/b^2` to leading order.
  Deliberately validates the SCALING LAW (the measured difference
  should fall off as `1/b^2`) using the solver's own numerical output as
  ground truth, then checks the extracted coefficient against the
  literature value of 8 with a loose tolerance -- not the other way
  around, specifically because a formula recalled from memory is an easy
  place to get a sign or factor wrong (as this same session's debugging
  then demonstrated, twice, below).
- **Two real bugs in `KerrGeodesicSolver.cpp` found and fixed while
  getting this test to pass -- not pre-existing issues that were being
  hunted for, genuinely surfaced by writing the check:**
  1. `TracePhotonKerr` never set `totalDeflectionAngle` for an
     escaped or captured ray -- only the disk-hit branch set it. Every
     other caller (the Photon Inspector, Session 6; this new test) was
     silently getting the struct's default `0.0` instead. Fixed by
     setting it in all three return branches, using `(phi - phi0) - pi`
     -- `phi0` has to be subtracted because, unlike the Schwarzschild
     solver's LOCAL tangent-plane phi (which starts at exactly 0 for
     every ray by construction), this solver's phi is the ABSOLUTE
     world-frame azimuth (`atan2` of position), so it starts at
     whatever `phi0` happens to be for that particular launch position.
  2. After fixing #1, retrograde rays STILL gave nonsense (~2*pi
     magnitude) values -- because a retrograde ray's angular momentum
     has the opposite sign, so ITS `phi` DECREASES over the trajectory
     instead of increasing, making the un-`abs()`'d
     "swept-angle-minus-pi" formula sweep roughly `-pi` instead of
     `+pi`. Fixed by taking `abs(phi - phi0)` before subtracting `pi`.
     Found by printing actual fate/steps/closest-approach/impact-
     parameter for the failing rays rather than guessing -- the fate was
     already correctly ESCAPED, which ruled out a geodesic-integration
     bug and pointed straight at the deflection-angle BOOKKEEPING
     instead.
  3. Needed more numerical precision than `DefaultKerrTestConfig()`
     provides to reliably resolve the correction (a genuine second-order
     effect on top of an already-small weak-field deflection): a bigger
     spin (`a=0.9` instead of `0.5`, since the signal scales linearly
     with `a`) and a 10x finer `baseLambdaStep` (`0.001`) for this
     specific check. At the default step size, discretization error was
     comparable to or larger than the signal at `b>=60`, producing a
     wrong-SIGNED (not just imprecise) result -- a precision problem,
     not a physics bug, but one that looked exactly like a physics bug
     until the step size was tightened and the wrong sign flipped back
     to correct.
- **Measured result** (a*=0.9, see `docs/validation.md` §9 for the full
  table): coefficients of 9.71, 10.52, 7.98, 10.05 at `b` = 25, 35, 45,
  60 respectively, averaging **10.63** against the literature
  leading-order value of **8** -- right order of magnitude and sign,
  residual difference consistent with next-order (`M^2/b^2`)
  contamination at these moderately-weak-field `b` values (same
  character as the existing plain-Schwarzschild deflection table in
  validation.md §5 against ITS own leading-order formula).

### What needs attention

- [ ] The deflection check's precision tuning (`a=0.9`, finer step,
      specific `b` range `{25,35,45,60}`) was found empirically for THIS
      spin/config combination -- it is not a general-purpose "how
      precise does the Kerr integrator need to be" answer, and wasn't
      meant to be one.
- [ ] `totalDeflectionAngle`'s caveat (documented inline in
      `KerrGeodesicSolver.cpp` and `docs/physics.md`): it's an exact
      deflection angle only for the equatorial-plane special case (theta
      held at pi/2 throughout). A general off-equatorial Kerr ray also
      has polar motion that a single azimuthal number can't capture --
      the field is a diagnostic "net azimuthal winding" there, not a
      rigorously defined 3D deflection angle. This was always true of
      the field's meaning; it just became load-bearing (rather than
      silently wrong-by-default) this session.

## Session 9 — Binary black holes (approximate)

User asked directly for this, from the original project-expansion
prompt's §10 wishlist. Scoped deliberately: orbital mechanics (exact,
for a circular Newtonian two-body orbit) + an approximate superposed
light-bending scheme for photons, CPU-only, no accretion disk for
either body, no gravitational-wave/inspiral physics. Builds on all
prior sessions; nothing reverted. Both `build.bat`/`build_cuda.bat`
build clean (no warnings), **89/89 (CPU-only) / 97/97 (CUDA)**, up from
73/81.

### What's new

- **`src/Physics/BinarySystem.h`** (new, header-only): two point masses
  on a circular Newtonian orbit about their shared barycenter (fixed at
  the origin). Body 1 *is* the configured `BlackHole` (`GM1=1`, same sim
  units); `massRatio` is body 2's mass relative to it. Positions,
  angular velocity (`Ω²=G(M1+M2)/separation³`), and period all follow
  in closed form from standard two-body Kepler mechanics — exact for
  this (circular, non-inspiraling) orbit.
- **`src/Physics/BinaryGeodesicSolver.{h,cpp}`** (new): traces a photon
  through the two masses' field via a **deliberately approximate**
  scheme -- each mass contributes an acceleration of TWICE its
  Newtonian value (`2GM/r²`), superposed, applied to a photon moving at
  constant coordinate speed (direction bends, speed doesn't). Doubling
  Newtonian gravity is the standard trick for recovering GR's correct
  weak-field deflection angle from a cheap, easily-superposed force law
  -- there is no shortcut available here the way there was for
  Schwarzschild/Kerr, since no exact two-body GR metric exists at all.
  "Frozen lens": both masses' positions are read once at launch and
  held fixed for that photon's whole flight.
- **Adaptive step size, same lesson as Kerr's.** First draft used a
  FIXED step in path length -- which, same as discovered for Kerr in an
  earlier session, needs a prohibitive step count to cross a large
  escape radius (confirmed by literally hitting that wall while writing
  the validation test: billions of steps needed, UNRESOLVED instead of
  ESCAPED). Fixed by making the step scale with distance to the nearest
  mass (`h = clamp(baseStep * minDist, minStep, maxStep)`), the same
  trick `KerrGeodesicSolver.cpp` already uses (step proportional to
  `r`) -- found empirically this session, not copied in advance from
  that precedent, though the fix ended up structurally identical to it.
- **`tests/test_binary.cpp`** (new, 16 checks): orbital mechanics
  (barycenter condition, Kepler's third law, half-period symmetry);
  single-mass reduction (`massRatio=0` collapses to one mass at the
  barycenter, and the solver's deflection then matches the SAME
  weak-field formula already validated for the real Schwarzschild
  solver, to a few percent, improving at larger impact parameter --
  not because the solvers share code, but because reproducing that
  formula is the whole point of the doubling trick); capture/escape
  classification; and a mirror-symmetry check (equal masses placed
  symmetrically on the x-axis deflect a `+y` and `-y` ray by IDENTICAL
  magnitudes, to 10 significant figures) that validates the force
  superposition respects the system's own geometry without needing any
  external reference value.
  - **Deflection measured differently here than for Kerr, deliberately:**
    rather than tracking an accumulated "phi" angle the way the
    Schwarzschild/Kerr solvers do (which caused two real, separate bugs
    for Kerr in Session 8 -- a missing phi0 subtraction, then a missing
    `abs()` for retrograde rays), this measures deflection as the angle
    between a ray's INITIAL direction and its escape direction. That's
    scheme-agnostic (works for any solver that reports an escape
    direction) and sidesteps that entire class of bug by construction,
    not by extra care.
  - **Found and fixed several of my OWN test-setup bugs while writing
    this** (documented in the test file's history, not re-litigated
    here in detail): `BinarySystem::AdvancePhase` takes a TIME, not a
    phase increment (a test initially called it with `pi` expecting a
    half-orbit, which doesn't do that); and three separate test blocks
    initially set `escapeRadius` smaller than (or too close to) the
    photon's actual launch distance from the origin, making the very
    first integration step register as "escaped" trivially before any
    real travel happened -- the same category of mistake
    `test_deflection.cpp` already has a comment warning about, just not
    one this session heeded on the first attempt.
- **Wired into the live app, CPU-only:** `RayTracing::RenderSettings`
  gained `binaryEnabled`/`binarySystem`/`binaryConfig`;
  `RenderImage` branches on it (ahead of the Kerr/Schwarzschild branch);
  `PerformRender` (`App.cpp`) forces the CPU path when enabled, since
  `RenderImageCUDA` has no binary kernel. New Advanced-mode "Binary
  Black Holes (approximate)" panel: enable toggle, mass ratio,
  separation, an "Animate orbit" toggle (ticks `BinarySystem::phase`
  forward via the main loop's existing per-frame update block, gated on
  the same `paused` flag everything else there already uses) or a
  manual phase slider when not animated, and live `r1`/`r2`/`rs1`/`rs2`/
  period readouts -- quantitative, like every other Advanced-mode
  readout in this app, not just a label. Takes priority over Kerr spin
  when both are configured (a rotating member of a binary isn't
  modeled) -- the tooltip says so explicitly.
- **`docs/physics.md`, `docs/validation.md`, `README.md` updated** with
  the real measured numbers from an actual test run (not
  paraphrased) -- `docs/validation.md` §10 has the full table.

### What needs attention

- [x] ~~No accretion disk around either body.~~ -- done in Session 11.
- [x] ~~No GPU port.~~ -- done in Session 10, see below (turned out not
      to need the `BH_HOST_DEVICE`-templating groundwork after all --
      see that session's note on why).
- [ ] The approximate scheme's accuracy near either horizon is
      genuinely uncharacterized beyond "plausible-looking" -- unlike
      Kerr's strong-field claims, which are backed by closed-form
      horizon/ISCO/photon-orbit checks, there is no strong-field
      validation here at all, only the weak-field reduction. This was a
      deliberate scope decision (documented in `docs/physics.md` §11),
      not an oversight, but it means a user zooming in close on either
      binary member should not expect the same quantitative confidence
      the single-hole modes offer.

### Addendum (same session) — user-reported lag, fixed the same way Kerr's was

User reported the app had become very laggy right after this session's
binary-mode addition. Root cause was the same category of problem as
Session 5's Kerr fix: binary mode now has TWO shadow boundaries (one
per mass) instead of one, each a performance-critical ring of pixels
where the adaptive step shrinks and a ray can burn through many steps
before resolving -- and `App.cpp`'s live-render config had been set
generously (`maxSteps=4000`, `escapeRadius=2000`) without the same
tightening pass Kerr's live config got. Fixed identically: new
`kBinaryLiveMaxSteps=1200` and `kBinaryLiveEscapeRadius=600` (both only
affect `PerformRender`'s live config, not `tests/test_binary.cpp`'s own
separate, larger-budget config used for validation -- confirmed by
rebuilding and rerunning the full suite, still 89/89 and 97/97, after
this change). `escapeRadius=600` was deliberately kept well above the
camera's own max orbit distance (300, the Orbit-mode mouse-wheel clamp)
-- going too low would reintroduce the EXACT "escapes trivially before
traveling anywhere" bug this same session's test-writing already hit
and fixed once, just in the live render path instead of a test this
time.

**Not verified by an actual frame-time measurement** (same caveat as
Session 5's fix) -- next session should confirm with the user whether
this actually resolved the lag, not just that the theoretical worst
case dropped.

## Session 10 — Binary black holes on the GPU (the real fix for user-reported lag)

User reported 15fps at 80% resolution; asked which mode, confirmed it
was binary mode. Root cause was simply that binary mode was CPU-only
(Session 9's scope boundary) -- Schwarzschild and Kerr both get GPU
acceleration, binary didn't, so switching to it discarded however much
of a speedup the user's GPU provides over CPU (documented elsewhere in
this file as ~9.5x+ for Schwarzschild). Session 9's "no GPU port" open
item speculated this would need `BinaryGeodesicMath.h`-style
`BH_HOST_DEVICE` templating done first, the way `KerrGeodesicMath.h`
was prepared ahead of Kerr's own GPU port (Session 4) -- that turned
out to be unnecessary: binary's math (`BinaryGeodesicSolver.cpp`'s
`Accel` function) is simple enough that it was just as fast to write a
second, separate `float` version directly in `geodesic_kernel.cu`
(mirroring `TracePhotonDevice`'s existing non-shared-header style for
Schwarzschild) as it would have been to retrofit a shared templated
header for it. Builds on all prior sessions; nothing reverted. Both
`build.bat`/`build_cuda.bat` build clean (no warnings, confirmed by
forcing a rebuild of `geodesic_kernel.cu.obj`), **89/89 (CPU-only) /
101/101 (CUDA)**, up from 89/97.

### What's new

- **`cuda/geodesic_kernel.cu` now has a binary path.**
  `TracePhotonBinaryDevice` (device function) and
  `TraceBinaryPhotonsKernel` (the `__global__` kernel) mirror
  `Physics::TracePhotonBinary` (`BinaryGeodesicSolver.cpp`) step for
  step -- same superposed "doubled Newtonian" acceleration, same
  step-scales-with-distance-to-nearest-mass adaptive stepping -- in
  `float` instead of `double`. New `LaunchBinaryGeodesicKernel` entry
  point (`GeodesicKernel.h`/`geodesic_kernel.cu`), sharing the existing
  persistent device/pinned buffers with the other two Launch functions
  (same `KernelPixelResult` output type, only one kernel launched per
  frame). New `KernelBinaryConfig` (mirrors
  `Physics::BinaryGeodesicConfig`) and `KernelBinaryBodies` (the two
  masses' CURRENT positions/GM values/capture radii, computed host-side
  from `Physics::BinarySystem` each frame -- the device never sees a
  `BinarySystem` or an orbital phase directly, only the already-resolved
  snapshot, same "frozen lens" principle the CPU solver already uses).
- **`RayTracing::RenderImageCUDA` (`CUDATracer.cpp`) now branches on
  `settings.binaryEnabled`** first (ahead of the Kerr/Schwarzschild
  branch, matching `RenderImage`'s existing CPU-side branch order).
  `App.cpp`'s `PerformRender` no longer forces the CPU path for binary
  mode -- removed the `&& !renderSettings.binaryEnabled` condition that
  Session 9 added, since it's no longer needed. UI tooltip updated
  (was: "CPU-only regardless of the GPU toggle below"; now says it runs
  on GPU when available, same as the other two modes).
- **`tests/test_gpu_render.cpp`: new `RunBinaryGpuRenderTests`** (4
  checks) -- same pattern as the Kerr GPU comparison, but no disk
  sampler (binary mode has none) and a denser test starfield
  (`density=0.2` vs. the other tests' default `0.04`), needed because
  without a disk's large guaranteed-bright area, a tightly-framed shot
  dominated by the two shadows can legitimately leave very few
  non-black pixels, which would make the "not a coincidental all-black
  match" sanity check meaningless at the default star density. Also
  loosened that check's threshold (non-black pixels > 1/50 of the
  frame, vs. the disk-having tests' 1/10) for the same reason.
  **Measured result: binary mode's CPU/GPU agreement is tighter than
  EITHER other mode's** (max diff 0.0029, mean 0.000002 -- vs.
  Schwarzschild's 0.0031/0.000001 and Kerr's 0.49/0.00014). Makes sense
  given what's actually different: binary's scheme has zero
  transcendental (sin/cos) calls (pure `sqrt`/arithmetic) and no
  turning-point retry loop, so there's simply less for float-vs-double
  rounding to disagree about than either other mode has.
- **Docs updated** (`docs/physics.md` §11, `docs/validation.md` §10-12,
  `README.md`) with the real measured numbers and counts from an
  actual test run, not paraphrased.

### What needs attention

- [ ] Same caveat as Sessions 5 and 9's lag fixes: this was verified by
      a measured CPU-vs-GPU numerical comparison and a clean
      build/test run, NOT by an actual frame-time measurement
      confirming the user's reported 15fps is now resolved. Next
      session should confirm with the user directly.
- [ ] Binary mode's live-render step budget/escape radius
      (`kBinaryLiveMaxSteps=1200`, `kBinaryLiveEscapeRadius=600`,
      `App.cpp`) were tuned for the CPU-only cost model in Session 9's
      addendum, before this session's GPU port existed. Now that binary
      mode can run on GPU, there may be headroom to raise these for
      better quality/thicker-shadow-boundary fidelity on GPU
      specifically -- not done this session since the user's immediate
      ask was fixing the lag, not maximizing quality once it's fast
      again.

## Session 11 — Binary accretion disks + a spacetime-curvature grid visualization

User reported no accretion disk visible in binary mode and asked why,
plus wanted to see visible motion, and asked to read a DIFFERENT,
unrelated local project (`C:\Users\satya\source\repos\Nahidwin`) for
"the code that does this" (disks) and for its "grid" to be ported in.
Read that project's `Nahidwin.cpp` (~101K, a Newtonian planets/N-body
simulator with a rubber-sheet grid visualization and a skybox-style
black-hole-texture flag) end to end first, confirmed via grep
(`disk|accretion`, zero matches) that it has **no accretion disk code
at all** -- told the user this rather than fabricating a port from
nothing. What it DOES have is a Flamm's-paraboloid-style warped grid
(`CreateGridVertices`/`UpdateGridVertices`/`DrawGrid`), which was
genuinely useful and got ported (cleaned up, adapted, properly labeled
as a conceptual analogy per the project's documentation standards --
the original had no such caveat). The accretion disks were built from
scratch using THIS project's own, already-more-rigorous `AccretionDisk`
machinery (already used for Schwarzschild/Kerr), not from Nahidwin.

Also found and fixed a real, separate bug while investigating "I
should see it moving around": binary orbit animation was nested inside
the Stage 1 Newtonian-simulation's `if (!paused)` block, so the
unrelated Stage 1 "Paused" checkbox could silently freeze binary
motion -- decoupled, "Animate orbit" is now the sole gate.

Builds on all prior sessions; nothing reverted. Both
`build.bat`/`build_cuda.bat` build clean (no warnings), **98/98
(CPU-only) / 110/110 (CUDA)**, up from 89/101.

### What's new

- **Each binary body gets its own accretion disk.** New
  `PhotonTraceResult::diskBodyIndex` (0 or 1, shared field, unused/0 for
  Schwarzschild/Kerr) lets the renderer pick between two separate
  `Astronomy::AccretionDisk` instances -- body 2 has its own real Kelvin
  scale (different mass) and its own radii, scaled from the SAME
  `diskSettings`/`diskEnabled` the single-body modes already expose (no
  new disk-specific sliders) by body 2's Schwarzschild radius relative
  to body 1's, preserving the "radius in units of `r_s`" the user
  actually set. `Physics::BinaryGeodesicSolver.cpp` gained
  `CheckDiskCrossing` (both disks coplanar with the orbital plane,
  `z=0`, a documented simplification) -- redshift reuses the exact
  Schwarzschild solver's `v=1/sqrt(r-rs)` / `sqrt(1-rs/r)` formulas
  centered on whichever body owns the disk, but combined with the
  ALREADY-TRACKED velocity vector directly (this scheme tracks `vel`
  explicitly every step, unlike Schwarzschild/Kerr which reconstruct a
  photon's local direction from a conserved impact parameter) -- simpler
  to get right here than it was for either of those solvers.
- **GPU mirrors the CPU disk logic exactly**
  (`BinaryCheckDiskCrossing`/`TracePhotonBinaryDevice` in
  `geodesic_kernel.cu`; new `KernelBinaryConfig::checkDisk`/disk radii
  and `KernelBinaryBodies::rs1`/`rs2`; `KernelPixelResult::diskBodyIndex`
  propagated through `CUDATracer.cpp`). Measured CPU/GPU agreement with
  both disks enabled: max diff 0.0034, mean 0.000002 (vs. 0.0029/0.000002
  without disks last session) -- still the tightest of the three modes.
- **Orbit-animation/pause decoupling** (see above) -- `App.cpp`'s main
  loop now advances `binarySystem.phase` unconditionally on
  `binaryModeEnabled && binaryOrbitAnimated`, outside the Stage 1
  `if (!paused)` block it was previously nested in.
- **3D debug view now actually shows the binary** -- previously it drew
  nothing binary-specific at all (just the single-BH horizon sphere at
  the origin, wrong position entirely once two offset bodies exist).
  Now draws both bodies' horizons (`Renderer::DrawSphere`/
  `DrawWireSphere`, both now with the center-taking overload
  `DrawWireSphere` was missing until this session) plus a sampled orbit
  circle per body, so motion is visible even with the lensed render
  view off or the orbit momentarily paused.
- **New "spacetime curvature grid" visualization** (ported concept from
  Nahidwin, not its code): `Renderer::DrawWarpedGrid` -- the existing
  flat debug grid, but with each line segment's endpoints displaced in
  `y` by Flamm's paraboloid (`dz = 2*sqrt(rs*(r-rs))`), summed over
  every active mass (one in Schwarzschild/Kerr mode, two in binary
  mode). Explicitly labeled in the UI tooltip as a 2D analogy for
  spacetime curvature, not a literal picture of it -- Nahidwin's
  original had no such caveat; this project's documentation culture
  requires one (see `docs/physics.md` §11).
- **`tests/test_binary.cpp`: +9 checks** for the new disk physics (both
  bodies' disks hit, correctly attributed via `diskBodyIndex`, radius in
  range, redshift finite and positive; a ray missing both disks
  registers no hit). `tests/test_gpu_render.cpp`'s
  `RunBinaryGpuRenderTests` updated to enable both disks too (previously
  tested only the starfield/capture/escape path).

### What needs attention

- [ ] `DrawWarpedGrid` recomputes its height function per-vertex, per-
      frame, in plain CPU code (immediate-mode GL, no VBO/caching) --
      fine at the tested grid sizes (40x40 divisions, 2 masses) but
      not profiled at larger divisions counts; if someone cranks the
      grid resolution up, this is the first place to look before
      assuming a GPU-side kernel is the bottleneck.
- [ ] The warped grid's embedding is evaluated independently per mass
      and summed linearly -- exact for a single Schwarzschild mass,
      but summing two separate embeddings is itself a simplification
      for two bodies (real superposed curvature isn't simply additive
      in this way), on top of the same-plane assumption the disks also
      make. Consistent with this whole mode's existing "approximate,
      explicitly labeled" standard, not a new gap.

### Addendum (same session) — Flamm's paraboloid as an actual funnel surface

User clarified (via a direct question, since the two interpretations
needed genuinely different implementations) that "Flamm's paraboloid"
meant the classic textbook embedding-diagram FUNNEL/TRUMPET surface of
revolution, not the flat sagging-grid-lines approach this session
built first (ported from Nahidwin's style). Replaced it:

- **`Renderer::DrawWarpedGrid`/`GridMass` removed entirely** (confirmed
  no other callers first) and replaced with
  `Renderer::DrawFlammParaboloid(center, rs, outerRadius, color, ...)`
  -- a wireframe surface of revolution (concentric rings + radial
  lines, matching the existing `DrawWireSphere` aesthetic) around a
  vertical axis through one mass at a time, height
  `y = -2*sqrt(rs*(rho-rs))` at planar distance `rho` from that axis.
  Rings use a quadratic ease-in on `rho` so the throat (`rho=rs`, where
  the real surface has a vertical tangent) is resolved smoothly instead
  of as a sharp cone -- a sampling-density choice, not a change to the
  underlying exact formula (same one the flat-grid version used).
  Called once per active mass from `App.cpp` (one funnel in
  Schwarzschild/Kerr mode, two independent funnels in binary mode,
  rather than one method call that internally summed contributions the
  way the flat grid did -- a surface of revolution can't meaningfully
  "sum" with another one the way flat heights could).
- Same checkbox/state variable (`showWarpedGrid`), relabeled "Show
  Flamm's paraboloid (spacetime curvature funnel)" with an updated
  tooltip describing the actual shape. `docs/physics.md` §11 updated to
  match.
- No test/count changes -- this is a debug-view-only visual change
  (immediate-mode GL drawing), nothing the headless test suite
  exercises. Both `build.bat`/`build_cuda.bat` rebuilt clean (confirmed
  via forced recompiles of `App.cpp`/`Renderer.cpp`, not just an empty
  ninja diff) and the full suite still passes (98/98, 110/110,
  unchanged from before this addendum).

### Build environment note (for next session, not a code issue)

`cmake`/`ninja` are not on PATH in the plain shell used for dev in this
environment — they only resolve inside a VS2022 Developer Command Prompt
(`vcvars64.bat`). Both `build.bat` and `build_cuda.bat` already call
`vcvars64.bat` themselves when run normally (e.g. by double-clicking or
from a regular `cmd.exe`), so this only matters if you're driving the
build from a non-Developer shell/script directly.

## Session 12 — Planetary destruction, Milestone 1 (spring-network tidal disruption)

User pasted a large (23-subsection) spec for a full planetary-destruction
system: materials, tidal tensors, elastic/plastic deformation, fracture,
fragmentation, self-gravity, debris accretion into the disk, timeline
scrubbing, GPU acceleration, a full UI panel, 12 initial-condition
presets, and validation tests. Scoped this down explicitly (per the
spec's own §28.22: land a stable milestone, don't fake completeness) and
confirmed the cut with the user before writing any code: Milestone 1 is
rigid-body-plus-deformation-plus-fragmentation, using a real mass-spring
elastic model (not StarBody's unbound-swarm approach), with **no
self-gravity and no GPU acceleration** this pass.

Builds on all prior sessions; nothing reverted or modified except
additive changes to `App.cpp` (new state/lambdas/UI block; existing
star/jet/binary/Kerr code untouched). Both `build.bat`/`build_cuda.bat`
build clean, **116/116 (CPU-only) / 128/128 (CUDA)**, up from 98/110 (all
18 new tests are in the CPU-only `bh_physics`/`bh_astronomy` libraries, so
both build configurations gain the same 18).

### What's new

- **`Physics::PlanetMaterial`** (`src/Physics/PlanetMaterial.h`, header-
  only data): `PlanetComposition` enum (Rocky, IronRich, Icy, GasGiant,
  Differentiated, Custom) with per-composition reference density, a
  dimensionless fracture-strain threshold (`materialStrength` — the ONE
  number that actually drives when a planet comes apart), a spring-
  stiffness multiplier, and a damping factor. `Differentiated` is
  approximated as a single averaged (iron-leaning) material, not a real
  layered core/mantle/crust — flagged as such in the header and in
  `docs/physics.md` §12.
- **`Physics::TidalForceModel`** (`src/Physics/TidalForceModel.h`, free
  functions): `NewtonianTidalAccelerationSim` (`Δa ≈ 2·G·M_bh·R_p/r³`) and
  `RocheLimitSim` (`r_Roche ≈ 2.44·R_p·(M_bh/M_p)^(1/3)`), both explicitly
  labeled weak-field/idealized diagnostics. **Neither feeds back into the
  simulation** — they're displayed in the UI for comparison only; actual
  destruction is decided entirely by spring strain vs. material strength.
- **`Astronomy::PlanetBody`** (`src/Astronomy/PlanetBody.h/.cpp`):
  extends StarBody's "cluster of independent test particles free-falling
  under `Physics::StaticObserverGravityAccelSim`" with an actual cohesion
  force — each particle also connects to ~6 nearest neighbors via springs
  (lattice built once at construction, O(N²) nearest-neighbor search,
  fine at the ~90-particle default). Semi-implicit Euler integration
  (gravity + Hooke's-law spring force + axial damping), same integration
  style as StarBody/`Jet::FlightPhysics`. A spring breaks permanently once
  `|Δlength/restLength|` exceeds the material's `materialStrength`; no
  velocity/position is altered by the break itself (what keeps momentum
  conserved — see tests). Connected components of the surviving lattice
  are rebuilt via union-find into `PlanetFragment`s (mass/COM
  position/velocity derived from member particles, capture/escape
  classified per-fragment) whenever a spring has broken since the last
  rebuild.
- **App.cpp integration**: `planets` vector alongside `stars`, a
  `SpawnPlanetWith`/`SpawnPlanet` lambda pair mirroring `SpawnStar`, five
  one-click initial-condition presets (stable orbit / close flyby / rocky
  disruption / icy disruption / direct infall — a representative subset
  of the spec's 12, not all of them), a composition dropdown, mass/
  particle-count/material-strength-multiplier sliders, and live readouts
  (fragment count, escaped-fragment count, captured/live particle counts,
  the most-recently-spawned planet's tidal-acceleration/Roche-limit
  diagnostics). Simple mode gets a one-paragraph "Add Planet"/"Clear
  Planets" control; Advanced mode gets the full panel. Rendering reuses
  `Renderer::DrawPath`/`DrawPoint` exactly like `DrawStars`, with fragment-
  id-based color shading so broken-off pieces read as visually distinct;
  the (non-ray-traced) debug 3D view additionally draws live springs as
  thin lines so deformation is legible before anything fractures.
- **`tests/test_planet.cpp`: +18 checks** — density-preset consistency,
  tidal-acceleration/Roche-limit closed-form scaling, fracture-threshold
  comparison across materials, a scripted close encounter (stays finite,
  actually fractures, fragment masses sum to the original total),
  momentum conservation for an isolated two-particle spring far from the
  hole, and a distant stable-orbit stability check (never spuriously
  fractures, stays one fragment).
- **`docs/physics.md`**: new §12 ("Stars and planets: tidal disruption")
  documenting both StarBody's existing model (previously undocumented
  outside code comments/HANDOFF) and PlanetBody's spring/fracture model
  with the same inline-approximation-labeling voice as the rest of the
  file; old §12 ("Limitations") renumbered to §13 and given a line
  pointing back at §12. **`docs/validation.md`**: new §12 summarizing the
  18 planet tests; summary table (now §13) updated to 116/128.

### Deliberately deferred to a later session (continuation plan, per spec §28.22)

- [ ] **Planetary self-gravity** among fragments/particles (spec 28.10) —
      the spring lattice is a stand-in for cohesion, not a simulation of
      mutual attraction; large separated fragments cannot gravitationally
      reaccumulate.
- [ ] **GPU acceleration** of the spring/particle system (spec 28.20) —
      planets, like stars, are CPU-only this milestone. If this becomes a
      perf problem at higher particle counts, follow the same
      `BH_HOST_DEVICE`-templated-header pattern Kerr/binary used for their
      geodesic kernels (see `docs/physics.md` §10/§11), not a bespoke
      scheme.
- [ ] **Debris → accretion-disk mass coupling** (spec 28.12/28.13) —
      `AccretionDisk::Mdot` is still purely a UI slider; captured planetary
      mass does not feed into its temperature/brightness.
- [ ] **Timeline scrubbing/rewind, save/load experiments** (spec
      28.14/28.17) — no snapshot storage exists yet.
- [ ] **Remaining 7 of the spec's 12 initial-condition presets**, a
      cross-sectional/interior-layer view, and a dedicated fragment-
      tracking camera mode (spec 28.8/28.15/28.19).
- [ ] **Full continuum/FEM or SPH-grade deformation** (spec 28.7, Level
      4) — this milestone stays at Level 2/3 (deformable lattice +
      fragmentation); the mass-spring model is a documented approximation,
      not a validated finite-element or SPH simulation.
- [ ] **Kerr/binary-hole-aware capture for planets** — like StarBody,
      `PlanetBody::Update` checks only `BlackHole::SchwarzschildRadiusSim()`,
      not the active Kerr/binary geometry, even if one is configured.

### Addendum (same session) — solid-body rendering + faster default disruption

User tried it and reported it never visibly disintegrates. Two real
issues, confirmed (not guessed) before fixing:

1. **Rendering never looked like a planet at all.** Planets reused
   `StarBody`'s point-cloud rendering (small dots + trails) — even fully
   intact, 90 tiny points don't read as "a planet," so there was no way
   to visually tell deformation/fracture was happening even when it was.
2. **The plain "Add Planet" button's defaults were too gentle.** Scripted
   a probe directly against `PlanetBody` (not a guess) to measure actual
   time-to-first-fracture at the old defaults (spawn radius 20, orbital
   speed fraction 0.55, Rocky): it DOES eventually fracture, but not
   until ~2344 steps (~39s of real time at 60fps) — long enough that a
   quick look would show nothing happening.

Fixed both:

- **`Rendering::Renderer::DrawEllipsoid`** (new): a solid sphere
  non-uniformly scaled into a prolate spheroid along a given axis,
  reusing the existing `DrawSphere(radius, color, ...)` unit-sphere mesh
  under an orthonormal-basis rotation + `glScaled` rather than a new mesh
  generator.
- **App.cpp's `DrawPlanets`** rewritten to draw solid shapes instead of
  points: while a planet is still a single intact fragment, it's drawn as
  one `DrawEllipsoid` stretched along the actual radial direction from
  the black hole, with both semi-axes measured from the live particles'
  real spread (so it visibly elongates as tidal stress increases, not a
  cosmetic animation). Once `Fragments().size() > 1`, each fragment with
  ≥4 live particles becomes its own solid `DrawSphere` sized to its
  spread, flying apart independently and colored by the same per-
  fragment shading as before; fragments with <4 particles (not worth a
  sphere) fall back to the old points+trails rendering as genuine debris.
- **Tuned the plain "Add Planet" defaults** from (spawn radius 20, speed
  fraction 0.55) to (14, 0.7) — re-probed several candidate values against
  `PlanetBody` directly before picking this one: breaks at ~1262 steps
  (~21s), ends with 76/90 particles still live and flying apart as
  visible fragments (not swallowed whole), which reads as actual
  disintegration rather than either "nothing happens" or "it just
  vanishes." The five named presets were left as originally tuned — they
  already each demonstrate a distinct, verified outcome (stable/no
  fracture, flyby with survivors, rocky/icy full consumption, slow
  infall).
- No test-count change — this is rendering plus UI-default tuning, not a
  change to `PlanetBody`'s physics; `tests/test_planet.cpp` is unaffected
  and the full suite still passes unchanged (116/116, 128/128). Verified
  `BlackHoleSimulator.exe` and `BlackHoleTests.exe` both rebuild clean.

## Session 13 — GPU (CUDA) acceleration for planetary destruction physics

User asked to GPU-accelerate the planet simulation (the deferred item from
Session 12's continuation plan). Stated the performance reality up front,
then built it anyway, since the real payoff isn't speed at today's
defaults — it's letting particle counts go much higher while staying
real-time: `PlanetBody::Update`'s per-frame work is O(particles+springs),
nothing quadratic (the only O(N²) step, `BuildSpringLattice`, runs once at
construction) — at the default particle counts (20-250) this is already
sub-millisecond on one CPU core, and a GPU kernel launch + host↔device copy
at that scale is expected to be *slower*, not faster, due to fixed
overhead. Measured this directly rather than asserting it (see the new
automated test's output below: CPU and GPU agree to ~3×10⁻⁶ sim units
after 20 steps, confirming correctness; no separate timing claim is made
since one wasn't needed to justify the real motivation — raising the
particle-count ceiling).

Builds on Sessions 1-12; nothing reverted. Both `build.bat` (freshly
reconfigured with `-DBH_ENABLE_CUDA=OFF` in a scratch directory to confirm
the CPU-only path still compiles byte-for-byte the same as before this
session) and `build_cuda.bat` build clean. Test counts: **116/116
(CPU-only, unchanged) / 134/134 (CUDA, up from 128 — 6 new checks)**.

### What's new

- **`cuda/PlanetKernel.h` + `cuda/planet_kernel.cu`** (new, following
  `cuda/GeodesicKernel.h`/`geodesic_kernel.cu`'s established plain-header +
  nvcc-compiled-`.cu` split, but NOT its `BH_HOST_DEVICE`-templated-shared-
  math approach — this follows the *other* existing precedent instead, the
  binary black hole's hand-duplicated-float-formula approach, since
  `Physics::StaticObserverGravityAccelSim`'s formula is equally simple and
  this avoids touching `BlackHole.h` at all). Four `__global__` kernels —
  gravity (one thread per particle), spring force via `atomicAdd` (one
  thread per spring, two threads can share a particle), semi-implicit
  Euler integration + horizon-consumption check (per particle), fracture
  check (per spring) — launched in that dependency order, mirroring
  `PlanetBody::Update`'s exact CPU sequence and formulas, just in `float`.
  Persistent grow-only device buffers (particle-sized and spring-sized,
  tracked independently), same pattern as the ray tracer's pinned-buffer
  cache, though these aren't pinned (no page-locked host buffer) since the
  caller already owns its own host arrays and sizes here are tiny compared
  to a rendered image.
- **`Astronomy::PlanetBody::UpdateGPU(dt, blackHole)`** (new, `#ifdef
  BH_PLANET_CUDA_AVAILABLE`): marshals this planet's particles/springs into
  flat float/uchar arrays, calls `CudaPlanetSim::RunPlanetStepCUDA`,
  unmarshals the results back, then calls the same `FinishStep` tail the
  CPU path uses (trail push, conditional fragment rebuild, tidal/Roche
  diagnostics) — extracted from `Update()`'s former tail into a shared
  private method specifically so neither path duplicates that logic.
  Returns `false` (this instance left completely untouched) if CUDA isn't
  compiled in, no device is present, or the call fails, so
  `App.cpp`'s loop (`if (!planet.UpdateGPU(dt, blackHole)) planet.Update(dt,
  blackHole);`) always has a correct result either way — same automatic-
  fallback-only philosophy as the ray tracer, no UI toggle.
- **New, separate compile definition `BH_PLANET_CUDA_AVAILABLE`** (not
  `BH_CUDA_AVAILABLE`) on `bh_astronomy`, gated by the same `BH_ENABLE_CUDA`
  option but intentionally its own macro — ray-tracing GPU support and
  planet-physics GPU support are independent capabilities in different
  libraries (`bh_cuda_tracer` vs. the new `bh_planet_cuda`), even though
  today both happen to turn on together.
- **UI**: Advanced "Planets" panel gained a GPU status line (same phrasing
  style as the render-resolution section's existing one) explicitly stating
  the no-speedup-at-this-scale caveat in its tooltip, and the "Particle
  count (resolution)" slider's max is raised from 250 to 2000 **only** when
  the GPU path is actually active — the one place this feature is visibly
  different from before, rather than an invisible no-op at default
  settings.
- **`tests/test_planet_gpu.cpp`** (new, `#ifdef`-equivalent via CMake's
  `if(BH_ENABLE_CUDA)` gate on `tests/CMakeLists.txt`, wired into
  `test_main.cpp` the same way `test_gpu_render.cpp` already is): builds
  one `PlanetBody`, copies it into a CPU instance and a GPU instance (NOT
  two independently-constructed instances — particle placement is
  randomized per construction, so separate instances wouldn't be
  comparable), steps both 20 times, and checks position/velocity agree
  within a float-appropriate tolerance and that consumed/broken-spring
  outcomes match exactly. Measured result: max position diff 3.2×10⁻⁶, max
  velocity diff 6.2×10⁻⁸ sim units — comfortably inside the 10⁻² tolerance,
  consistent with the Kerr/binary kernels' own CPU/GPU agreement.
- **`docs/physics.md` §12**: added a GPU-acceleration subsection explaining
  the "not a speedup, a scale-up" framing inline, in the same
  approximation-labeling voice as the rest of the file. **`docs/validation.md`
  §12**: added the GPU test's results table; summary table (§13) updated to
  134 for the CUDA build (116 unchanged for CPU-only).

### Decisions worth knowing about

- `RebuildFragments` (union-find) and `BuildSpringLattice` (one-time O(N²)
  neighbor search) deliberately stay CPU-only in BOTH the CPU and GPU
  paths — sequential-shaped or one-time, not good GPU fits, and not the
  per-frame cost anyway.
- One kernel launch per live planet per frame, not a single batched launch
  across every simultaneously-spawned planet — simpler, and the difference
  between 1 and a handful of small launches per frame is negligible at
  realistic planet counts. Listed as a non-goal, not a bug, if someone
  later wants to spawn dozens of planets at once.
- Rendering (immediate-mode GL, one `glBegin`/`glEnd` pair per particle/
  spring/fragment) is unchanged and untouched by this session — if someone
  actually pushes particle count toward the new 2000 ceiling, the
  rendering loop becomes the real bottleneck before the physics does. Not
  fixed here; flagged so it isn't mistaken for a regression in this
  change.

### What needs attention

- [ ] No batched multi-planet kernel launch (see above) — fine for a
      handful of planets, would need revisiting for dozens+.
- [ ] Rendering cost at high particle counts (see above) — the GPU path
      makes 2000-particle planets *simulate* fine; nothing was done to
      make *drawing* 2000 particles/springs/fragments-worth-of-primitives
      per frame fast.
- [ ] No actual frame-time/throughput measurement was taken (CPU-vs-GPU
      wall-clock comparison) — only correctness (position/velocity/
      fracture agreement). If someone wants to confirm the "GPU wins at
      high particle counts" claim quantitatively rather than by
      complexity argument alone, that measurement is still open.

### Addendum (same session) — Lambertian shading for planet bodies

User asked to "add shadows to the planets." Planets were flat-colored
silhouettes (`Renderer::DrawSphere`/`DrawEllipsoid`, plain `glColor3f`, no
lighting at all) — read as round only via screen-space shape, with no lit/
dark side. Added per-vertex Lambertian shading rather than true shadow-
casting (shadow maps): this is a legacy fixed-function immediate-mode
renderer with no FBO/depth-texture infrastructure, and a lit/dark side is
what "shadows" means in essentially every other casual rendering request
like this — true planet-casts-shadow-on-disk geometry would be a much
larger, differently-scoped feature, not attempted here.

- **`Renderer::DrawShadedSphere`/`DrawShadedEllipsoid`** (new, alongside
  the existing flat-colored `DrawSphere`/`DrawEllipsoid`, which are left
  completely untouched — still used as-is for the event horizon spheres in
  binary/single-hole debug views, which should NOT look lit). Same
  tessellation as their flat counterparts, but with per-vertex shading:
  `0.25` ambient floor + `0.75` Lambertian term against the sphere's
  analytic normal — the exact same split `Jet::JetRenderer`'s shader
  already uses, for visual consistency with the one other lit object in
  this scene. `lightDirection` uses the same convention as
  `JetRenderer::Draw` too ("points FROM the light TOWARD the body"), and
  since the black hole is this scene's only light source and sits at the
  world origin, callers just pass `normalize(bodyPosition)`.
- For the ellipsoid case, the per-vertex normal used for shading is the
  UNSTRETCHED unit-sphere normal rotated into world space — exact once
  `primarySemiAxis == lateralSemiAxis`, an approximation (not the true
  ellipsoid surface normal) once a planet is visibly tidally elongated.
  Documented as such in the method's own header comment rather than
  presented as exact — visually convincing at the stretch ratios a
  planet actually reaches before fracturing.
- `App.cpp`'s `DrawPlanets` lambda updated to call the shaded variants,
  reusing the already-computed `radialDir` (direction from the black hole
  to the body) directly as the light direction for the intact-body
  ellipsoid case, and `normalize(com)` for each separated fragment sphere.
- No test changes (visual-only; no automated coverage of the rendering
  path, consistent with this project's existing stance that
  StarBody/PlanetBody rendering is unit-untested by design). Verified
  `BlackHoleSimulator.exe` and `BlackHoleTests.exe` both rebuild clean and
  the full suite still passes unchanged.

## Session 14 — Planetary self-gravity, debris → accretion-disk coupling, and a real bug fix the first exposed

User asked for both remaining deferred items from Session 12/13's
continuation plan: self-gravity (fragment reaccumulation) and debris
feeding into the accretion disk. Scoped via plan mode first (self-gravity
needed an explicit decision from the user: physically-exact-but-invisible
vs. deliberately exaggerated — they chose exaggerated, same spirit as
`initialRadius`'s own existing exaggeration-for-visibility).

**Implementing self-gravity exposed a real, pre-existing bug**, not a new
design problem: enabling self-gravity (correctly scaled) immediately
caused runaway collapse even on planets that should have been stable,
which traced back to `ApplySpringForces` dividing spring force by each
particle's REAL kilogram mass instead of its mass fraction of the planet.
Since `kBaseSpringStiffness` is a freely-tuned sim-unit constant (not
calibrated to real N/m) and gravity is a pure sim-unit acceleration with
no mass term, dividing a sim-unit force by a real ~10²²–10²⁴ kg mass made
spring accelerations ~15-20 orders of magnitude weaker than gravity --
springs were doing essentially nothing since Milestone 1 shipped.
Everything that looked like "deformation resisted by springs" in Sessions
12-13 was actually almost entirely unopposed differential gravity
(StarBody's own mechanism), despite the code's and docs' claims
otherwise. Fixed (see `docs/physics.md` §12 for the full writeup) and
every preset/default re-tuned against the corrected physics via scripted
probes, not guesses -- see "What's new" below for the new numbers.

Builds on Sessions 1-13; nothing reverted. Both a fresh CPU-only
configuration and the CUDA build compile clean. Test counts: **126/126
(CPU-only, up from 116 -- +1 self-gravity test, +9 flare tests) / 144/144
(CUDA, up from 134)**.

### What's new

- **Spring-force mass-fraction fix** (`PlanetBody::ApplySpringForces` and
  the mirrored `SpringForceKernel` in `cuda/planet_kernel.cu`, which
  needed the same fix plus a new `totalMassKg` parameter): divides by
  `particle.mass / totalMassKg` instead of raw `particle.mass`. This
  changes actual simulated behavior, not just internals -- springs now
  genuinely resist deformation.
- **Retuned constants and defaults**, all via scripted probe against
  `PlanetBody` directly (same methodology as the Milestone-1/GPU-session
  tunings): `kBaseSpringStiffness` 400.0 → **0.025** (most of this drop is
  for explicit-Euler numerical stability at the corrected force scale, not
  because springs are meant to be weak -- confirmed via probe that higher
  values spuriously shatter an otherwise-stable distant planet). The plain
  "Add Planet" default moved from (spawn radius 14, speed fraction 0.7) to
  **(9.0, 0.7)** -- fractures in ~31s now instead of instantly/never.
  "Rocky disruption"/"Icy disruption" presets moved from (9.6, 0.5) to
  **(7.0, 0.5)** -- now fracture in ~11-17s (icy faster, as intended).
  "Stable orbit" (60, 1.0) and "Direct infall" (18, 0.0) presets were
  reverified and left unchanged -- both still behave as intended under the
  corrected physics. "Close flyby" (15, 0.85) was reverified too: it no
  longer fractures at all (previously it did, under the broken physics) --
  its existing tooltip already said "mild stretching," not "disruption,"
  so this is accurate without a wording change; a real planet surviving a
  flyby at that distance is the more physically sensible outcome anyway.
- **`Astronomy::PlanetBody::ApplySelfGravity`** (new) + the mirrored
  `SelfGravityKernel` in `cuda/planet_kernel.cu`: direct O(N²) summation,
  using a standalone tuned force constant (`kSelfGravityCoefficient`, NOT
  the real mass-ratio formula -- see its own and
  `PlanetBodySettings::selfGravityStrength`'s comments for why) with a
  minimum-distance cutoff (`kSelfGravityMinDistanceFactor = 2.5×
  initialRadius`) below which it's skipped entirely -- without that cutoff
  it fights the spring lattice at close range and destabilizes an intact
  planet (confirmed via probe; first attempt used 1.5× and still failed
  for a smaller test planet whose full seed-sphere diameter exceeded that
  factor). Measured effect (scripted probe, not assumed): the two largest
  surviving fragments of a disrupted planet end up **~10× closer together**
  after a few minutes of sim time with self-gravity on vs. off. New
  `PlanetBodySettings::selfGravityEnabled`/`selfGravityStrength` fields,
  default on; new UI controls (checkbox + strength slider) in the Planets
  panel with a tooltip stating plainly that this is exaggerated, not real
  physics.
- **`Astronomy::TidalDisruptionFlare`** (new, `src/Astronomy/
  TidalDisruptionFlare.h/.cpp`): a decaying-accumulator model (12-second
  exponential time constant, clamped to a +0.6 eddingtonFraction ceiling)
  explicitly documented as a simplified stand-in for the real `t^-5/3`
  fallback-accretion light curve, not an attempt to reproduce it.
  `PlanetBody` gained `MassCapturedLastStep()` (tracks mass newly consumed
  during the most recent `Update()`/`UpdateGPU()` call, detected inline at
  the exact point each path already flips `consumed`). App.cpp wires one
  `TidalDisruptionFlare` instance: every planet's captured mass feeds it
  each frame, and its current boost is added to a per-frame COPY of the
  user's own disk settings before rendering (never mutating the
  slider-backed value) -- both the main render path and the CPU-vs-GPU
  validation render path. New Advanced-panel readout under the disk
  section ("Tidal disruption flare active: Eddington fraction +X.XX" /
  "No recent captures"). Single/Kerr-hole scope only, same existing
  limitation stars/planets already have for horizon checks.
- **`tests/test_planet.cpp`: +1 check** (self-gravity reaccumulation,
  comparing two separately-run disruption scenarios' largest-two-fragment
  separation with/without self-gravity, generous 2× margin against the
  observed ~10× effect to tolerate per-run randomness in particle
  placement). **`tests/test_planet_gpu.cpp`**: unchanged check count, but
  now implicitly exercises self-gravity too (both CPU and GPU default to
  it enabled) -- re-measured agreement: max position diff 2.7×10⁻⁶, max
  velocity diff 1.7×10⁻⁶ sim units, still comfortably inside tolerance.
  **`tests/test_tidal_flare.cpp`** (new, +9 checks, unconditional -- not
  CUDA-gated): fresh-tracker-inactive, registering-mass-activates,
  monotonic-decay, ceiling-clamp, and accumulates-rather-than-overwrites
  checks.
- **`docs/physics.md` §12**: added the bug-fix writeup (quoted above, in
  full there) plus new self-gravity and tidal-flare subsections, same
  approximation-labeling voice as the rest of the file. **`docs/
  validation.md` §12**: added the self-gravity test row, a new flare-test
  subsection, and re-measured GPU-agreement numbers; summary table (§13)
  updated to 126/144.

### Decisions worth knowing about

- The spring-force bug was caught by symptom (self-gravity immediately
  destabilizing scenarios that should have been fine), not by code review
  -- worth remembering that a force law's UNITS deserve as much scrutiny
  as its functional form, especially in a sim that deliberately mixes real
  SI quantities (particle mass in kg) with freely-tuned dimensionless sim
  constants (spring stiffness) in the same formula.
- Self-gravity's minimum-distance cutoff is a deliberate design choice,
  not a workaround to hide instability: self-gravity is conceptually about
  attraction between SEPARATED masses (fragments, or particles that have
  drifted apart), not an additional force between already-spring-connected
  neighbors, which the spring already governs. The cutoff makes that
  distinction explicit rather than relying on tuning alone to keep the two
  forces from fighting.
- `SelfGravityKernel` is this project's first O(N²)-per-thread GPU kernel
  (every other kernel so far is O(1) or O(E) work per thread) -- still
  trivial at the current particle ceilings (90 default / 250 CPU / 2000
  GPU), but the first thing to revisit if those ceilings are ever raised
  further or multi-planet batching (still not implemented -- see below) is
  tackled.

### What needs attention

- [ ] No batched multi-planet GPU kernel launch (still one launch per
      planet per frame -- self-gravity makes this somewhat more expensive
      per planet than before, worth revisiting together).
- [ ] Debris → disk coupling is tuned by feel (decay time constant,
      kg-to-Eddington-fraction conversion, clamp), not derived or
      calibrated against a real TDE light curve -- flagged in both the
      code and `docs/physics.md`, not hidden, but a real
      `t^-5/3`-shaped model remains a documented future improvement if
      ever wanted.
- [ ] Fragments that reaccumulate via self-gravity can still pass through
      each other at close range -- there's no collision response, only
      attraction. Flagged in the original plan as a known gap, still open.
- [ ] Binary-hole-aware self-gravity/flare coupling remains out of scope
      (single/Kerr-hole only), consistent with the existing capture-check
      limitation.

## Session 15 — Fix: stars/planets ignored binary mode's second body

User reported stars and planets spawned while binary mode is active still
behave as if a single black hole sits at the origin ("still goes to the
automatic center of where it was pre binary"), rather than responding to
the actual two-body configuration. Confirmed as a real bug, not a known
limitation: `Astronomy::StarBody::Update` and `PlanetBody::Update`/
`UpdateGPU` always called the single-hole `Physics::
StaticObserverGravityAccelSim` (pull toward the origin, capture check
`length(position) < rs`) with no awareness that binary mode moves both
masses off the origin (`Physics::BinarySystem::Position1()`/`Position2()`)
— so a spawned body felt a phantom single mass at the (now-empty)
barycenter instead of either of the two actual, visually rendered holes.

Builds on Sessions 1-14; nothing reverted. Both a fresh CPU-only build and
both CUDA build directories (`build/`, `build_cuda/`) compile clean. Test
counts unchanged (126/126 CPU-only, 144/144 CUDA) — this was a behavior
fix, not new test surface, beyond hardening one pre-existing flaky test
noticed while re-running the suite repeatedly during verification (see
below).

### What's new

- **`Physics::PointMassGravityAccelSim`** (new, `src/Physics/BlackHole.h`):
  `StaticObserverGravityAccelSim`'s formula generalized to take an
  arbitrary `gmSim`/`rsSim` instead of hardcoding `1.0`/`2.0` --
  `StaticObserverGravityAccelSim` itself is now just
  `PointMassGravityAccelSim(position, 1.0, SchwarzschildRadiusSim())`, so
  the single-hole path's behavior is provably unchanged (same formula,
  same constants, just factored).
- **`Physics::BinarySystem::GravityAccelSim`/`IsCaptured`** (new,
  `src/Physics/BinarySystem.h`): sum/check both bodies' own
  `PointMassGravityAccelSim` pull at their own current positions -- the
  same "doubled Newtonian" superposition `BinaryGeodesicSolver` already
  uses for photons, now available for massive test particles too.
- **`StarBody::Update`** and **`PlanetBody::Update`/`UpdateGPU`** all gained
  an optional `const Physics::BinarySystem* binary = nullptr` parameter;
  when non-null, gravity and the horizon check go through the binary-aware
  path instead of the single-hole one. `App.cpp`'s main loop now builds
  `activeBinary = binaryModeEnabled ? &binarySystem : nullptr` once per
  frame and passes it to every star/planet update call.
- **`PlanetBody::UpdateGPU(..., binary)` returns `false` immediately when
  `binary` is non-null** -- the GPU kernel only knows the single-hole law,
  and extending `cuda/planet_kernel.cu` to a binary-aware kernel was
  scoped out of this fix (urgent correctness fix, not a GPU feature
  session); rather than silently running the wrong physics fast, binary-
  mode planets now always fall back to the (correct, binary-aware) CPU
  `Update()` path via the existing automatic-fallback mechanism -- no new
  code needed there, it already tries GPU first and falls back on `false`.
- **Hardened a pre-existing flaky test** noticed while re-running the
  suite ~10x to verify this fix didn't regress anything:
  `tests/test_planet.cpp`'s self-gravity reaccumulation check (added
  Session 14) compared only the two LARGEST fragments' pairwise
  separation from a single random trial each way, which occasionally
  failed (~1 run in 5) because gravitational few-body dynamics among
  several fragments are genuinely chaotic -- a close encounter can
  occasionally fling a fragment outward even with self-gravity active,
  which is real physics, not a bug. Replaced with a mass-weighted RMS
  spread across ALL surviving fragments (less sensitive to which specific
  two happen to be largest), averaged over 20 trials each way, with a
  looser (0.85x) margin and a comment explaining why individual trials are
  expected to vary. Confirmed via 8+ consecutive clean runs after the
  change (was failing roughly 1 in 3-5 runs before).

### Decisions worth knowing about

- This is scoped as a correctness fix for stars/planets ONLY -- the ray
  tracer's own binary photon handling (`BinaryGeodesicSolver`) was already
  correct and untouched. The bug was specifically that `StarBody`/
  `PlanetBody` never knew binary mode existed at all.
- Binary-mode planets are CPU-only until/unless a binary-aware GPU kernel
  is written later (see `docs/physics.md` §11's new paragraph) -- flagged
  explicitly rather than silently degrading the particle-count ceiling
  back to 250 without explanation.
- Kerr spin still isn't accounted for in star/planet gravity even in
  single-hole mode (both still use the non-rotating Schwarzschild law
  regardless of the configured spin) -- not part of this fix, listed here
  so it isn't confused with the binary case just resolved.

### What needs attention

- [ ] No binary-aware GPU kernel for planets (see above) -- if someone
      wants high-particle-count planets in binary mode specifically, this
      is the gap to close.
- [ ] Kerr-aware gravity/capture for stars/planets remains unimplemented
      (single, non-rotating Schwarzschild law always used, regardless of
      configured spin) -- a separate, still-open gap from the binary one
      just fixed.

## Session 16 — Freely-placed extra black holes ("spawn anything anywhere," Universe-Sandbox-style)

User asked to be able to "spawn anything anywhere" with the black holes,
"like Universe Sandbox." Scoped via plan mode first, confirmed three
things with the user before building: placement works by flying the
existing free-fly camera to a spot and hitting "spawn here" (not numeric
fields, not viewport click-picking); spawned objects stay test-particle
physics (no mutual N-body gravity, matching everything built so far); and
the new spawnable type is extra black holes, freely placed rather than
locked to Binary mode's circular orbit.

This is the largest architectural change so far — it touches the ray
tracer's core dispatch, star/planet gravity, per-body accretion disks, and
debug-view rendering. Scoped deliberately (CPU-only lensing for the new
multi-body case, 4-extra-black-hole cap, fully additive) so it shipped as
one solid milestone rather than an open-ended rewrite — same spirit as
the planetary-destruction and GPU-planet milestones. **Every existing
mode (single Schwarzschild, Kerr spin, the exact 2-body Binary orbit) is
left completely unchanged** — the new path only activates once 3+ bodies
are actually in the scene.

Builds on Sessions 1-15; nothing reverted. A fresh CPU-only build and both
`build/`/`build_cuda/` (CUDA) compile clean. Test counts: **132/132
CPU-only (up from 126 — +6 multi-body tests, unconditional, not CUDA-
gated) / 150/150 CUDA (up from 144)**.

### What's new

- **`Physics::ExtraBlackHole`** (new, `src/Physics/MultiBlackHoleSystem.h`):
  a freely-placed point mass — `gmRatio` (GM relative to the primary's
  GM=1), `position`, `velocity` (constant-drift; defaults to 0, i.e. stays
  put — nothing pulls on it, consistent with every other spawned object
  being a test particle). Capped at 4 in the UI; `kMaxLensingBodies = 6`
  (1 primary + 1 binary-second-body + 4 extras) bounds every fixed-size
  array this feature touches.
- **`Physics::TracePhotonMultiBody`** (new, `src/Physics/
  MultiBodyGeodesicSolver.h/.cpp`, CPU only): generalizes
  `BinaryGeodesicSolver.cpp`'s exactly-two-named-bodies scheme to a loop
  over up to `kMaxLensingBodies` bodies — same doubled-Newtonian
  superposition, same frozen-lens/no-exact-strong-field caveats, a
  genuinely independent implementation (not a refactor of
  `TracePhotonBinary`), confirmed to reduce to `TracePhotonBinary`'s own
  result for an equivalent 2-body configuration
  (`tests/test_multibody.cpp`). `RayTracing::CPUTracer.cpp` gained
  `BuildLensingBodies` (builds the active body list once per `RenderImage`
  call, not per pixel) and now dispatches to this solver whenever 3+
  bodies are active, leaving the existing binary/Kerr/Schwarzschild chain
  completely untouched for 1-2 bodies.
- **A real bug, caught by the reduction test, not code review:**
  `BuildLensingBodies` (and this session's first version of
  `test_multibody.cpp`) assumed the primary black hole sits at the world
  origin even when binary mode is ALSO active — `BinarySystem::Position1()`
  shows it does not; body 1 orbits the shared barycenter right alongside
  body 2. Fixed in both places once the "does this reduce to
  `TracePhotonBinary`'s own result" check caught the mismatch (closest
  approach 1.96 vs. an expected 3.65, fate disagreeing) — exactly the kind
  of regression a from-scratch reimplementation needs this kind of
  cross-check to catch.
- **GPU: explicitly not implemented for multi-body lensing.**
  `cuda/geodesic_kernel.cu` is untouched. `App.cpp`'s `PerformRender` now
  computes `useGpuThisFrame = useGpu && cudaDeviceAvailable &&
  extraBlackHoles.empty()` — any frame with extra black holes placed falls
  back to the CPU tracer rather than silently running the wrong physics
  fast, same precedent `PlanetBody::UpdateGPU` already set for binary mode.
- **`Physics::CombinedGravityAccelSim`/`IsCapturedByAny`** (new,
  `MultiBlackHoleSystem.h`): shared helpers so `StarBody::Update` and
  `PlanetBody::Update`/`UpdateGPU` don't each reimplement "sum gravity
  across whichever of binary/extras are active." Both gained a parallel
  `extraBlackHoles` parameter alongside the existing `binary` one from
  Session 15's fix — both can be active at once. `PlanetBody::UpdateGPU`
  falls back to CPU whenever any extra black holes are placed, same
  mechanism as binary mode.
- **Per-extra-black-hole accretion disks**: generalizes the existing
  `accretionDisk`/`accretionDisk2` pattern (binary's second body already
  gets its own disk instance, radii scaled by its own `rs` ratio) to a
  `std::vector<Astronomy::AccretionDisk>` sized to the live extra-black-
  hole list, rebuilt the same way each frame; the disk sampler now looks
  up the right instance by `diskBodyIndex` through a small per-frame
  pointer list instead of a 0/1 ternary.
- **Debug 3D view**: event-horizon spheres and Flamm's paraboloid funnels
  now also draw once per extra black hole (magenta-ish color to
  distinguish from the primary's red / binary's red-blue), additive to
  the existing single/binary drawing blocks rather than replacing them.
  Photon-sphere overlay now also skips whenever extra black holes are
  placed (same "no exact formula" reasoning binary mode already used).
- **UI**: new Advanced-panel "Extra Black Holes" section — mass slider,
  "Add black hole here" (spawns at `camera.Position() + camera.Forward()
  * spawnDistanceFromCamera`, reusing `Rendering::Camera`'s already-public
  methods, no new picking code needed), a list with per-item remove
  buttons, capped at 4. New "Add Star Here"/"Add Planet Here" buttons
  (Simple and Advanced panels) alongside the existing random-orbit
  spawners — camera-position-based, with a launch-speed slider (0 = just
  drop it) in Advanced mode. All fully additive: the existing radius/
  orbital-speed-fraction controls and all 5 planet presets are untouched.
- **`tests/test_multibody.cpp`** (new, 6 checks, unconditional — not
  CUDA-gated): the binary-reduction check described above; capture against
  a non-primary body; disk-hit attribution to the correct body index; a
  star falling into and captured by an extra black hole instead of the
  (much farther) primary.
- **`docs/physics.md`**: new §12 ("Freely-placed extra black holes")
  between Binary (§11) and Stars/planets (now §13, was §12); Limitations
  (now §14) gained a line for extra black holes' own gaps.
  **`docs/validation.md`**: new §11 for the multi-body tests (old §11+
  renumbered up by one); summary table updated to 132/150.

### Decisions worth knowing about

- Binary mode's own circular-orbit UI/physics is completely untouched —
  extra black holes are a parallel, independent concept (free placement,
  no orbit constraint), not a replacement or a generalization of
  `BinarySystem` itself. They can both be active at once (primary +
  binary's second body + extras all lens together once 3+ total).
- The reduction test (multi-body-with-2-bodies vs. binary) is the load-
  bearing regression guard for this whole feature — it's what caught the
  primary-position bug, and it's what gives confidence the new solver's
  physics is right without needing to re-derive/re-validate the
  doubled-Newtonian scheme from scratch (that validation already exists,
  for binary mode, in `tests/test_binary.cpp` and `docs/physics.md` §11).

### What needs attention

- [ ] No GPU kernel for multi-body lensing (see above) -- if someone wants
      extra black holes to render at full framerate, this is the gap to
      close, likely following the exact same pattern used for the Kerr/
      binary kernels (bounded array passed by value, same persistent-
      buffer trick).
- [ ] Extra black holes can't carry Kerr spin, and don't interact with
      binary mode's orbit (they're independent, static-by-default point
      masses) -- consistent with the existing "binary/multi-body mode
      can't carry spin" precedent, not a new gap.
- [ ] No UI control to give an extra black hole a nonzero drift velocity
      after spawning (the `ExtraBlackHole::velocity` field and `Advance()`
      support it; nothing in the UI sets it to anything but zero yet).
- [ ] Mutual gravity between spawned objects (including between black
      holes themselves) remains explicitly out of scope, confirmed with
      the user up front -- not a gap, a deliberate choice.
      **(Reversed in Session 17, for stars/planets only -- black holes
      still stay fixed/scripted.)**

## Session 17 — Mutual gravity between spawned stars/planets ("binary star system" conditions)

User asked for every spawned object to have its own gravitational field,
"like realism," so planets/stars can form binary-star-like systems --
explicitly reversing Session 16's confirmed scope decision that spawned
objects would stay test particles with no mutual gravity. Scoped via plan
mode first, confirmed two things with the user before building: mutual
gravity applies at the **object level** (each star/planet reduced to one
point mass at its own center, not full particle-to-particle N-body), and
**black holes stay exactly as they are** -- fixed/scripted gravity sources,
pulled by nothing, not even each other.

Builds on Sessions 1-16; nothing reverted except the scope line above.
Off by default -- every existing scene/preset behaves identically unless
the new "Mutual Gravity" panel is explicitly enabled. A fresh CPU-only
build and both `build/`/`build_cuda/` (CUDA) compile clean. Test counts:
**136/136 CPU-only (up from 132 -- +4 mutual-gravity tests, unconditional,
not CUDA-gated) / 154/154 CUDA (up from 150)**.

### What's new

- **`Physics::GravitySource`** (new, `src/Physics/GravitySource.h`):
  `{ glm::dvec3 position; double massKg; }` -- deliberately lighter than
  `ExtraBlackHole`: no Schwarzschild radius, no capture check (two
  spawned objects passing close isn't an event horizon, and
  collision/merger response is out of scope). Same file's
  `MutualGravityAccelSim` sums a tuned sim-unit force law (not the real,
  invisibly-small physics -- same reasoning as `PlanetBody`'s own
  self-gravity) across every source, scaled by a strength parameter.
- **Stars have a mass for the first time**: `StarBodySettings` gained
  `totalMassKg` (default 1 solar mass). `StarBody`/`PlanetBody` both
  gained `CenterOfMass()`/`CurrentMassKg()`-style accessors (planet's
  version exact, from its already-tracked per-particle masses; star's
  version scaled by live/total particle-count fraction, same convention
  used elsewhere for "how much of this body is left").
- **`StarBody::Update` and `PlanetBody::Update`/`UpdateGPU`** each gained
  `mutualGravitySources`/`mutualGravityStrength` parameters, summed into
  `Physics::CombinedGravityAccelSim` alongside the existing binary/extra-
  black-hole terms. `PlanetBody::UpdateGPU` falls back to CPU whenever
  mutual sources are supplied, non-empty -- same precedent binary mode and
  extra black holes already established.
- **`App.cpp` wiring**: each frame, before updating any star/planet,
  builds small per-object filtered `GravitySource` lists (every other live
  star/planet, excluding self) and passes them through. New "Mutual
  Gravity" Advanced-panel section (enable checkbox, default off + strength
  slider) and a new "Mass (solar masses)" slider next to the star spawn
  controls (stars never had a configurable mass before).
- **Empirical tuning found a real problem, not just a number to pick.**
  The first probe pass referenced Earth mass as `MutualGravityAccelSim`'s
  mass ratio denominator (mirroring `PlanetBody`'s self-gravity exactly).
  Every single coefficient/min-distance combination tried against that
  reference flung a pair of 1-solar-mass test stars apart to separations
  of 10⁴-10⁶ sim units within a few hundred/thousand steps -- including
  the *zero-tangential-velocity, pure-infall* case, which should merely
  oscillate. Root cause: a solar mass is ~3.3×10⁵ Earth masses, so even
  `coefficient=1` produced a per-step velocity kick many times the
  objects' own separation at ordinary spawn distances/timesteps -- pure
  numerical blow-up, not a physics bug. Switching the reference mass to
  one solar mass (stars are the primary use case) fixed every combination
  immediately; re-swept from there and picked `kMutualGravityCoefficient
  = 1.5`, `kMutualGravityMinDistance = 3.0` for producing a visible
  spiral-together/swing-past/spread-back-out close encounter (confirmed by
  tracking orbital angle swept, not just separation) rather than a stable
  closed orbit (not expected from a softened inverse-square law under
  semi-implicit Euler) or an ejection. A pair left ~500 units apart stayed
  essentially motionless, confirming distant/unrelated objects aren't
  destabilized.
- **`tests/test_mutual_gravity.cpp`** (new, 4 checks, unconditional -- not
  CUDA-gated): mutual-gravity-on decreases separation, mutual-gravity-off
  leaves it exactly unchanged; a star measurably accelerates toward a
  10×-more-massive companion; `PlanetBody::UpdateGPU` falls back to CPU
  when mutual sources are supplied. Both dynamics tests place their stars
  ~10⁶ sim units from the primary black hole specifically to make its own
  always-on pull negligible -- an earlier draft at a more modest distance
  saw spurious drift (and a swamped signal in the second check) from nothing
  more than `Physics::CombinedGravityAccelSim`'s unconditional primary term,
  not a mutual-gravity bug. Also caught, mid-test-writing: `StarBody` still
  scatters a single particle to a random point *within* `initialRadius`
  even at `particleCount=1`, so a "small" nonzero radius (0.01, used
  elsewhere for single-particle determinism) was enough jitter to break an
  exact-separation assertion -- fixed by using `initialRadius=0.0` for
  these specific tests, not a change to `StarBody` itself.
- **`docs/physics.md`**: new §14 ("Mutual gravity between spawned
  stars/planets") between Stars/planets (§13) and Limitations (now §15);
  Limitations' stars/planets and extra-black-holes bullets both updated to
  reflect the new, still-excludes-black-holes reality.
  **`docs/validation.md`**: new §14 for the mutual-gravity tests (old §14
  Summary renumbered to §15); summary table updated to 136/154.

### Decisions worth knowing about

- This explicitly reverses part of Session 16's confirmed scope (mutual
  gravity was declared out of scope there) -- the user asked for it
  directly this session. Black holes staying fixed/scripted is NOT
  reversed; that half of Session 16's decision still holds, reconfirmed
  with the user before implementation.
- Object-level only, confirmed with the user: every particle of every star
  pulling on every other particle of every other object was explicitly
  declined as unnecessary cost/fragility for a visual effect this size.
- The reference-mass bug (Earth mass -> universal ejection) is the load-
  bearing lesson of this session, in the same family as Session 14's
  spring-force-divided-by-real-mass bug and the self-gravity collapse
  fix: whenever a tuned sim-unit force law's scale depends on a mass
  *ratio*, the choice of reference mass isn't a free aesthetic pick --
  get it orders of magnitude off and no coefficient/softening combination
  will produce stable behavior, because the instability is really a
  per-step-kick-vs-separation integrator problem hiding behind what looks
  like a tuning search.

### What needs attention

- [ ] No GPU kernel for mutual gravity (see `PlanetBody::UpdateGPU`'s
      fallback above) -- same gap class as multi-body lensing's CPU-only
      status (Session 16); likely follow-up if this ever needs to scale
      past a handful of objects.
- [ ] No collision/merger response when two objects' centers pass very
      close together -- the minimum-distance softening is purely a
      numerical safety floor, confirmed out of scope with the user.
- [ ] The tuned encounter is a visible close pass, not a stable closed
      orbit -- two stars given a tangential kick will spiral in, swing
      past each other, and spread back out, not settle into a repeating
      ellipse. This matches what a softened (not true) inverse-square law
      integrated with semi-implicit Euler actually produces; getting an
      actually-closed orbit would need either a real (undamped, un-
      softened at relevant scales) inverse-square law -- reintroducing the
      divergence risk this session's tuning specifically avoided -- or a
      different integrator, neither of which the user has asked for.

## Session 18 — Universe / Star Map: real catalog travel ("No Man's Sky, but real")

User asked for a No Man's Sky-style ability to travel between star systems,
explicitly scoped to stay realistic/real-world rather than becoming a fully
procedural game. Scoped with the user up front into three decisions before
writing any code: (1) real star/black-hole/galaxy catalog + travel, not a
procedural galaxy generator; (2) space travel and orbital viewing only, no
on-foot/surface exploration; (3) the existing black hole (TON 618 by
default) becomes one destination among many, not a fixed centerpiece.
Builds on all prior sessions; nothing reverted. `build.bat` builds clean (no
new warnings), full suite now includes a new Universe test group (0
failures across the whole suite, confirmed by an actual rebuild + test run,
not assumed).

### What's new

- **`src/Universe/StarCatalog.h`** (new, header-only): ~35 real catalog
  entries -- nearby/bright real stars (Alpha Centauri, Sirius, Proxima
  Centauri, Vega, Betelgeuse, Rigel, Polaris, etc., with real RA/Dec/
  distance/spectral-type/mass/temperature), real black holes (Cygnus X-1,
  Sagittarius A*, M87*, TON 618 -- the app's original default, now one
  catalog entry rather than a hardcoded constant), and real galaxies
  (Andromeda, Triangulum, the Magellanic Clouds, Whirlpool, Centaurus A) for
  distance context. `CatalogObject::CartesianLy()` converts each entry's
  real RA/Dec/distance into a shared heliocentric Cartesian frame (in
  light-years, NOT simulation units -- see below) purely so real distances
  between ANY two catalog entries (not just from Sol) can be computed.
  Figures are standard published values, not research-grade astrometry --
  documented inline, including which distances (supergiants) carry large
  real uncertainty and which black hole spins are genuinely unmeasured
  (`kUnknownSpin` sentinel) versus independently measured (Cygnus X-1,
  M87*).
- **`src/Astronomy/Blackbody.h`** (new): `ApproximateBlackbodyHue`, factored
  out of `AccretionDisk.cpp` (where it originated, unchanged) so the new
  Star System View (below) reuses the exact same temperature-to-color
  approximation instead of a second, independently-drifting color ramp.
  `AccretionDisk.cpp` now includes it instead of defining its own copy --
  confirmed behaviorally identical (same keypoints, same log-scale lerp),
  not just moved.
- **"Universe / Star Map" Advanced-mode panel** (`App.cpp`, inserted
  between "Mutual Gravity" and "Scenario"): a scrollable list of every
  catalog entry, current-location readout, selected entry's real distance/
  mass/temperature/notes, and a "Travel Here" button. Distance is always
  shown with its light-travel-time equivalent.
- **Travel is an explicitly-labeled instantaneous narrative jump, not
  simulated faster-than-light flight** -- a deliberate honesty choice,
  consistent with this project's existing refusal to silently misrepresent
  physics (e.g. the jet's relativistic speed cap, Session 2). This app's
  camera/physics have no mechanism for continuously flying across
  light-year distances (sim units are relative to whichever black hole is
  configured, see `Physics/Constants.h`), so "travel" re-points the
  existing one-scene app at a different real object instead, and the UI
  always prints the real light-travel-time the jump represents right next
  to the button that performs it.
  - Traveling to a **BlackHoleObj** entry calls the exact same
    `blackHole.SetMassSolarMasses`/`SetSpin` the mass/spin sliders already
    call -- travel is just catalog-driven slider-setting, not a new
    rendering path. TON 618 is no longer special-cased as the hardcoded
    default; it's simply the catalog entry the sliders start on.
  - Traveling to a **Star** entry switches to a new, deliberately simple
    **Star System View** (`App.cpp`'s main render branch, guarded by a new
    `inStarSystemView` flag): this app's ray tracer only ever models black
    holes (null geodesics through a Schwarzschild/Kerr metric), so there is
    no equivalent GR renderer for an ordinary star. Rather than silently
    mislabeling a black-hole render as "visiting a star," Star System View
    is explicit about being a stand-in: the star drawn as a plain, unlit
    (self-luminous, not Lambertian-shaded) sphere colored via
    `ApproximateBlackbodyHue(star.temperatureK)`, on its own fixed,
    arbitrary visual scale completely independent of whichever black
    hole's `GM/c^2` sim-unit scale happens to be configured -- documented
    inline specifically so a future session doesn't try to reconcile the
    two scales. **Galaxy entries are not travel targets** (too far/too
    extended for this app's single-point-source renderer) -- selecting one
    disables the Travel button and shows why.
  - The HUD compass (Session 2) now labels the origin POI by the visited
    star's real name instead of unconditionally "Black Hole" while in Star
    System View, and shows its bearing distance in plain visual-scale units
    instead of a real-meters conversion (which would be meaningless at that
    arbitrary scale) -- found and fixed while wiring this in, not a
    pre-existing bug.
- **`tests/test_universe.cpp`** (new, 6 checks via `TestFramework`):
  `CartesianLy()`'s RA/Dec->Cartesian formula against two independent
  checks (equatorial RA=0 lands on +X; the real near-polar star Polaris
  lands almost entirely on the polar axis), a universal magnitude-equals-
  listed-distance check across every catalog entry at once, and two
  real-world sanity checks that would catch a transposed/garbled data
  field without hardcoding every digit (Alpha Centauri closer than Sirius;
  Sagittarius A* far less massive than TON 618) -- deliberately validating
  against real relative facts, same spirit as the project's existing
  physics tests validating against closed-form results rather than
  memorized numbers alone.

### Decisions worth knowing about (additions to the running list)

- **Catalog data lives in one header, not a database/file format.** ~35
  entries is small enough that a `BuildCatalog()` function returning a
  `std::vector` is simpler than adding a data-loading layer; revisit if the
  catalog grows to hundreds+ of entries.
- **No continuous interstellar flight.** This was discussed with the user
  up front (see the three scoping decisions above) -- "travel" deliberately
  re-points the scene rather than attempting to simulate crossing
  light-years at sub-light speed in real time, which would take the jet's
  existing 0.2c-capped flight model literal *years* of wall-clock time to
  honor. Don't "fix" this into continuous flight without discussing the
  implied time-compression tradeoff with the user first.
- **Star System View has no orbiting planets, moons, or any other body.**
  Deliberately out of scope for this session (matches the "space travel +
  orbital viewing only" scoping decision, not surface exploration) -- it's
  a single sphere plus real informational text, nothing more yet.

### What needs attention

- [ ] Only ~35 catalog entries exist -- real astronomical catalogs (Gaia,
      Hipparcos) have far more; this session deliberately hand-picked a
      small, real, well-known set rather than importing a bulk dataset.
- [ ] Star System View's visual scale (camera distance ~25-30 units from a
      radius-6 sphere) was chosen to "look reasonable," not derived from
      any real star's actual angular size -- there's no real-to-visual
      scale mapping documented or intended here, unlike every other
      distance/mass figure in this session.
- [ ] No automated test exercises the new UI panel or the Star System View
      render branch itself (same category of gap as every other UI-only
      addition in this project, e.g. Sessions 6-7) -- `test_universe.cpp`
      covers the underlying catalog/coordinate data only.
- [ ] Not verified by actually driving the app (this environment's known
      UI-automation limitation, see Session 1) -- verified by a clean
      rebuild and the full test suite passing (confirmed 0 failures) only.

### Addendum (same session) — real exoplanets in Star System View

Follow-on, same session: Star System View (above) originally drew an empty
star with nothing around it. Added real, cataloged exoplanets so arriving
somewhere shows something.

- **`src/Universe/ExoplanetCatalog.h`** (new): a small, deliberately real
  set of confirmed/candidate exoplanets -- Proxima Centauri b, Ross 128 b,
  Barnard's Star b (flagged `detectionDisputed`, since its 2018 detection
  has genuinely been contested in later literature -- not a blanket hedge
  on every entry), Epsilon Eridani b, Pollux b, and two Tau Ceti candidates
  (e, g) -- with real semi-major axes and masses. Most `StarCatalog.h`
  stars have NO entries here, deliberately: a star with no real detection
  gets no planet, rather than a plausible-looking invented one (Star System
  View's info panel says so explicitly, "No exoplanets confirmed here,"
  when that's the case).
- **`OrbitalPeriodDays(semiMajorAxisAu, hostMassSolar)`**: real Kepler's-
  third-law period in days, `P[years] = sqrt(a[AU]^3 / M[Msun])`. Used
  instead of hardcoding each planet's published period so the catalog's
  star masses and planet semi-major axes are forced to stay mutually
  consistent with real physics -- cross-checked in `test_universe.cpp`
  against all 5 of this session's listed planets' real published periods
  (within 10%, since the catalog's star masses are rounded published
  figures, not each discovery paper's own exact value) and genuinely
  passed, not tuned to pass: Proxima Centauri b 11.26 vs. real 11.19 days,
  Ross 128 b 9.79 vs. 9.9, Epsilon Eridani b 2618 vs. 2691, Tau Ceti e
  163.2 vs. 162.9, Tau Ceti g 20.06 vs. 20.0.
- **Star System View now draws each listed planet** on a circular orbit
  whose RELATIVE angular speed is real (a closer-in planet orbits faster
  than a farther one, driven directly by `OrbitalPeriodDays`) -- what's
  NOT real, and documented inline as such: the absolute timescale
  (compressed so the outermost listed planet in a system completes an
  orbit in 24 real seconds, not days/years -- otherwise nothing would
  visibly move) and the orbital radii (spaced by relative semi-major axis
  only, scaled to fit near the star's already-not-to-scale render size,
  same reasoning StarCatalog.h's render branch already uses for the star
  itself). Planets are flat-colored by kind (gas giant vs. rocky, no
  per-planet albedo data exists to do better) with a faint orbit-path
  ring, reusing `Renderer::DrawPath`.
- **New "Star System" info overlay** (`App.cpp`, shown only in Star System
  View): the visited star's real catalog facts plus each listed planet's
  real semi-major axis/mass/period/notes, including the disputed-detection
  flag where applicable -- makes arriving somewhere informative, not just
  visual.
- **`tests/test_universe.cpp`** gained the 5-planet Kepler cross-check
  above (now 11 checks total in this file). Full suite rebuilt, still 0
  failures.

#### What needs attention (addendum)

- [ ] Only 6 real exoplanets are cataloged -- thousands are confirmed
      across all known systems; this stayed deliberately small and
      hand-picked, same reasoning as the ~35-entry main catalog.
- [ ] Planet visuals (flat color by kind, fixed small size range) carry no
      real albedo/radius data -- there isn't a project-wide per-planet
      dataset backing this the way StarCatalog.h's temperature-driven
      color has one.
- [ ] Same UI-driving caveat as the rest of this session: verified by
      clean rebuild + full test suite only, not by actually watching
      planets orbit in the running app.

### Addendum (same session) — real user-reported bugs after actually using the Star Map

User tried the feature and reported four real problems: couldn't fly while
visiting a place, the piloted Endurance didn't appear after traveling, no
keybind to open the Star Map, and the background starfield vanished (along
with the accretion-disk-style "gas") outside the original TON 618 scene.
Root-caused each rather than guessing:

1. **Star System View had no jet rendering at all.** The jet is only ever
   drawn by the lensed-view's overlay-compositing code (`jetRenderer.Draw`,
   gated on `camera.mode == Jet`); Star System View's render branch never
   called it, so the Endurance was invisible there regardless of position
   -- not a camera-mode bug, a missing draw call. Fixed by drawing it
   directly after `renderer.BeginFrame` in that branch (no separate overlay
   pass needed -- `jetRenderer` manages its own shader/VAO state
   independent of the fixed-function matrix stack `BeginFrame` sets up).
2. **Even once drawn, the jet would have been invisible or nonsensical
   anyway: a scale mismatch.** `jetSpawnPosition` (180 sim units) and the
   capture-reset respawn both assumed the black-hole view's sim-unit scale;
   Star System View uses its own much smaller, independent ~0-25-unit
   visual scale (see that branch's own comment on why). Traveling never
   repositioned the jet, so arriving at a star while piloting left the
   Endurance either far outside Star System View's visible range (coming
   from black-hole scale) or absurdly close to a black hole (coming from
   star-view scale) -- which reads as "I can't fly," since the jet is
   technically still responding to input, just invisible/nowhere near
   anything. Fixed with a new shared `ResetJetForArrival` helper (also used
   by the existing "Pilot Endurance" button) that TravelToUniverseIndex now
   calls with a destination-appropriate position, and the capture-reset
   path (jetPhysics.capturedByBlackHole) now also checks
   `inStarSystemView` instead of unconditionally respawning at the
   black-hole-scale `jetSpawnPosition`.
   - This required moving `TravelToUniverseIndex`'s actual definition
     later in `App::Run` (to after the jet piloting state it now needs to
     reset), since it was originally defined before that state existed.
     The UI code that calls it is unaffected -- a `std::function<void(int)>`
     forward declaration keeps the call sites exactly as they were.
3. **No keybind for the Star Map** -- it was Advanced-mode-only, buried
   mid-panel. Added "M" (edge-detected via a `wasDown` bool, same pattern
   as the jet's existing Tab/C handling) to toggle a standalone window with
   the exact same contents as the Advanced-mode panel -- both now call one
   shared `DrawUniversePanelContents` lambda instead of duplicating the UI,
   so they can't drift apart. Works in Simple mode and mid-flight, not just
   Advanced mode.
4. **Star System View cleared to flat black with no starfield, and no
   "gas"** -- unlike the main ray-traced view, which always samples
   `Astronomy::StarField` per pixel, Star System View never drew ANY
   background at all (not a regression from changing black holes -- it
   genuinely never existed for this view). Fixed with a fixed, once-
   generated (seeded, not re-randomized per frame) scattering of ~1600
   background points colored via the same `ApproximateBlackbodyHue` the
   star/disk rendering already uses -- explicitly a decorative stand-in,
   same spirit as `StarField`'s own "not astronomical data" framing, not a
   claim about real neighboring stars (which StarCatalog.h already
   represents properly, as actual travel targets). Also added the
   requested "gas areas": a real, not invented, circumstellar debris disk
   -- drawn only for the three catalog stars with an actual published one
   (Vega, Fomalhaut, Epsilon Eridani; the latter's is already mentioned in
   its own catalog note) as a faint dust-colored particle ring beyond the
   exoplanet orbits.

Verified by a clean rebuild of both `build.bat` and `build_cuda.bat` (no
warnings) and the full test suite passing in both (0 failures, confirmed
across repeated runs after one apparently transient single-run flake that
didn't reproduce). **Still not verified by actually flying the Endurance in
the running app** -- this environment's UI-automation limitation (Session
1) applies here same as everywhere else; the fixes above are root-caused
from reading the actual code paths involved, not guessed-and-checked
against a running repro.

### Addendum (same session) — the REAL "can't fly" cause, disk-on default, and procedural star textures

The previous addendum's fixes (jet rendering/positioning in Star System
View) were real bugs, but the user reported flight was STILL broken even
near the original black hole (no travel involved) and even after traveling
to a different black hole mass -- meaning something more fundamental than
Star System View was at fault. Root-caused this time with actual numbers,
not another guess:

- **The relativistic speed cap (`Jet::MAX_SPEED_FRACTION_OF_C`, Session
  2) goes to ~6e-7 sim-units/s at TON 618's mass, not just "slow."**
  `FlightPhysics::Update` converts the real-c fraction into sim-units/s via
  `metersPerSimUnit` (the current black hole's `GM/c^2` in meters, see
  Physics/Constants.h); at TON 618 (~6.6e10 solar masses, this app's
  default), `metersPerSimUnit` is ~0.01 light-years, so 0.2c converts to
  roughly 6e-7 sim-units/s. Worked out by hand: `G*M/c^2` for TON 618 is
  ~9.75e13 m, so `0.2*c / 9.75e13 ~ 6.1e-7`. At that cap the jet reaches
  it from its own thrust in microseconds and then sits there -- effectively
  frozen on any human timescale, not merely "slow." This is NOT a bug in
  the sense of contradicting its own documented design (Session 2's
  HANDOFF entry already flagged "genuinely slow (realistically, years)" as
  a known, deliberate tradeoff) -- but a tradeoff whose actual numeric
  severity at the app's own DEFAULT black hole mass had never been worked
  out, and in practice reads as "flight doesn't work at all," not "slow."
  This also explains "even though the mass changed I still can't fly":
  traveling to another sufficiently massive black hole (M87* at 6.5e9
  solar masses, say) hits the same wall, and traveling to a STAR doesn't
  touch `blackHole.MassKg()` at all (Star entries never call
  `SetMassSolarMasses`), so the cap stayed pinned to whatever black hole
  mass was loaded before the trip regardless of destination.
- **Fix: `Jet::MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC` (150 sim-units/s,
  `FlightPhysics.h`/`.cpp`)** -- the sim-units/s cap is now
  `max(relativisticCap, this floor)`. At any stellar/moderate-mass hole
  the relativistic cap is already far above 150, so `max()` changes
  nothing there (verified by the formula: the floor only ever binds when
  `metersPerSimUnit` is large enough that the real-c conversion undercuts
  it, which only happens at supermassive scale). **Explicit, deliberate
  tradeoff, not a quiet one:** this means the jet CAN now exceed the
  literal 0.2c the slider promises once a hole is massive enough -- so a
  new amber warning line was added right under the Advanced-mode speed
  readout (`App.cpp`, Jet panel) whenever the floor is actually binding,
  showing the real c-multiple the floor implies and saying plainly that
  it's a playability concession, not a claim the flight is still
  sub-light. This project's whole ethos is never silently misrepresenting
  its own physics (see e.g. Session 8's deflection-angle honesty, or the
  jet's own original 0.2c cap rationale) -- a quietly-broken promise would
  have repeated that same mistake in the opposite direction.
- **Accretion disk now defaults to ON** (`diskEnabled`, `App.cpp`,
  initialized `true` instead of `false`) -- a fresh launch showed no disk
  at all unless the user found and enabled it, which read as "broken"
  rather than "a quiet/early-type black hole," especially now that travel
  makes jumping straight to a visually-dramatic quasar-mass hole like TON
  618 the very first thing that can happen. The Simple-mode "Black Hole"
  preset still explicitly sets `diskEnabled = false` on click (that
  preset's whole point is the disk-less baseline view, per its own
  blurb) -- this only changes the untouched startup state before any
  preset button is clicked.
- **Procedural star textures, not downloaded ones.** Looked into sourcing
  real NASA/public-domain Sun imagery (a genuine, real option -- NASA SDO
  data exists and this sandbox does have outbound network access,
  confirmed via a test `curl`), but real resolved surface imagery exists
  for essentially no star in the catalog except the Sun -- using one real
  photo for exactly one of ~35 stars and a flat color for the rest would
  be a worse, more misleading inconsistency than using none, and the
  available ready-made "sun texture" assets found via search were either
  not actually NASA-sourced (artist-adjusted, CC-BY-licensed composites
  needing attribution infrastructure this project doesn't have) or not in
  a directly usable projection. Instead: **`Astronomy::GenerateStarTexture`
  (new, `src/Astronomy/StarTexture.h`)** generates a seeded, deterministic
  "granulation" pattern (soft overlapping blobs, U-seam-wrapped so it
  tiles on a sphere) tinted by the star's real effective temperature via
  the existing `ApproximateBlackbodyHue` -- explicitly documented as
  invented surface detail over a real color, never claimed as photographic.
  - **`Rendering::Renderer::DrawTexturedSphere`** (new, `Renderer.h/.cpp`):
    same unit-sphere tessellation as `DrawSphere`, with per-vertex UVs and
    `GL_TEXTURE_2D` binding instead of a flat color -- unlit, same as plain
    `DrawSphere`, since the star is a light source, not a lit object.
  - Star System View generates the texture once per visited star (cached
    by catalog index in function-local statics, not regenerated every
    frame) and uploads it via the existing `UploadImage` (the same
    function the lensed-render path already uses for its RGB-float
    images), then draws the star with `DrawTexturedSphere` instead of a
    flat-colored `DrawSphere`.
- **Real (not hypothetical) perf fix found while touching this code:** the
  lensed-render trigger (`if (viewLensedRender && (liveMode ||
  needsRerender))`, right before `PerformRender()`) never checked
  `inStarSystemView` -- so in Live mode, every single frame spent in Star
  System View was ALSO tracing a full lensed image that view never
  displays, pure wasted GPU/CPU work. Added `!inStarSystemView` to that
  condition.

Verified by a clean rebuild of both `build.bat` and `build_cuda.bat` (no
warnings) and both test suites passing (0 failures). **Still not verified
by actually flying the Endurance in the running app** -- same UI-automation
caveat as every addendum above; the speed-cap fix in particular is
confirmed by hand-computing the actual numbers involved (shown above), not
by observing the jet move on screen.

### Addendum (same session) — Shift/Ctrl throttle with real reverse thrust

User asked directly for Shift (speed up) / Left Ctrl (speed down, including
reversing) instead of the original Shift/Space scheme, which had no
reverse at all (`thrust` floored at 0).

- **`InputHandler::ProcessKeyboard`**: throttle-down moved from Space to
  Ctrl (either Left or Right), and the clamp's floor moved from `0.0f` to
  `-MAX_THRUST * REVERSE_THRUST_FRACTION` (new constant, `FlightPhysics.h`,
  0.3 -- reverse thrust capped at 30% of forward, an arcade choice: enough
  to back out of a bad approach or hold station, not to out-accelerate
  forward flight). `F_thrust = forward * thrust * thrustMultiplier`
  (`FlightPhysics.cpp`, unchanged) already pushes the jet backward for
  free once `thrust` can go negative -- no separate "reverse mode" needed,
  just a wider clamp range.
- **Real bug found and fixed while verifying this would actually work,
  not assumed safe from the clamp change alone:** `UpdateForces`'s
  existing "aerodynamic turn correction" (`FlightPhysics.cpp`) unconditionally
  pulled velocity toward `forward * airSpeed` every frame once airspeed
  exceeded 10 units/s -- a deliberate arcade feature (velocity snaps to
  the nose direction, so a tight turn doesn't leave the ship skidding
  sideways) that, read literally, would have fought every reverse-thrust
  input by continuously re-aligning velocity to POSITIVE forward regardless
  of which way the ship was actually being pushed -- i.e. reverse thrust
  would decelerate the ship and then the turn correction would immediately
  snap it back into forward motion instead of letting it actually back up.
  Fixed by making the correction's target direction follow the sign of
  `thrust`: `forward * (airSpeed * (thrust >= 0 ? 1 : -1))`. Forward flight
  (the overwhelmingly common case) is bit-for-bit unchanged since `thrust`
  is almost always positive there.
- Updated the two places that print the jet's control scheme to the user
  (`App.cpp`'s Jet-mode comment and its Advanced-panel `TextDisabled` control
  summary) from "Shift/Space thrust" to "Shift/Ctrl thrust (Ctrl past zero
  reverses)".

Verified by a clean rebuild of both `build.bat`/`build_cuda.bat` (no
warnings) and both test suites passing (0 failures). The turn-correction
sign fix was reasoned through by tracing what `UpdateForces` actually does
to a negative-thrust frame step by step, not just changed and
assumed-correct -- but, same caveat as the rest of this session, not
confirmed by actually flying backward in the running app.

### Addendum (same session) — Star System View's starfield wasn't a skybox

User reported the background stars in Star System View "disappear" when
flying far enough from a visited star. Real bug, found on inspection: the
previous session's starfield fix (drawing ~1600 procedurally-colored
points) placed each point at a FIXED WORLD position, `direction * 450.0`,
centered on the ORIGIN (the star) -- not re-centered on the camera every
frame. That's not a skybox, it's a 450-unit-radius cluster of points
sitting at the star; flying far enough away (easy at the jet's cruise
speeds, or just orbiting a planet at the outer edge of the system) leaves
the entire "sky" behind, exactly as reported.

Fixed by recomputing each point's position as `camera.Position() +
direction * 450.0` every frame instead of a one-time `direction * 450.0`
-- the direction and color per star are still generated once (same static,
seeded vector as before, so the pattern itself doesn't re-randomize every
frame), only the recentering is now per-frame. This is the standard way any
engine draws a skybox (always same angular size/position relative to the
viewer, however far they've traveled) -- the bug was never translating the
existing points, not a deeper design flaw.

Verified by a clean rebuild of both `build.bat`/`build_cuda.bat` (no
warnings) and both test suites passing (0 failures). Not confirmed by
actually flying far from a star and watching the sky stay put -- same
UI-automation caveat as the rest of this session; the fix is a direct,
mechanical read of why the old code could only ever produce a
origin-centered cluster, not a skybox.

## Session 19 — Multi-star systems (2-body, 3-body, N-body), real N-body integration

User asked directly for 2-body/3-body/multi-body star system options on the
Star Map. Builds on everything above; nothing reverted. Both
`build.bat`/`build_cuda.bat` build clean (no warnings) and both test
suites pass (0 failures, new test group included).

### What's new

- **`src/Universe/MultiStarSystem.h`** (new): `MultiStarSystemEntry`
  (name, real sky position if applicable, a list of `StarMember`
  {name, massSolar, temperatureK}, a `MultiStarKind` of TwoBody/
  ThreeBody/NBody, and an `isRealSystem` flag). Two REAL builders --
  `BuildAlphaCentauriBinary()` (the nearest star system's real inner pair,
  real masses, ~23.5 AU/~79.9-year real orbit) and
  `BuildAlphaCentauriTriple()` (the same system plus its real, genuinely
  bound but extraordinarily distant third member, Proxima Centauri --
  ~13,000 AU out, ~500,000+-year period, substantial real orbital
  uncertainty) -- and one procedural generator, `GenerateProceduralSystem
  (starCount, seed)`, for anything else: masses drawn from a simplified
  plausible range (not a specific real system -- precisely-measured real
  bound systems of 4+ comparably-separated stars are rare and not
  something to fabricate specific numbers for from memory), temperatures
  from a simplified real mass-temperature power law (`T ~ 5778 * M^0.54`).
  Deterministic by seed (same seed -> same system, tested).
- **A genuinely real N-body integration, not a scripted animation.**
  `App::Run`'s per-frame update gained a new block (guarded by
  `inMultiStarView && !paused`) that computes every member's gravitational
  pull on every OTHER member using the exact same tuned mutual-gravity law
  (`Physics::MutualGravityAccelSim`, `Physics/GravitySource.h`) the
  existing "Mutual Gravity" star/planet feature already uses and has
  validated (two 1-solar-mass bodies spiral together, swing past, and
  spread back out over tens of sim-time units) -- reused as-is, not
  retuned, so a 2-body system here settles into an ordinary bound orbit
  while a 3+-body system is genuinely chaotic (no general closed-form
  solution exists for 3+ mutually gravitating bodies -- this IS "the
  three-body problem," not a simplification of an available exact
  answer). Two explicit passes per step (every acceleration computed from
  pre-step positions, THEN every velocity/position update applied) so the
  result doesn't depend on member iteration order.
- **`TravelToMultiStarSystem`** (`App.cpp`, same forward-declared-
  `std::function` pattern as `TravelToUniverseIndex`, for the same reason
  -- it needs the jet piloting state defined later in `Run()`): lays out
  the system's members on a circle (arbitrary visual scale, same spirit as
  Star System View's own), then shifts every position by the MASS-WEIGHTED
  centroid so the real barycenter condition (`sum(m_i * r_i) = 0`) holds
  exactly regardless of that arbitrary starting shape, gives each member a
  tangential kick sized from its own local mutual-gravity acceleration
  (a circular-orbit estimate, not an exact N-body solution -- none exists
  for N>=3), and zeroes the mass-weighted mean velocity so the system's
  barycenter doesn't drift off-screen. Generates one real-temperature-
  tinted procedural texture per member up front (`Astronomy::
  GenerateStarTexture`, Session 18) rather than lazily, since a variable-
  length member list doesn't fit Star System View's single-texture lazy
  cache.
- **New Multi-Star System View render branch** (`App.cpp`, checked before
  the existing single-star Star System View, mutually exclusive with it):
  shares the same procedural background starfield Star System View uses
  (factored out into a new `DrawBackgroundStarfield` lambda specifically
  so both views stay in sync rather than drifting into two copies), draws
  each member as a `DrawTexturedSphere` sized by the cube root of its mass
  (a real star's mass-radius relation is sub-linear, this is NOT that
  exact relation, just the same "heavier reads as bigger" spirit), and
  composites the piloted jet on top exactly like the other two render
  branches already do.
- **"Current location" tracking generalized.** `universeLocationIndex`
  (an index into `StarCatalog.h`'s single-object catalog) can't represent
  "I'm at a multi-star system," so real "where am I" state moved to new
  `currentLocationLy`/`currentLocationName` variables, updated by BOTH
  `TravelToUniverseIndex` and `TravelToMultiStarSystem` -- real multi-star
  travel (Alpha Centauri) quotes a real distance/light-travel-time exactly
  like any other real destination; a procedural system instead says
  plainly that materializing one isn't a real travel jump (it has no real
  sky position).
- **Universe/Star Map panel** (`App.cpp`'s `DrawUniversePanelContents`,
  reachable both via Advanced mode and the "M" popup, Session 18): a new
  "Multi-Star Systems" section with two real-system buttons, a star-count
  slider (2-8) for the generator, a "Generate & Travel" button (seed
  auto-increments per click so repeated clicks don't reproduce the same
  system), and -- while in a multi-star system -- a live listing of every
  member's real mass/temperature.
- **`tests/test_multistar.cpp`** (new, ~11 checks): both real builders'
  member counts/kind/real-data sanity (Alpha Centauri A really is more
  massive than B; the triple really does include Proxima); the procedural
  generator's count/kind classification across 2/3/6-star cases, its
  seed-determinism (same seed -> bit-identical masses), and a sanity bound
  on every generated temperature; and a direct mutual-gravity-direction
  check (two stars pull toward, not away from, each other) independent of
  App.cpp's own integration loop.
- Compass (Session 2) and the HUD distance-label special-casing (Session
  18's Star System View fix) both extended to cover `inMultiStarView`:
  every member gets its own (moving) compass entry instead of one fixed
  origin POI, and distances show in plain visual-scale units rather than a
  meaningless real-meters conversion.

### Decisions worth knowing about

- **No closed-form shortcut for 3+ bodies, by design, not a missing
  feature.** This is the actual, long-unsolved three/N-body problem --
  there genuinely is no general closed-form orbit to fall back on, so
  real-time numerical integration is the only correct way to show one.
  Don't "fix" the chaos into a stable scripted orbit; it's the real
  physics, not a bug.
- **The procedural generator's mass range (0.15-2.5 solar masses, log-
  uniform-ish via `std::uniform_real_distribution`) and mass-temperature
  power law are simplified, not a real initial-mass-function sample or a
  full stellar-structure model** -- documented inline in
  `MultiStarSystem.h`, same honesty standard as every other approximation
  in `Universe/`.
- **Initial velocities are a local circular-orbit ESTIMATE, not an exact
  solution.** For N=2 this happens to be very close to exact; for N>=3 it's
  just a reasonable starting condition for the chaos to evolve from, same
  spirit as the binary black hole mode's "frozen lens" approximation
  (Session 9) being labeled exactly as approximate as it is.

### What needs attention

- [ ] Not verified by actually traveling to/watching a multi-star system
      evolve in the running app -- same UI-automation limitation as every
      other addendum this session and the last; verified by a clean
      rebuild and both test suites passing (0 failures) only.
- [ ] No collision/escape handling: if the chaotic integration throws a
      member very close to another or flings one far away, there's no
      special-case response (no merge, no "ejected" state) -- it just
      keeps integrating the (possibly now-extreme) positions/velocities.
      `Physics::GravitySource`'s existing minimum-distance floor
      (`kMutualGravityMinDistance`) still applies and keeps the force from
      diverging, so this shouldn't crash, but a visually extreme outcome
      (two members overlapping, or one disappearing off past the
      background starfield) is a real possible consequence of genuine
      chaos, not guarded against.
- [ ] Larger star counts (6-8) were not individually visually tuned
      (orbit/base radius scaling is a simple linear formula, `6.0 +
      1.5*n`) -- it should avoid obvious overlap at spawn for every N in
      [2,8] by construction, but hasn't been visually confirmed for the
      higher end of that range.

### Addendum (same session) — FreeFly mouse-look was inverted

User reported turning right with the mouse in FreeFly mode turned the
camera left, and vice versa. Confirmed by tracing the actual math rather
than just flipping the sign and hoping: `Camera::Forward()` for FreeFly is
`(sin(yaw), ..., cos(yaw))`, and `OrthonormalBasis()` defines `right` as
`cross(forward, up)`. Working through both formulas together, increasing
`freeFlyYawRadians` rotates `Forward()` toward the LEFT of that `right`
vector, not toward it -- so the original `camera.freeFlyYawRadians += (mx -
lastMouseX) * 0.005` (mouse moves right -> yaw increases) was turning the
view left. Fixed by negating it (`-=`). Orbit mode's mouse-drag (a
different, click-and-drag-to-orbit interaction, not first-person
mouselook) was left untouched -- not reported as wrong, and that
convention (drag right orbits the camera the "other" way around the
subject) is a different, deliberately different UX pattern from
first-person look, not the same bug.

Verified by a clean rebuild of both `build.bat`/`build_cuda.bat` (no
warnings) and both test suites passing (0 failures). Not confirmed by
actually moving the mouse in the running app -- same UI-automation
caveat as the rest of this session; the fix follows directly from the sign
analysis above, not trial-and-error.
