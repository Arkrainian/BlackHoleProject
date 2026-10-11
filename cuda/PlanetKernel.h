#pragma once

// Plain C++ (no CUDA syntax) so this header is includable from ordinary
// MSVC-compiled translation units (Astronomy/PlanetBody.cpp) as well as
// from planet_kernel.cu itself -- same reasoning, same pattern, as
// cuda/GeodesicKernel.h (see its own header comment).
//
// This is a genuinely different kind of GPU workload than the ray tracer's:
// that one launches once per frame over ~10^5-10^6 independent, read-once
// pixels. A planet's particle/spring simulation is a small (tens to a few
// hundred elements by default), STATEFUL system that must round-trip to
// the CPU every frame anyway (for rendering and the union-find fragment
// rebuild, both of which stay on the CPU -- see Astronomy/PlanetBody.h).
// At the default particle counts, a GPU kernel launch here will likely be
// SLOWER than the equivalent CPU loop, not faster -- the point of this path
// is to let particle counts go much higher than would be CPU-real-time,
// not to speed up today's defaults. See HANDOFF.md for the measurement
// this is based on.

namespace CudaPlanetSim {

// True if a CUDA-capable device was found (same check as
// CudaTracing::CudaDeviceAvailable in GeodesicKernel.h, deliberately
// duplicated in planet_kernel.cu rather than shared -- these are two
// independent .cu targets, and coupling them together just to share one
// five-line device-count check isn't worth the build-graph dependency).
bool PlanetCudaDeviceAvailable();

// Runs one physics step -- gravity + spring forces + semi-implicit Euler
// integration + horizon-consumption check + fracture check -- for a single
// planet's particle/spring arrays entirely on the GPU, mirroring
// Astronomy::PlanetBody::Update's per-particle math exactly (same formulas,
// same order of operations), but in float instead of double -- the same
// precision tradeoff already accepted for the Kerr/binary ray-tracing
// kernels (see KernelVec3's comment in GeodesicKernel.h).
//
// positionsXYZ / velocitiesXYZ: flat interleaved [x0,y0,z0,x1,y1,z1,...]
// arrays, length 3*particleCount, updated in place.
// masses: length particleCount, read-only.
// consumedFlags: length particleCount, 0 or 1, read AND written -- a
// particle that crosses the horizon this step gets its flag set to 1 (and
// its position/velocity are left exactly where they were at that moment,
// never touched again); a particle already flagged going in is left alone
// entirely (no gravity/spring/integration applied to it, matching
// PlanetBody::Update's own `if (particle.consumed) continue`).
// springA / springB / springRestLengths: length springCount, read-only --
// the lattice topology never changes after construction, only which
// springs are broken does.
// springBroken: length springCount, 0 or 1, read AND written -- permanent,
// same as the CPU path (a broken spring never un-breaks).
// schwarzschildRadius/stiffness/damping/materialStrength/dt: the same
// per-step constants PlanetBody::Update/ApplySpringForces/CheckFractures
// already compute from Physics::BlackHole and PlanetMaterial -- passed in
// rather than recomputed device-side.
// selfGravityEnabled/selfGravityCoefficient/totalMassKg: mirrors
// PlanetBody::ApplySelfGravity's direct-summation formula exactly (see its
// own comment for why selfGravityCoefficient is a standalone tuned
// constant, not the real mass-ratio self-gravity formula) -- totalMassKg
// is the planet's total mass (kg), used the same way ApplySelfGravity uses
// it to keep the per-particle pull resolution-invariant.
//
// Returns false, leaving every array byte-for-byte unmodified, if no CUDA
// device is available or an allocation/launch/copy failed -- the caller
// (PlanetBody::UpdateGPU) falls back to the CPU Update() path for that
// frame in that case.
// selfGravityMinDistance: self-gravity is skipped between particle pairs
// closer together than this (sim units) -- mirrors PlanetBody::
// ApplySelfGravity's own cutoff exactly (see its comment for why this
// exists: without it, self-gravity fights the spring lattice at its own
// short rest lengths and destabilizes an otherwise-intact planet).
bool RunPlanetStepCUDA(float* positionsXYZ, float* velocitiesXYZ, const float* masses, unsigned char* consumedFlags,
                        int particleCount, const int* springA, const int* springB, const float* springRestLengths,
                        unsigned char* springBroken, int springCount, float schwarzschildRadius, float stiffness,
                        float damping, float materialStrength, float dt, bool selfGravityEnabled,
                        float selfGravityCoefficient, float totalMassKg, float selfGravityMinDistance);

} // namespace CudaPlanetSim
