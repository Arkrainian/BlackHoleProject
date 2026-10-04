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
tests pass, all validated against closed-form GR results. **No git
repository has been initialized yet** — nothing in this project is
committed anywhere.

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

- [ ] **`docs/validation.md` §9 is now stale.** It documents a
      bit-identical CPU vs. GPU comparison (`max abs diff = 0.000000`).
      That was true when the GPU kernel used `double`; after the float
      conversion it is **no longer bit-identical** (small, expected
      float-vs-double differences). Re-run "Validate: Compare CPU vs GPU"
      in the app (Advanced mode) and update that section with real
      numbers and an explanation of the expected float/double delta.
- [ ] **No git repository.** Nothing is committed. First real task for
      whoever picks this up: `git init`, decide on a `.gitignore` (at
      minimum `build/`, `build_cuda/`), and get an initial commit in.
- [ ] **Window title is stale**: still says "Stage 1-3: Baseline,
      Geodesics & Lensing" (`App.cpp`, `glfwCreateWindow` call) even
      though Stages 1-7 are all implemented.
- [ ] **`docs/images/*.png` predate the Simple/Advanced UI split.** They
      show the old single-panel debug view, not the current default
      Simple-mode experience. Consider recapturing for the README.
- [ ] **No automated coverage of `App.cpp` or the CUDA kernel.** All 38
      tests exercise the CPU physics engine only. GPU correctness is only
      spot-checked interactively via the compare button. If this project
      grows, consider a small headless GPU test (render a known scene,
      check a few pixels) rather than relying on eyeballing screenshots.
- [ ] **`kMaxRenderPixels = 1,500,000` is a hardcoded constant**, not a UI
      setting. It was tuned against one GPU (RTX 4070 Ti Super). A much
      stronger or much weaker machine might want it higher/lower. Worth
      exposing as an advanced setting if perf complaints keep coming from
      different hardware.
- [ ] **The near-radial camera-ray special case is unhandled in the CUDA
      kernel** (documented in a comment at the top of
      `cuda/geodesic_kernel.cu`). Affects at most a handful of pixels
      directly along the camera's exact forward axis; the CPU path
      handles it correctly. Low priority — visually negligible — but a
      real, known gap between the two tracers.
- [ ] **UI automation doesn't work reliably in this dev environment**
      (synthetic mouse clicks on ImGui buttons frequently didn't
      register during this session, whereas screenshots and keyboard/
      state-based verification did). Worth knowing before relying on
      click-driven UI tests here.

## Build

```
build.bat          # CPU-only, no CUDA toolkit needed
build_cuda.bat      # adds the GPU tracer (needs CUDA Toolkit 12.6+, tested against an RTX 4070 Ti Super)
```

Both configs build clean with no warnings (beyond pre-existing, unrelated
GLM `CMakeLists.txt` deprecation notices) and pass all 38 tests as of this
handoff.
