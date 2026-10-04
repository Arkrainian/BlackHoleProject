#include "TestFramework.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "Astronomy/AccretionDisk.h"
#include "Astronomy/StarField.h"
#include "Physics/BlackHole.h"
#include "RayTracing/CPUTracer.h"
#include "RayTracing/CUDATracer.h"

using namespace TestFramework;

namespace {

// A small, fixed, non-trivial scene: an orbiting camera looking at the
// black hole with the accretion disk enabled, so both the lensed starfield
// AND the disk-redshift path (the two places CPU/GPU could disagree) are
// exercised in one render. Deliberately modest resolution -- this only
// needs to catch a regression in the GPU kernel's float math or control
// flow (e.g. the near-radial branch), not stand in for a visual benchmark.
RayTracing::CameraFrame BuildTestCameraFrame() {
    RayTracing::CameraFrame frame;
    frame.position = glm::dvec3(0.0, 8.0, 30.0);
    frame.forward = glm::normalize(-frame.position);
    frame.right = glm::normalize(glm::cross(frame.forward, glm::dvec3(0.0, 1.0, 0.0)));
    frame.up = glm::cross(frame.right, frame.forward);
    frame.tanHalfFovY = std::tan(glm::radians(45.0) * 0.5);
    frame.aspect = 16.0 / 9.0;
    return frame;
}

} // namespace

// Renders the same scene through RayTracing::RenderImage (CPU, double
// precision) and RayTracing::RenderImageCUDA (GPU, float precision -- see
// GeodesicKernel.h) and checks they agree to within the expected
// float-vs-double tolerance. This is the automated counterpart to the
// Advanced-mode "Validate: Compare CPU vs GPU" button -- see
// docs/validation.md section 9 for the numbers this produces.
void RunGpuRenderTests() {
    using Physics::BlackHole;

    const double rs = BlackHole::SchwarzschildRadiusSim();

    RayTracing::RenderSettings settings;
    settings.width = 160;
    settings.height = 90;
    settings.geodesicConfig.escapeRadius = 1.0e4;
    settings.geodesicConfig.maxSteps = 8000;
    settings.geodesicConfig.basePhiStep = 0.015;
    settings.geodesicConfig.checkDisk = true;
    settings.geodesicConfig.diskInnerRadius = 3.0 * rs; // ISCO
    settings.geodesicConfig.diskOuterRadius = 26.0;
    settings.geodesicConfig.diskClockwise = true;

    const RayTracing::CameraFrame frame = BuildTestCameraFrame();

    Astronomy::StarField::Settings starSettings;
    Astronomy::StarField starField(starSettings);
    const RayTracing::BackgroundSampler background = [&](const glm::dvec3& dir) { return starField.Sample(dir); };

    Astronomy::AccretionDiskSettings diskSettings;
    diskSettings.innerRadius = settings.geodesicConfig.diskInnerRadius;
    diskSettings.outerRadius = settings.geodesicConfig.diskOuterRadius;
    diskSettings.clockwise = settings.geodesicConfig.diskClockwise;
    Astronomy::AccretionDisk accretionDisk(diskSettings);
    const RayTracing::DiskSampler diskSampler = [&](const Physics::PhotonTraceResult& hit) {
        const double emittedTemperature = accretionDisk.Temperature(hit.diskHitRadius);
        const double observedTemperature = emittedTemperature * hit.diskRedshiftFactor;
        return accretionDisk.EmittedColor(observedTemperature);
    };

    const std::vector<glm::vec3> cpuPixels = RayTracing::RenderImage(frame, settings, background, diskSampler);
    const std::vector<glm::vec3> gpuPixels = RayTracing::RenderImageCUDA(frame, settings, background, diskSampler);

    ExpectTrue(cpuPixels.size() == gpuPixels.size(), "CPU and GPU renders produce the same pixel count");
    if (cpuPixels.size() != gpuPixels.size()) {
        return;
    }

    float maxAbsDiff = 0.0f;
    double sumAbsDiff = 0.0;
    int nonBlackCpuPixels = 0;
    for (size_t i = 0; i < cpuPixels.size(); ++i) {
        const glm::vec3 d = glm::abs(cpuPixels[i] - gpuPixels[i]);
        maxAbsDiff = std::max({maxAbsDiff, d.r, d.g, d.b});
        sumAbsDiff += d.r + d.g + d.b;
        if (cpuPixels[i].r > 0.0f || cpuPixels[i].g > 0.0f || cpuPixels[i].b > 0.0f) {
            ++nonBlackCpuPixels;
        }
    }
    const double meanAbsDiff = sumAbsDiff / (cpuPixels.size() * 3.0);

    std::printf("  CPU vs GPU: max abs color diff = %.6f, mean abs color diff = %.6f (%d/%zu pixels non-black)\n",
                maxAbsDiff, meanAbsDiff, nonBlackCpuPixels, cpuPixels.size());

    // The scene actually renders something (shadow + disk + stars), not an
    // all-black or degenerate image -- guards against a silent regression
    // (e.g. a broken escape/capture classification) that would otherwise
    // make a trivial all-black comparison pass by coincidence.
    ExpectTrue(nonBlackCpuPixels > static_cast<int>(cpuPixels.size()) / 10,
               "Test scene renders a substantial non-black image (sanity check, not a coincidental all-black match)");

    // Not bit-identical by design: the GPU kernel runs in float, the CPU
    // tracer in double (see HANDOFF.md / GeodesicKernel.h). This tolerance
    // is deliberately loose -- it catches a broken kernel (wrong physics,
    // control-flow divergence), not float's normal ULP-level noise.
    ExpectTrue(maxAbsDiff < 0.35f, "Max per-channel CPU/GPU color difference stays within expected float/double tolerance");
    ExpectTrue(meanAbsDiff < 0.01, "Mean per-channel CPU/GPU color difference stays small across the whole image");
}
