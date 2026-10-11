// GPU particle/spring step for Astronomy::PlanetBody -- one CUDA thread per
// particle (gravity, integration+horizon-check) or per spring (spring
// force, fracture check), re-orchestrating the exact same formulas
// PlanetBody.cpp's CPU path uses (CUDA device code can't use
// std::vector/glm's host-side convenience the way the CPU version does --
// same reasoning as geodesic_kernel.cu's header comment). Unlike the ray
// tracer's kernels, this one does NOT share BH_HOST_DEVICE-templated math
// with the CPU path: Physics::StaticObserverGravityAccelSim's formula is
// hand-duplicated below in float, following the exact precedent the binary
// black hole's GPU kernel already set for an equally simple formula (see
// HANDOFF.md: "deemed simple enough not to need the shared-header
// treatment") -- keep PlanetGravityAccel below in sync with
// Physics/BlackHole.h's StaticObserverGravityAccelSim if that formula ever
// changes.

#include "PlanetKernel.h"

#include <cstdio>
#include <cuda_runtime.h>

using namespace CudaPlanetSim;

namespace {

__device__ inline float3 LoadVec3(const float* arr, int idx) {
    return make_float3(arr[idx * 3 + 0], arr[idx * 3 + 1], arr[idx * 3 + 2]);
}
__device__ inline void StoreVec3(float* arr, int idx, float3 v) {
    arr[idx * 3 + 0] = v.x;
    arr[idx * 3 + 1] = v.y;
    arr[idx * 3 + 2] = v.z;
}
__device__ inline float3 operator+(float3 a, float3 b) { return make_float3(a.x + b.x, a.y + b.y, a.z + b.z); }
__device__ inline float3 operator-(float3 a, float3 b) { return make_float3(a.x - b.x, a.y - b.y, a.z - b.z); }
__device__ inline float3 operator*(float s, float3 a) { return make_float3(s * a.x, s * a.y, s * a.z); }
__device__ inline float Dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
__device__ inline float Length3(float3 a) { return sqrtf(Dot3(a, a)); }

// Float mirror of Physics::StaticObserverGravityAccelSim (src/Physics/BlackHole.h)
// -- see this file's header comment for why this is a hand-duplicated
// formula rather than a shared templated header.
__device__ inline float3 PlanetGravityAccel(float3 position, float rs) {
    const float r = Length3(position);
    if (r < 1e-6f) return make_float3(0.0f, 0.0f, 0.0f);
    const float newtonTerm = 1.0f / (r * r);
    const float relativisticFactor = 1.0f / sqrtf(fmaxf(1e-4f, 1.0f - rs / r));
    const float magnitude = fminf(newtonTerm * relativisticFactor, 5000.0f);
    const float3 direction = (1.0f / r) * position;
    return (-magnitude) * direction;
}

__global__ void GravityKernel(const float* positions, float* accel, int particleCount, float rs) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= particleCount) return;
    StoreVec3(accel, i, PlanetGravityAccel(LoadVec3(positions, i), rs));
}

// Mirrors PlanetBody::ApplySpringForces exactly: Hooke's-law force plus
// axial damping, split equally/oppositely between the two endpoints.
// atomicAdd is required (not a plain +=) because two springs sharing a
// particle can run on different threads concurrently.
__global__ void SpringForceKernel(const float* positions, const float* velocities, const float* masses,
                                   const unsigned char* consumed, const int* springA, const int* springB,
                                   const float* restLengths, const unsigned char* broken, int springCount,
                                   float stiffness, float damping, float* accel) {
    const int s = blockIdx.x * blockDim.x + threadIdx.x;
    if (s >= springCount) return;
    if (broken[s]) return;

    const int a = springA[s];
    const int b = springB[s];
    if (consumed[a] || consumed[b]) return;

    const float3 pa = LoadVec3(positions, a);
    const float3 pb = LoadVec3(positions, b);
    const float3 delta = pb - pa;
    const float length = Length3(delta);
    if (length < 1e-9f) return;
    const float3 direction = (1.0f / length) * delta;
    const float strain = length - restLengths[s];

    const float3 va = LoadVec3(velocities, a);
    const float3 vb = LoadVec3(velocities, b);
    const float relativeVelocityAlongAxis = Dot3(vb - va, direction);
    const float forceMagnitude = stiffness * strain + damping * stiffness * relativeVelocityAlongAxis;
    const float3 force = forceMagnitude * direction;

    const float massA = masses[a];
    const float massB = masses[b];
    atomicAdd(&accel[a * 3 + 0], force.x / massA);
    atomicAdd(&accel[a * 3 + 1], force.y / massA);
    atomicAdd(&accel[a * 3 + 2], force.z / massA);
    atomicAdd(&accel[b * 3 + 0], -force.x / massB);
    atomicAdd(&accel[b * 3 + 1], -force.y / massB);
    atomicAdd(&accel[b * 3 + 2], -force.z / massB);
}

// Semi-implicit Euler, same order as the CPU path: velocity updated from
// this step's accel, then position from the updated velocity, then the
// horizon check against the new position.
__global__ void IntegrateKernel(float* positions, float* velocities, const float* accel, unsigned char* consumed,
                                 int particleCount, float rs, float dt) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= particleCount) return;
    if (consumed[i]) return;

    float3 v = LoadVec3(velocities, i);
    const float3 a = LoadVec3(accel, i);
    v = v + dt * a;
    float3 p = LoadVec3(positions, i);
    p = p + dt * v;

    StoreVec3(velocities, i, v);
    StoreVec3(positions, i, p);

    if (Length3(p) < rs) {
        consumed[i] = 1;
    }
}

// Mirrors PlanetBody::CheckFractures exactly -- including that it does NOT
// skip springs touching an already-consumed particle (the CPU version
// doesn't either); uses this step's POST-integration positions, since this
// kernel always launches after IntegrateKernel.
__global__ void FractureKernel(const float* positions, const int* springA, const int* springB,
                                const float* restLengths, unsigned char* broken, int springCount,
                                float materialStrength) {
    const int s = blockIdx.x * blockDim.x + threadIdx.x;
    if (s >= springCount) return;
    if (broken[s]) return;

    const float restLength = restLengths[s];
    if (restLength < 1e-9f) return;

    const float3 pa = LoadVec3(positions, springA[s]);
    const float3 pb = LoadVec3(positions, springB[s]);
    const float length = Length3(pb - pa);
    const float strain = fabsf(length - restLength) / restLength;
    if (strain > materialStrength) {
        broken[s] = 1;
    }
}

// Persistent device buffers, grow-only, reused across calls -- same
// rationale and pattern as geodesic_kernel.cu's EnsureBufferCapacity (this
// function is called every frame per live planet at a size that only
// changes when a planet is spawned/cleared, not continuously). Particle-
// sized and spring-sized buffers are tracked/grown independently since
// they scale differently. Single-threaded caller (the render loop), so
// plain statics are safe -- no synchronization needed.
float* g_dPositions = nullptr;
float* g_dVelocities = nullptr;
float* g_dMasses = nullptr;
unsigned char* g_dConsumed = nullptr;
float* g_dAccel = nullptr; // scratch, device-only, never copied to host
size_t g_particleCapacity = 0;

int* g_dSpringA = nullptr;
int* g_dSpringB = nullptr;
float* g_dSpringRestLengths = nullptr;
unsigned char* g_dSpringBroken = nullptr;
size_t g_springCapacity = 0;

void FreeParticleBuffers() {
    if (g_dPositions) cudaFree(g_dPositions);
    if (g_dVelocities) cudaFree(g_dVelocities);
    if (g_dMasses) cudaFree(g_dMasses);
    if (g_dConsumed) cudaFree(g_dConsumed);
    if (g_dAccel) cudaFree(g_dAccel);
    g_dPositions = g_dVelocities = g_dAccel = nullptr;
    g_dMasses = nullptr;
    g_dConsumed = nullptr;
    g_particleCapacity = 0;
}

bool EnsureParticleBuffers(size_t n) {
    if (n <= g_particleCapacity && g_dPositions != nullptr) return true;
    FreeParticleBuffers();

    cudaError_t err = cudaMalloc(&g_dPositions, n * 3 * sizeof(float));
    if (err == cudaSuccess) err = cudaMalloc(&g_dVelocities, n * 3 * sizeof(float));
    if (err == cudaSuccess) err = cudaMalloc(&g_dMasses, n * sizeof(float));
    if (err == cudaSuccess) err = cudaMalloc(&g_dConsumed, n * sizeof(unsigned char));
    if (err == cudaSuccess) err = cudaMalloc(&g_dAccel, n * 3 * sizeof(float));
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA (planet): particle buffer allocation failed: %s\n", cudaGetErrorString(err));
        FreeParticleBuffers();
        return false;
    }
    g_particleCapacity = n;
    return true;
}

void FreeSpringBuffers() {
    if (g_dSpringA) cudaFree(g_dSpringA);
    if (g_dSpringB) cudaFree(g_dSpringB);
    if (g_dSpringRestLengths) cudaFree(g_dSpringRestLengths);
    if (g_dSpringBroken) cudaFree(g_dSpringBroken);
    g_dSpringA = g_dSpringB = nullptr;
    g_dSpringRestLengths = nullptr;
    g_dSpringBroken = nullptr;
    g_springCapacity = 0;
}

bool EnsureSpringBuffers(size_t n) {
    if (n <= g_springCapacity && g_dSpringA != nullptr) return true;
    FreeSpringBuffers();

    cudaError_t err = cudaMalloc(&g_dSpringA, n * sizeof(int));
    if (err == cudaSuccess) err = cudaMalloc(&g_dSpringB, n * sizeof(int));
    if (err == cudaSuccess) err = cudaMalloc(&g_dSpringRestLengths, n * sizeof(float));
    if (err == cudaSuccess) err = cudaMalloc(&g_dSpringBroken, n * sizeof(unsigned char));
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA (planet): spring buffer allocation failed: %s\n", cudaGetErrorString(err));
        FreeSpringBuffers();
        return false;
    }
    g_springCapacity = n;
    return true;
}

} // namespace

namespace CudaPlanetSim {

bool PlanetCudaDeviceAvailable() {
    int count = 0;
    const cudaError_t err = cudaGetDeviceCount(&count);
    return err == cudaSuccess && count > 0;
}

bool RunPlanetStepCUDA(float* positionsXYZ, float* velocitiesXYZ, const float* masses, unsigned char* consumedFlags,
                        int particleCount, const int* springA, const int* springB, const float* springRestLengths,
                        unsigned char* springBroken, int springCount, float schwarzschildRadius, float stiffness,
                        float damping, float materialStrength, float dt) {
    if (particleCount <= 0) return true;
    if (!EnsureParticleBuffers(static_cast<size_t>(particleCount))) return false;
    if (springCount > 0 && !EnsureSpringBuffers(static_cast<size_t>(springCount))) return false;

    cudaMemcpy(g_dPositions, positionsXYZ, static_cast<size_t>(particleCount) * 3 * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(g_dVelocities, velocitiesXYZ, static_cast<size_t>(particleCount) * 3 * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(g_dMasses, masses, static_cast<size_t>(particleCount) * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(g_dConsumed, consumedFlags, static_cast<size_t>(particleCount) * sizeof(unsigned char), cudaMemcpyHostToDevice);
    if (springCount > 0) {
        cudaMemcpy(g_dSpringA, springA, static_cast<size_t>(springCount) * sizeof(int), cudaMemcpyHostToDevice);
        cudaMemcpy(g_dSpringB, springB, static_cast<size_t>(springCount) * sizeof(int), cudaMemcpyHostToDevice);
        cudaMemcpy(g_dSpringRestLengths, springRestLengths, static_cast<size_t>(springCount) * sizeof(float), cudaMemcpyHostToDevice);
        cudaMemcpy(g_dSpringBroken, springBroken, static_cast<size_t>(springCount) * sizeof(unsigned char), cudaMemcpyHostToDevice);
    }

    // Flat 1-D launches (particles/springs, not an image) -- a 256-wide
    // block is the usual default for this kind of embarrassingly-parallel
    // flat-array kernel, unlike the ray tracer's 16x16 2-D blocks.
    constexpr int kBlockSize = 256;
    const int particleBlocks = (particleCount + kBlockSize - 1) / kBlockSize;
    GravityKernel<<<particleBlocks, kBlockSize>>>(g_dPositions, g_dAccel, particleCount, schwarzschildRadius);

    if (springCount > 0) {
        const int springBlocks = (springCount + kBlockSize - 1) / kBlockSize;
        SpringForceKernel<<<springBlocks, kBlockSize>>>(g_dPositions, g_dVelocities, g_dMasses, g_dConsumed, g_dSpringA,
                                                         g_dSpringB, g_dSpringRestLengths, g_dSpringBroken, springCount,
                                                         stiffness, damping, g_dAccel);
    }

    IntegrateKernel<<<particleBlocks, kBlockSize>>>(g_dPositions, g_dVelocities, g_dAccel, g_dConsumed, particleCount,
                                                     schwarzschildRadius, dt);

    if (springCount > 0) {
        const int springBlocks = (springCount + kBlockSize - 1) / kBlockSize;
        FractureKernel<<<springBlocks, kBlockSize>>>(g_dPositions, g_dSpringA, g_dSpringB, g_dSpringRestLengths,
                                                      g_dSpringBroken, springCount, materialStrength);
    }

    cudaError_t err = cudaGetLastError();
    if (err == cudaSuccess) err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA (planet): kernel launch failed: %s\n", cudaGetErrorString(err));
        return false;
    }

    cudaMemcpy(positionsXYZ, g_dPositions, static_cast<size_t>(particleCount) * 3 * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(velocitiesXYZ, g_dVelocities, static_cast<size_t>(particleCount) * 3 * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(consumedFlags, g_dConsumed, static_cast<size_t>(particleCount) * sizeof(unsigned char), cudaMemcpyDeviceToHost);
    if (springCount > 0) {
        cudaMemcpy(springBroken, g_dSpringBroken, static_cast<size_t>(springCount) * sizeof(unsigned char), cudaMemcpyDeviceToHost);
    }
    return true;
}

} // namespace CudaPlanetSim
