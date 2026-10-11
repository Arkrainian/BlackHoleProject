#include "CUDATracer.h"

#include <algorithm>
#include <execution>
#include <numeric>

#include "../../cuda/GeodesicKernel.h" // plain header, no CUDA syntax -- see its own comment

using Physics::PhotonFate;
using Physics::PhotonTraceResult;

namespace RayTracing {

namespace {

// double <-> float at the host/device boundary -- the kernel itself works
// in float (see GeodesicKernel.h's KernelVec3 comment for why); the rest of
// the app (camera, StarField, AccretionDisk) stays in double throughout.
CudaTracing::KernelVec3 ToKernel(const glm::dvec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}
glm::dvec3 FromKernel(const CudaTracing::KernelVec3& v) {
    return glm::dvec3(static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z));
}

PhotonFate FromKernelFate(CudaTracing::KernelFate fate) {
    switch (fate) {
        case CudaTracing::KernelFate::Captured: return PhotonFate::CAPTURED;
        case CudaTracing::KernelFate::Escaped: return PhotonFate::ESCAPED;
        case CudaTracing::KernelFate::Unresolved: return PhotonFate::UNRESOLVED;
        case CudaTracing::KernelFate::Active: return PhotonFate::ACTIVE;
    }
    return PhotonFate::ACTIVE;
}

} // namespace

bool CudaAvailable() { return CudaTracing::CudaDeviceAvailable(); }

std::vector<glm::vec3> RenderImageCUDA(const CameraFrame& camera, const RenderSettings& settings,
                                        const BackgroundSampler& background, const DiskSampler& disk) {
    CudaTracing::KernelCameraFrame kernelCamera;
    kernelCamera.position = ToKernel(camera.position);
    kernelCamera.forward = ToKernel(camera.forward);
    kernelCamera.right = ToKernel(camera.right);
    kernelCamera.up = ToKernel(camera.up);
    kernelCamera.tanHalfFovY = static_cast<float>(camera.tanHalfFovY);
    kernelCamera.aspect = static_cast<float>(camera.aspect);

    CudaTracing::KernelGeodesicConfig kernelConfig;
    kernelConfig.schwarzschildRadius = static_cast<float>(settings.geodesicConfig.schwarzschildRadius);
    kernelConfig.captureMultiplier = static_cast<float>(settings.geodesicConfig.captureMultiplier);
    kernelConfig.escapeRadius = static_cast<float>(settings.geodesicConfig.escapeRadius);
    kernelConfig.basePhiStep = static_cast<float>(settings.geodesicConfig.basePhiStep);
    kernelConfig.minPhiStep = static_cast<float>(settings.geodesicConfig.minPhiStep);
    kernelConfig.maxPhiStep = static_cast<float>(settings.geodesicConfig.maxPhiStep);
    kernelConfig.maxSteps = settings.geodesicConfig.maxSteps;
    kernelConfig.adaptiveStep = settings.geodesicConfig.adaptiveStep;
    kernelConfig.checkDisk = settings.geodesicConfig.checkDisk;
    kernelConfig.diskInnerRadius = static_cast<float>(settings.geodesicConfig.diskInnerRadius);
    kernelConfig.diskOuterRadius = static_cast<float>(settings.geodesicConfig.diskOuterRadius);
    kernelConfig.diskClockwise = settings.geodesicConfig.diskClockwise;

    CudaTracing::KernelKerrConfig kernelKerrConfig;
    kernelKerrConfig.captureMultiplier = static_cast<float>(settings.kerrConfig.captureMultiplier);
    kernelKerrConfig.escapeRadius = static_cast<float>(settings.kerrConfig.escapeRadius);
    kernelKerrConfig.baseLambdaStep = static_cast<float>(settings.kerrConfig.baseLambdaStep);
    kernelKerrConfig.minLambdaStep = static_cast<float>(settings.kerrConfig.minLambdaStep);
    kernelKerrConfig.maxLambdaStep = static_cast<float>(settings.kerrConfig.maxLambdaStep);
    kernelKerrConfig.maxSteps = settings.kerrConfig.maxSteps;
    kernelKerrConfig.checkDisk = settings.kerrConfig.checkDisk;
    kernelKerrConfig.diskInnerRadius = static_cast<float>(settings.kerrConfig.diskInnerRadius);
    kernelKerrConfig.diskOuterRadius = static_cast<float>(settings.kerrConfig.diskOuterRadius);

    CudaTracing::KernelBinaryConfig kernelBinaryConfig;
    kernelBinaryConfig.escapeRadius = static_cast<float>(settings.binaryConfig.escapeRadius);
    kernelBinaryConfig.baseStep = static_cast<float>(settings.binaryConfig.baseStep);
    kernelBinaryConfig.minStep = static_cast<float>(settings.binaryConfig.minStep);
    kernelBinaryConfig.maxStep = static_cast<float>(settings.binaryConfig.maxStep);
    kernelBinaryConfig.maxSteps = settings.binaryConfig.maxSteps;
    kernelBinaryConfig.checkDisk = settings.binaryConfig.checkDisk;
    kernelBinaryConfig.disk1InnerRadius = static_cast<float>(settings.binaryConfig.disk1InnerRadius);
    kernelBinaryConfig.disk1OuterRadius = static_cast<float>(settings.binaryConfig.disk1OuterRadius);
    kernelBinaryConfig.disk2InnerRadius = static_cast<float>(settings.binaryConfig.disk2InnerRadius);
    kernelBinaryConfig.disk2OuterRadius = static_cast<float>(settings.binaryConfig.disk2OuterRadius);

    // The two masses' positions/masses are a snapshot of
    // settings.binarySystem's CURRENT orbital phase ("frozen lens," same
    // as the CPU solver -- see BinaryGeodesicSolver.h), computed here on
    // the host rather than passing the whole BinarySystem to the device.
    CudaTracing::KernelBinaryBodies kernelBinaryBodies;
    kernelBinaryBodies.position1 = ToKernel(settings.binarySystem.Position1());
    kernelBinaryBodies.position2 = ToKernel(settings.binarySystem.Position2());
    kernelBinaryBodies.gm1 = 1.0f;
    kernelBinaryBodies.gm2 = static_cast<float>(settings.binarySystem.massRatio);
    kernelBinaryBodies.rs1 = static_cast<float>(settings.binarySystem.SchwarzschildRadius1());
    kernelBinaryBodies.rs2 = static_cast<float>(settings.binarySystem.SchwarzschildRadius2());
    kernelBinaryBodies.captureRadius1 =
        static_cast<float>(settings.binaryConfig.captureMultiplier * settings.binarySystem.SchwarzschildRadius1());
    kernelBinaryBodies.captureRadius2 =
        static_cast<float>(settings.binaryConfig.captureMultiplier * settings.binarySystem.SchwarzschildRadius2());

    // Binary mode takes priority (see RayTracing::RenderImage's identical
    // branch order), then Kerr (nonzero spin) routes to the Kerr kernel,
    // otherwise plain Schwarzschild -- all three share this function's
    // output type and the coloring pass below.
    const CudaTracing::KernelPixelResult* kernelResults =
        settings.binaryEnabled
            ? CudaTracing::LaunchBinaryGeodesicKernel(kernelCamera, kernelBinaryConfig, kernelBinaryBodies,
                                                       settings.width, settings.height)
        : settings.spin != 0.0
            ? CudaTracing::LaunchKerrGeodesicKernel(kernelCamera, kernelKerrConfig, static_cast<float>(settings.spin),
                                                     settings.width, settings.height)
            : CudaTracing::LaunchGeodesicKernel(kernelCamera, kernelConfig, settings.width, settings.height);

    const size_t pixelCount = static_cast<size_t>(settings.width) * settings.height;

    // Reused across calls (grows, never shrinks -- same strategy as the
    // kernel's own persistent device/pinned buffers) instead of a fresh
    // ~18MB heap allocation + zero-fill every single frame at typical
    // render resolutions. `static` is safe here: PerformRender in App.cpp
    // only ever calls this from the one render/UI thread.
    static std::vector<glm::vec3> image;
    image.resize(pixelCount);

    if (!kernelResults) {
        std::fill(image.begin(), image.end(), glm::vec3(0.0f));
        return image;
    }

    // Coloring (starfield/disk sampling) runs on the CPU -- the GPU kernel
    // only computes each ray's fate/escape-direction/disk-hit, per
    // GeodesicKernel.h's design. That's cheap per pixel, but at these
    // resolutions "cheap per pixel" still adds up to real milliseconds if
    // done on a single thread -- parallelized across rows the same way
    // CPUTracer::RenderImage already is, so this loop can't silently
    // become the dominant cost of an otherwise GPU-accelerated frame.
    std::vector<int> rows(settings.height);
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int y) {
        const size_t rowStart = static_cast<size_t>(y) * settings.width;
        for (int x = 0; x < settings.width; ++x) {
            const size_t i = rowStart + x;
            const CudaTracing::KernelPixelResult& kr = kernelResults[i];

            if (kr.hitDisk && disk) {
                PhotonTraceResult hit;
                hit.hitDisk = true;
                hit.diskHitRadius = kr.diskHitRadius;
                hit.diskRedshiftFactor = kr.diskRedshiftFactor;
                hit.diskBodyIndex = kr.diskBodyIndex;
                image[i] = disk(hit);
            } else if (FromKernelFate(kr.fate) == PhotonFate::ESCAPED) {
                image[i] = background(FromKernel(kr.escapeDirection));
            } else {
                image[i] = glm::vec3(0.0f);
            }
        }
    });
    return image;
}

} // namespace RayTracing
