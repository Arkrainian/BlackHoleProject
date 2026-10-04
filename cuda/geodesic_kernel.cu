// GPU photon geodesic tracer: one CUDA thread per pixel/ray, mirroring
// Physics::TracePhoton's non-radial branch (see GeodesicSolver.cpp) using
// the SAME shared ODE primitives from Physics/GeodesicMath.h -- the actual
// numerical integration is not reimplemented, only re-orchestrated into a
// flat per-thread kernel (CUDA device code can't use std::vector/
// std::function/exceptions the way the CPU version does).
//
// The near-radial special case (impact parameter ~0, camera aimed dead-on
// at the black hole to floating-point precision) IS special-cased here,
// mirroring the CPU tracer's trivial linear-r stepping -- see the
// planeNormalLen branch in TracePhotonDevice below and GeodesicSolver.cpp's
// matching branch. Without it, the main b-parametrized path's b -> 0
// produces a large-but-finite v0 that either integrates implausibly or
// fails the isfinite() guard, misclassifying the handful of pixels
// directly along the camera's exact forward axis as Unresolved.

#include "GeodesicKernel.h"

#include <cstdio>
#include <cstring>
#include <cuda_runtime.h>

#include "Physics/GeodesicMath.h"
#include "Physics/SchwarzschildMetric.h"

using namespace CudaTracing;

namespace {

__host__ __device__ inline KernelVec3 operator+(const KernelVec3& a, const KernelVec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
__host__ __device__ inline KernelVec3 operator-(const KernelVec3& a, const KernelVec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
__host__ __device__ inline KernelVec3 operator-(const KernelVec3& a) { return {-a.x, -a.y, -a.z}; }
__host__ __device__ inline KernelVec3 operator*(float s, const KernelVec3& a) { return {s * a.x, s * a.y, s * a.z}; }
__host__ __device__ inline float Dot(const KernelVec3& a, const KernelVec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
__host__ __device__ inline KernelVec3 Cross(const KernelVec3& a, const KernelVec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
__host__ __device__ inline float Length(const KernelVec3& a) { return sqrtf(Dot(a, a)); }
__host__ __device__ inline KernelVec3 Normalize(const KernelVec3& a) {
    const float len = Length(a);
    return (1.0f / len) * a;
}
template <typename T>
__host__ __device__ inline T DMax(T a, T b) { return a > b ? a : b; }
template <typename T>
__host__ __device__ inline T DMin(T a, T b) { return a < b ? a : b; }

// All arithmetic below is float, not double -- see the comment on
// KernelVec3 in GeodesicKernel.h for why. GeodesicMath::Accel/RK4Step/
// AdaptivePhiStep and Schwarzschild::LapseFactor are templated on T and
// deduce T=float here purely from these calls' argument types, so this is
// still the exact same shared ODE code the CPU path uses at T=double --
// not a reimplementation.
__device__ KernelPixelResult TracePhotonDevice(const KernelVec3& origin, const KernelVec3& directionIn,
                                                 const KernelGeodesicConfig& config) {
    KernelPixelResult result;

    const float rs = config.schwarzschildRadius;
    const float r0 = Length(origin);
    const float captureRadius = config.captureMultiplier * rs;

    if (r0 <= rs) {
        result.fate = KernelFate::Captured;
        return result;
    }

    const KernelVec3 dir = Normalize(directionIn);
    const KernelVec3 rHat = (1.0f / r0) * origin;

    const KernelVec3 planeNormalRaw = Cross(origin, dir);
    const float planeNormalLen = Length(planeNormalRaw);
    const float lapse0 = Physics::Schwarzschild::LapseFactor(r0, rs);

    if (planeNormalLen < 1.0e-6f) {
        // Near-radial ray (impact parameter ~0): mirrors the CPU tracer's
        // trivial linear-r stepping (see GeodesicSolver.cpp) rather than
        // falling through to the b-parametrized path below, where b -> 0
        // would blow up v = -nRadial/b. Disk crossing is intentionally not
        // checked here, matching the CPU path's behavior for this case.
        const float nRadial = Dot(dir, rHat); // +1 outward, -1 inward
        float r = r0;
        const float rawStep = (config.escapeRadius - rs) / config.maxSteps;
        const float step = rawStep > 1.0e-4f ? rawStep : 1.0e-4f;

        for (int i = 0; i < config.maxSteps; ++i) {
            r += nRadial * fmaxf(step, 1.0e-6f);

            if (r <= captureRadius) {
                result.fate = KernelFate::Captured;
                return result;
            }
            if (r >= config.escapeRadius) {
                result.fate = KernelFate::Escaped;
                result.escapeDirection = nRadial >= 0.0f ? rHat : -rHat;
                return result;
            }
        }
        result.fate = KernelFate::Unresolved;
        return result;
    }

    const KernelVec3 planeNormal = (1.0f / planeNormalLen) * planeNormalRaw;
    const KernelVec3 tangentialDir = Cross(planeNormal, rHat);

    const float nRadial = Dot(dir, rHat);
    const float nTangential = Dot(dir, tangentialDir);

    const float b = r0 * nTangential / sqrtf(lapse0);

    float u = 1.0f / r0;
    float v = -nRadial / b;
    float phi = 0.0f;

    KernelVec3 prevPos = origin;
    float prevPhi = 0.0f;

    for (int i = 0; i < config.maxSteps; ++i) {
        const float h = Physics::GeodesicMath::AdaptivePhiStep(u, rs, config.basePhiStep, config.minPhiStep,
                                                                 config.maxPhiStep, config.adaptiveStep);
        Physics::GeodesicMath::RK4Step(u, v, h, rs);
        phi += h;

        if (!isfinite(u) || !isfinite(v)) {
            result.fate = KernelFate::Unresolved;
            return result;
        }

        if (u <= 1.0f / config.escapeRadius) {
            result.fate = KernelFate::Escaped;
            result.escapeDirection = cosf(phi) * rHat + sinf(phi) * tangentialDir;
            return result;
        }

        const float r = 1.0f / u;
        const KernelVec3 pos = r * (cosf(phi) * rHat + sinf(phi) * tangentialDir);

        if (config.checkDisk && prevPos.z != pos.z && (prevPos.z > 0.0f) != (pos.z > 0.0f)) {
            const float t = prevPos.z / (prevPos.z - pos.z);
            const KernelVec3 hitPos = prevPos + t * (pos - prevPos);
            const float hitR = Length(hitPos);

            if (hitR >= config.diskInnerRadius && hitR <= config.diskOuterRadius) {
                const float hitPhi = prevPhi + t * (phi - prevPhi);

                const float nPhiHatAtHit = b * sqrtf(Physics::Schwarzschild::LapseFactor(hitR, rs)) / hitR;
                const float nRadialAtHit =
                    (v >= 0.0f ? -1.0f : 1.0f) * sqrtf(DMax(0.0f, 1.0f - nPhiHatAtHit * nPhiHatAtHit));
                const KernelVec3 radialDirAtHit = cosf(hitPhi) * rHat + sinf(hitPhi) * tangentialDir;
                const KernelVec3 tangentialDirAtHit = (-sinf(hitPhi)) * rHat + cosf(hitPhi) * tangentialDir;
                const KernelVec3 photonDir = nRadialAtHit * radialDirAtHit + nPhiHatAtHit * tangentialDirAtHit;

                KernelVec3 diskVelocityDir = Normalize(Cross(KernelVec3{0.0f, 0.0f, 1.0f}, hitPos));
                if (!config.diskClockwise) diskVelocityDir = -diskVelocityDir;
                const float vLocal = 1.0f / sqrtf(DMax(1e-6f, hitR - rs));
                const float gammaLocal = 1.0f / sqrtf(DMax(1e-9f, 1.0f - vLocal * vLocal));

                const float cosPsi = Dot(photonDir, diskVelocityDir);
                const float dopplerFactor = 1.0f / (gammaLocal * (1.0f - vLocal * cosPsi));
                const float gravitationalFactor = sqrtf(Physics::Schwarzschild::LapseFactor(hitR, rs));

                result.hitDisk = true;
                result.diskHitRadius = hitR;
                result.diskRedshiftFactor = gravitationalFactor * dopplerFactor;
                return result;
            }
        }
        prevPos = pos;
        prevPhi = phi;

        if (r <= captureRadius) {
            result.fate = KernelFate::Captured;
            return result;
        }
    }

    result.fate = KernelFate::Unresolved;
    return result;
}

__global__ void TracePhotonsKernel(KernelCameraFrame camera, KernelGeodesicConfig config, int width, int height,
                                    KernelPixelResult* output) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const float ndcX = ((x + 0.5f) / width * 2.0f - 1.0f) * camera.aspect * camera.tanHalfFovY;
    const float ndcY = (1.0f - (y + 0.5f) / height * 2.0f) * camera.tanHalfFovY;

    const KernelVec3 direction = Normalize(camera.forward + ndcX * camera.right + ndcY * camera.up);
    output[static_cast<size_t>(y) * width + x] = TracePhotonDevice(camera.position, direction, config);
}

// Persistent device (and pinned host) buffers, reused across calls instead
// of cudaMalloc/cudaFree'd every frame. In a live-rendering app this
// function is called ~10+ times a second at a size that only changes when
// the window is resized or the resolution slider moves -- allocating and
// freeing GPU memory on that cadence is pure overhead (cudaMalloc is not a
// cheap syscall-free operation), and was a real, measurable contributor to
// the app feeling laggy despite the kernel itself being fast. Grows to fit
// the largest size requested so far; never shrinks (avoids realloc churn
// when toggling the resolution slider back and forth). This app only ever
// calls LaunchGeodesicKernel from one thread (the render loop), so plain
// statics are safe here -- no synchronization needed.
KernelPixelResult* g_deviceResults = nullptr;
KernelPixelResult* g_hostResultsPinned = nullptr; // page-locked host memory: faster cudaMemcpy than a plain std::vector
size_t g_bufferCapacity = 0;

bool EnsureBufferCapacity(size_t pixelCount) {
    if (pixelCount <= g_bufferCapacity && g_deviceResults != nullptr) {
        return true;
    }
    if (g_deviceResults) cudaFree(g_deviceResults);
    if (g_hostResultsPinned) cudaFreeHost(g_hostResultsPinned);
    g_deviceResults = nullptr;
    g_hostResultsPinned = nullptr;
    g_bufferCapacity = 0;

    cudaError_t err = cudaMalloc(&g_deviceResults, pixelCount * sizeof(KernelPixelResult));
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA: cudaMalloc failed: %s\n", cudaGetErrorString(err));
        return false;
    }
    err = cudaMallocHost(&g_hostResultsPinned, pixelCount * sizeof(KernelPixelResult));
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA: cudaMallocHost failed: %s\n", cudaGetErrorString(err));
        cudaFree(g_deviceResults);
        g_deviceResults = nullptr;
        return false;
    }
    g_bufferCapacity = pixelCount;
    return true;
}

} // namespace

namespace CudaTracing {

bool CudaDeviceAvailable() {
    int count = 0;
    const cudaError_t err = cudaGetDeviceCount(&count);
    return err == cudaSuccess && count > 0;
}

const KernelPixelResult* LaunchGeodesicKernel(const KernelCameraFrame& camera, const KernelGeodesicConfig& config,
                                               int width, int height) {
    const size_t pixelCount = static_cast<size_t>(width) * height;

    if (!EnsureBufferCapacity(pixelCount)) {
        return nullptr;
    }

    const dim3 block(16, 16);
    const dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    TracePhotonsKernel<<<grid, block>>>(camera, config, width, height, g_deviceResults);

    cudaError_t err = cudaGetLastError();
    if (err == cudaSuccess) {
        err = cudaDeviceSynchronize();
    }
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA: kernel launch failed: %s\n", cudaGetErrorString(err));
        return nullptr;
    }

    // Straight into the pinned buffer, which the caller reads directly --
    // no intermediate std::vector copy (see the header comment).
    cudaMemcpy(g_hostResultsPinned, g_deviceResults, pixelCount * sizeof(KernelPixelResult), cudaMemcpyDeviceToHost);
    return g_hostResultsPinned;
}

} // namespace CudaTracing
