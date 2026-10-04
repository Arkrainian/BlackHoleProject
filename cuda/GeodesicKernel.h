#pragma once

// Plain C++ (no CUDA syntax) so this header is includable from ordinary
// MSVC-compiled translation units (RayTracing/CUDATracer.cpp) as well as
// from geodesic_kernel.cu itself. LaunchGeodesicKernel's DEFINITION lives
// in geodesic_kernel.cu (compiled by nvcc, using the CUDA runtime API);
// this is the standard pattern for mixing CUDA and regular C++ in one
// target without CUDA syntax leaking into the MSVC-compiled side.

#include <vector>

namespace CudaTracing {

// A minimal 3-vector mirroring glm::dvec3, kept separate rather than using
// glm directly across the host/device boundary to avoid depending on glm's
// CUDA compatibility -- CUDATracer.cpp converts to/from glm::dvec3 at the
// boundary.
//
// float, not double: consumer NVIDIA GPUs (this project was profiled on an
// RTX 4070 Ti Super) deliberately cripple double-precision throughput to a
// small fraction of their single-precision rate -- unlike a CPU, where
// double and float cost about the same, double-precision arithmetic was
// measured as the dominant cost of this entire kernel on that class of
// hardware. The CPU tracer (Physics::GeodesicSolver, RayTracing::CPUTracer)
// is completely unaffected and still integrates in double precision -- so
// is the automated test suite, which only exercises the CPU path -- this
// float boundary applies only to the live GPU-rendered preview, where a
// few parts-per-million of angular error is visually imperceptible.
struct KernelVec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct KernelCameraFrame {
    KernelVec3 position, forward, right, up;
    float tanHalfFovY = 0.4142f;
    float aspect = 16.0f / 9.0f;
};

// Mirrors Physics::GeodesicConfig (see Physics/GeodesicSolver.h) -- kept as
// a separate plain struct rather than shared directly since GeodesicConfig
// isn't guaranteed trivially copyable to device memory in general, and
// keeping this boundary explicit documents exactly what crosses into CUDA.
struct KernelGeodesicConfig {
    float schwarzschildRadius = 2.0f;
    float captureMultiplier = 1.001f;
    float escapeRadius = 1.0e4f;
    float basePhiStep = 0.015f;
    float minPhiStep = 1.0e-6f;
    float maxPhiStep = 0.02f;
    int maxSteps = 8000;
    bool adaptiveStep = true;
    bool checkDisk = false;
    float diskInnerRadius = 6.0f;
    float diskOuterRadius = 26.0f;
    bool diskClockwise = true;
};

// Mirrors Physics::PhotonFate.
enum class KernelFate : int { Active = 0, Captured = 1, Escaped = 2, Unresolved = 3 };

struct KernelPixelResult {
    KernelFate fate = KernelFate::Active;
    KernelVec3 escapeDirection;
    bool hitDisk = false;
    float diskHitRadius = 0.0f;
    float diskRedshiftFactor = 1.0f;
};

// Traces one photon geodesic per pixel on the GPU (one CUDA thread per
// ray) and returns a pointer to the flat row-major result buffer (width *
// height elements), or nullptr on failure. Mirrors Physics::TracePhoton's
// non-path-recording behavior exactly (same shared GeodesicMath.h ODE step)
// -- CUDATracer.cpp applies the same StarField/AccretionDisk coloring
// CPUTracer uses to turn these results into pixel colors, so CPU and GPU
// renders are directly comparable.
//
// The returned pointer is into a page-locked (pinned) host buffer owned and
// reused internally across calls -- valid until the next call to this
// function (which may resize/replace it if the requested resolution grew).
// Returning a pointer into that buffer directly, rather than copying it into
// a fresh std::vector, avoids an extra full-frame host-to-host memcpy AND a
// fresh heap allocation every single frame -- at ~1.5M pixels this was
// measurably real (tens of milliseconds of otherwise-invisible overhead on
// top of the kernel's own, genuinely fast, GPU time).
const KernelPixelResult* LaunchGeodesicKernel(const KernelCameraFrame& camera, const KernelGeodesicConfig& config,
                                               int width, int height);

// True if a CUDA device was found and initialized successfully; false (with
// LaunchGeodesicKernel never called) otherwise -- checked once by
// CUDATracer so the UI can disable the GPU option gracefully.
bool CudaDeviceAvailable();

} // namespace CudaTracing
