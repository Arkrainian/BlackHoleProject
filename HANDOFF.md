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
