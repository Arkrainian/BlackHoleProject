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

### Build environment note (for next session, not a code issue)

`cmake`/`ninja` are not on PATH in the plain shell used for dev in this
environment — they only resolve inside a VS2022 Developer Command Prompt
(`vcvars64.bat`). Both `build.bat` and `build_cuda.bat` already call
`vcvars64.bat` themselves when run normally (e.g. by double-clicking or
from a regular `cmd.exe`), so this only matters if you're driving the
build from a non-Developer shell/script directly.
