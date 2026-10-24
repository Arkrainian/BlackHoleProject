# Black Hole Ray-Tracing Simulator

A computational astrophysics project: a from-scratch C++/CUDA simulator
that computes how light actually propagates through curved spacetime
around a black hole, by numerically integrating the null geodesic
equation — not by faking gravitational lensing with a screen-space shader
effect. It has since grown from a single Schwarzschild black hole demo
into a small, honestly-scoped universe: Kerr and binary black holes,
planetary destruction, a pilotable ship, and a real-catalog Star Map you
can travel through — all built on the same rule the original project
started with.

This is a physics simulation with a renderer attached, not a graphics demo
with physics-flavored decoration. Every visual result (the bent light,
the Einstein ring, the shadow, the lensed accretion disk, a star's real
temperature-derived color, an exoplanet's real orbital period) is either
the direct numerical output of integrating real equations of motion, or
is explicitly labeled as an approximation/visual stand-in at the exact
point it's made — this project does not silently misrepresent its own
physics anywhere.

<p align="center">
  <img src="docs/images/accretion_disk.png" width="48%" alt="Lensed accretion disk with the far side wrapped around the shadow" />
  <img src="docs/images/einstein_ring.png" width="48%" alt="Einstein ring formed by exact geodesic integration" />
</p>

*Left: an accretion disk, lensed — its far side visibly bent around behind
the shadow. Right: an Einstein ring, formed because pixels around the
shadow all happen to bend light from many different launch angles into
the same source direction. Neither image is drawn or warped; both are the
literal output of tracing photon geodesics.*

## What this demonstrates

- **General relativity**: the Schwarzschild metric, null geodesics, the
  photon sphere, gravitational lensing, the Einstein ring, the
  event-horizon/shadow distinction, gravitational + Doppler redshift; a
  Kerr (rotating) mode adding frame dragging, the ergosphere, and a
  spin-asymmetric photon orbit/ISCO/shadow via Carter's constant; and a
  binary-black-hole mode (approximate, by physical necessity — no exact
  two-body GR metric exists), plus arbitrarily many freely-placed extra
  black holes lensing together (N-body lensing).
- **Astrophysics beyond the metric itself**: a real Shakura-Sunyaev
  accretion disk; stars and planets as tidal-disruption test particles
  (real "spaghettification" as a direct consequence of differential
  gravity, not a scripted stretch effect); a planetary-destruction model
  with materials, Roche-limit fracture, and self-gravity reaccumulation;
  object-level mutual gravity between spawned bodies.
- **A real, bounded Star Map, not a procedural galaxy generator**: ~35
  real stars/black holes/galaxies at their real distances and masses,
  real exoplanets with Kepler-derived orbital periods cross-checked
  against their published values, and 2-body/3-body/N-body multi-star
  systems integrated with genuine real-time Newtonian gravity (the actual
  three-body problem, not a scripted animation, for N≥3).
- **A pilotable ship**, falling toward the black hole under the exact
  general-relativistic static-observer acceleration (not Newtonian
  gravity or a constant "down"), with a real, worked-through relativistic
  speed-cap tradeoff at supermassive black hole masses.
- **Numerical methods**: RK4 integration, curvature-adaptive step
  control, a from-scratch closed-form Kepler-orbit reference for
  validating integrator error, documented failure modes and the fixes
  for them.
- **C++**: a modular physics/rendering/ray-tracing architecture, RAII,
  `std::execution::par` parallelism, a from-scratch OpenGL+ImGui
  application.
- **CUDA**: a from-scratch GPU port sharing its numerical core with the
  CPU path at the source level (Schwarzschild, Kerr, binary, and
  planetary destruction), validated against the CPU output within
  expected float/double tolerance.
- **Scientific rigor**: 201 automated checks (0 failures) against known
  closed-form results and real published data — not "it looks right,"
  but "it matches the analytical prediction, or the real catalog number,
  to N significant figures, here's the number."

## Quick start

Requires Visual Studio 2022 (MSVC) on Windows; CMake and Ninja are used
via the ones bundled with VS. CUDA build additionally requires the CUDA
Toolkit (developed against 12.6) and an NVIDIA GPU.

```bat
:: CPU-only build (no CUDA toolkit needed)
build.bat
build\BlackHoleSimulator.exe
build\tests\BlackHoleTests.exe

:: CUDA-accelerated build (adds the GPU tracer and GPU-vs-CPU tests)
build_cuda.bat
build_cuda\BlackHoleSimulator.exe
```

Everything else (GLFW, GLM, Dear ImGui) is fetched automatically by CMake
at configure time — nothing to install beyond the compiler/CUDA toolkit.

## What's in the app

Opens in **Simple mode** (four one-click preset scenes, plain-language
sliders); click **"Show every control (Advanced mode)"** for the full
scientific panel. A few of the highlights:

- **The ray-traced black hole view**: Schwarzschild by default (TON 618's
  real mass, ~6.6×10¹⁰ solar masses), with an optional Kerr spin slider
  (frame dragging, ergosphere, prograde/retrograde asymmetry) and an
  optional second black hole in a real two-body orbit (binary mode,
  approximate lensing). A real, Shakura-Sunyaev accretion disk (on by
  default) with a toggle to compare relativistic effects on vs. off.
  Freely-placed extra black holes can be spawned anywhere and lens
  together with the primary.
- **Universe / Star Map** (Advanced mode, or press **M** anytime,
  including mid-flight): browse ~35 real stars, black holes, and
  galaxies at their real distances/masses and "Travel Here" — an
  explicitly-labeled instantaneous jump (the real light-travel time is
  always shown, never silently glossed over), not simulated
  faster-than-light flight. Traveling to a black hole re-points the ray
  tracer at its real mass/spin; traveling to a star switches to a
  simplified Star System View (this app's GR renderer only models black
  holes) with a procedurally-textured, temperature-tinted star, any real
  cataloged exoplanets on real Kepler-derived orbits, and real
  circumstellar debris disks where one is actually published (Vega,
  Fomalhaut, Epsilon Eridani). A separate "Multi-Star Systems" section
  offers a real 2-body binary (Alpha Centauri AB), a real 3-body system
  (the same system plus its real, extraordinarily distant third member,
  Proxima Centauri — a genuine three-body problem, no closed-form
  solution exists), and a procedural N-body generator (2-8 stars),
  integrated with a true per-frame Newtonian N-body simulation.
- **Pilot the ship** ("Pilot Endurance"): WASD pitch/roll, Q/E yaw, mouse
  look, Shift to throttle up, Ctrl to throttle down and reverse. Falls
  toward the black hole under the exact GR static-observer acceleration.
  A relativistic speed cap (0.2c by default, adjustable) is floored at a
  minimum playable rate at supermassive black hole masses — surfaced with
  an explicit on-screen warning whenever that floor is actually
  overriding the "real c" the slider promises, rather than silently
  breaking that promise.
- **Spawn stars and planets**: tidal-disruption test particles (real
  "spaghettification," no scripted stretch effect) and a full planetary
  destruction model (materials, Roche-limit fracture, self-gravity
  reaccumulation, debris feeding an accretion-disk-coupled tidal flare),
  with optional object-level mutual gravity between any two spawned
  bodies.
- **Scientific tooling**: a Photon Inspector (pick any pixel, see that
  ray's real fate/impact parameter/conserved quantities), Scientific
  Plots (deflection-angle sweep vs. the analytical formula, lapse-factor
  vs. radius, disk temperature profile), and a live CPU-vs-GPU comparison
  with measured speedup and pixel-color difference.

## Documentation

- **[docs/physics.md](docs/physics.md)** — the theory, start to finish:
  the Schwarzschild/Kerr metrics and null-geodesic equations, the photon
  sphere, the shadow/horizon distinction, the deflection angle and
  Einstein ring, the accretion-disk redshift derivation, binary/N-body
  lensing, tidal disruption and planetary destruction, the jet's exact
  GR gravity and the real speed-cap tradeoff it ran into, the Universe/
  Star Map's real-catalog coordinate math and travel honesty, Kepler-
  derived exoplanet orbits, procedural star textures, and multi-star
  N-body systems (including why 3+ bodies are genuinely chaotic, not a
  bug) — and a full, honest Limitations section.
- **[docs/numerical_methods.md](docs/numerical_methods.md)** — the
  numerics: why each integrator was chosen, the adaptive step-size
  heuristic, every configurable tolerance and why it exists, real bugs
  that were found and fixed (documented with the diagnosis), and how the
  CUDA kernel shares its numerical core with the CPU tracer at the source
  level rather than duplicating it.
- **[docs/validation.md](docs/validation.md)** — the results: every
  number is copied directly from an actual test run, not paraphrased.
  201 checks, 0 failures — Schwarzschild/Kerr closed-form results, binary
  and N-body lensing, planetary destruction, mutual gravity, the
  Universe/Star Map's real catalog coordinates and Kepler-derived
  exoplanet periods (checked against real published values), multi-star
  system data, and bit-identical CPU-vs-GPU comparisons with measured
  speedup.
- **[HANDOFF.md](HANDOFF.md)** — the engineering history: what's built,
  session by session, what's rough, real bugs found and fixed (with root
  causes), and decisions worth knowing about so they don't get "fixed"
  back by accident.

## Architecture

```
src/
  Physics/      Pure C++ physics engine: BlackHole, GeodesicSolver,
                KerrGeodesicSolver, BinaryGeodesicSolver,
                MultiBodyGeodesicSolver, NewtonianIntegrator, KeplerOrbit,
                GravitySource (tuned mutual gravity), GeodesicMath (the
                __host__ __device__-shared ODE core). No GL/CUDA deps —
                unit-testable and reusable from the CUDA kernel verbatim.
  RayTracing/   CPUTracer and CUDATracer: same function signature, same
                output, different execution target.
  Rendering/    Camera, immediate-mode debug-view renderer (including
                textured-sphere support for procedural star surfaces),
                fullscreen texture blit for the ray-traced image.
  Astronomy/    StarField (procedural sky), AccretionDisk (Shakura-
                Sunyaev profile + blackbody color, Blackbody.h),
                StarBody/PlanetBody (tidal disruption, planetary
                destruction), StarTexture.h (procedural stellar surfaces).
  Jet/          FlightPhysics/InputHandler/FlightCamera/JetRenderer: the
                pilotable ship's arcade flight model and exact-GR gravity.
  Universe/     StarCatalog.h (real stars/black holes/galaxies),
                ExoplanetCatalog.h (real exoplanets + Kepler periods),
                MultiStarSystem.h (real binary/triple + procedural N-body).
  Application/  App.cpp: GLFW/ImGui wiring and the interactive UI.
cuda/           geodesic_kernel.cu / planet_kernel.cu (GPU kernels) and
                their plain-C++ boundary headers, so CUDA syntax never
                leaks into MSVC-compiled code.
tests/          201 checks against closed-form analytical results and
                real published astronomical data.
docs/           physics.md, numerical_methods.md, validation.md.
```

Physics is deliberately kept independent of rendering: `bh_physics` and
`bh_astronomy` build as standalone static libraries with no GL/CUDA
dependency, so they're unit-testable in isolation and the CUDA kernel
can call directly into the same header the CPU path uses.

## Limitations

The core model is Schwarzschild (non-rotating, uncharged); a Kerr
(rotating) mode adds frame dragging, the ergosphere, and a
prograde/retrograde-asymmetric shadow and ISCO (see
[docs/physics.md §10](docs/physics.md#10-kerr-rotating-black-holes)), and
a binary-black-hole mode adds a second, orbiting mass — explicitly
approximate (no exact two-body metric exists in GR at all), using a
"doubled Newtonian" superposed light-bending scheme (see
[docs/physics.md §11](docs/physics.md#11-binary-black-holes-approximate)).
None of these implement magnetic fields or MHD, realistic disk radiative
transfer, or flux-calibrated brightness. The Universe/Star Map is a
small, hand-picked real catalog (~35 objects, 6 exoplanets), not a bulk
astronomical-catalog import or a procedural galaxy generator; travel is
an instantaneous narrative jump, never simulated continuous
faster-than-light flight; and this app's scope stops at space travel and
orbital viewing — no on-foot/surface exploration. See
[docs/physics.md §20](docs/physics.md#20-limitations) for the full,
honest list of what this is and isn't. Where an approximation is made
anywhere in the code, it's labeled as such at the point it's made.
