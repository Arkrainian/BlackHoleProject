#include "TestFramework.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "Astronomy/AccretionDisk.h"
#include "Astronomy/StarField.h"
#include "Physics/BinarySystem.h"
#include "Physics/BlackHole.h"
#include "Physics/KerrMetric.h"
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

// Same idea as RunGpuRenderTests, but with Kerr spin != 0 -- exercises the
// Kerr-specific GPU path (RenderImageCUDA -> TraceKerrPhotonsKernel ->
// TracePhotonKerrDevice) against the CPU Kerr solver
// (Physics::TracePhotonKerr) it's meant to mirror. This is the only
// end-to-end check that the CUDA port of the Carter-constant integrator
// (ported from the CPU solver after tests/test_kerr.cpp validated ITS
// physics) didn't introduce a float-vs-double-only discrepancy beyond the
// expected precision gap -- tests/test_kerr.cpp alone only ever exercised
// the CPU solver.
void RunKerrGpuRenderTests() {
    using Physics::BlackHole;

    const double spin = 0.8;

    RayTracing::RenderSettings settings;
    settings.width = 160;
    settings.height = 90;
    settings.spin = spin;
    settings.kerrConfig.escapeRadius = 1.0e4;
    settings.kerrConfig.maxSteps = 8000;
    settings.kerrConfig.checkDisk = true;
    settings.kerrConfig.diskInnerRadius = Physics::Kerr::EquatorialIscoRadiusSim(spin, true);
    settings.kerrConfig.diskOuterRadius = 26.0;

    const RayTracing::CameraFrame frame = BuildTestCameraFrame();

    Astronomy::StarField::Settings starSettings;
    Astronomy::StarField starField(starSettings);
    const RayTracing::BackgroundSampler background = [&](const glm::dvec3& dir) { return starField.Sample(dir); };

    Astronomy::AccretionDiskSettings diskSettings;
    diskSettings.innerRadius = settings.kerrConfig.diskInnerRadius;
    diskSettings.outerRadius = settings.kerrConfig.diskOuterRadius;
    Astronomy::AccretionDisk accretionDisk(diskSettings);
    const RayTracing::DiskSampler diskSampler = [&](const Physics::PhotonTraceResult& hit) {
        const double emittedTemperature = accretionDisk.Temperature(hit.diskHitRadius);
        const double observedTemperature = emittedTemperature * hit.diskRedshiftFactor;
        return accretionDisk.EmittedColor(observedTemperature);
    };

    const std::vector<glm::vec3> cpuPixels = RayTracing::RenderImage(frame, settings, background, diskSampler);
    const std::vector<glm::vec3> gpuPixels = RayTracing::RenderImageCUDA(frame, settings, background, diskSampler);

    ExpectTrue(cpuPixels.size() == gpuPixels.size(), "Kerr: CPU and GPU renders produce the same pixel count");
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

    std::printf("  Kerr (a*=%.2f) CPU vs GPU: max abs color diff = %.6f, mean abs color diff = %.6f (%d/%zu pixels non-black)\n",
                spin, maxAbsDiff, meanAbsDiff, nonBlackCpuPixels, cpuPixels.size());

    ExpectTrue(nonBlackCpuPixels > static_cast<int>(cpuPixels.size()) / 10,
               "Kerr test scene renders a substantial non-black image (sanity check)");

    // Looser than the Schwarzschild tolerance above, and specifically on
    // maxAbsDiff rather than meanAbsDiff: right at the shadow boundary
    // (impact parameter ~ the critical value), float vs double can tip a
    // handful of pixels to opposite sides of the capture/escape
    // classification -- one captured (black) vs. escaped (starfield-
    // colored) pixel is a large PER-PIXEL difference but affects a thin
    // rim of pixels, which is why meanAbsDiff (checked below, and the
    // more meaningful signal for "is the kernel actually right") stays
    // tiny regardless. The Kerr integrator's turning-point retry loop
    // makes this boundary marginally wider/noisier than Schwarzschild's
    // single clean u(phi) crossing, hence the looser cap here.
    ExpectTrue(maxAbsDiff < 0.6f, "Kerr: max per-channel CPU/GPU color difference stays within expected tolerance");
    ExpectTrue(meanAbsDiff < 0.02, "Kerr: mean per-channel CPU/GPU color difference stays small across the whole image");
}

// Same idea again, for binary mode -- exercises
// RenderImageCUDA -> TraceBinaryPhotonsKernel -> TracePhotonBinaryDevice
// against the CPU Physics::TracePhotonBinary it's meant to mirror,
// including each body's own disk (diskBodyIndex routing both CPU and GPU
// results to the right AccretionDisk instance).
void RunBinaryGpuRenderTests() {
    Physics::BinarySystem system;
    system.massRatio = 1.0;
    system.separation = 40.0;
    system.phase = 0.0;

    RayTracing::RenderSettings settings;
    settings.width = 160;
    settings.height = 90;
    settings.binaryEnabled = true;
    settings.binarySystem = system;
    settings.binaryConfig.escapeRadius = 600.0;
    settings.binaryConfig.maxSteps = 4000;
    settings.binaryConfig.checkDisk = true;
    settings.binaryConfig.disk1InnerRadius = 6.0;
    settings.binaryConfig.disk1OuterRadius = 26.0;
    settings.binaryConfig.disk2InnerRadius = 6.0;
    settings.binaryConfig.disk2OuterRadius = 26.0;

    const RayTracing::CameraFrame frame = BuildTestCameraFrame();

    // Denser than the Schwarzschild/Kerr tests' default starfield (0.04)
    // -- binary mode's disks are smaller in frame than the single-body
    // tests' (two separate disks rather than one centered on the camera's
    // look-at point), so a denser starfield keeps the "not a coincidental
    // all-black match" sanity check below meaningful regardless.
    Astronomy::StarField::Settings starSettings;
    starSettings.density = 0.2;
    Astronomy::StarField starField(starSettings);
    const RayTracing::BackgroundSampler background = [&](const glm::dvec3& dir) { return starField.Sample(dir); };

    Astronomy::AccretionDiskSettings disk1Settings;
    disk1Settings.innerRadius = settings.binaryConfig.disk1InnerRadius;
    disk1Settings.outerRadius = settings.binaryConfig.disk1OuterRadius;
    Astronomy::AccretionDisk disk1(disk1Settings);
    Astronomy::AccretionDiskSettings disk2Settings;
    disk2Settings.innerRadius = settings.binaryConfig.disk2InnerRadius;
    disk2Settings.outerRadius = settings.binaryConfig.disk2OuterRadius;
    Astronomy::AccretionDisk disk2(disk2Settings);
    const RayTracing::DiskSampler diskSampler = [&](const Physics::PhotonTraceResult& hit) {
        const Astronomy::AccretionDisk& whichDisk = (hit.diskBodyIndex == 1) ? disk2 : disk1;
        const double emittedTemperature = whichDisk.Temperature(hit.diskHitRadius);
        const double observedTemperature = emittedTemperature * hit.diskRedshiftFactor;
        return whichDisk.EmittedColor(observedTemperature);
    };

    const std::vector<glm::vec3> cpuPixels = RayTracing::RenderImage(frame, settings, background, diskSampler);
    const std::vector<glm::vec3> gpuPixels = RayTracing::RenderImageCUDA(frame, settings, background, diskSampler);

    ExpectTrue(cpuPixels.size() == gpuPixels.size(), "Binary: CPU and GPU renders produce the same pixel count");
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

    std::printf("  Binary CPU vs GPU: max abs color diff = %.6f, mean abs color diff = %.6f (%d/%zu pixels non-black)\n",
                maxAbsDiff, meanAbsDiff, nonBlackCpuPixels, cpuPixels.size());

    // Lower fraction than the Schwarzschild/Kerr tests' (1/10): those get
    // a large guaranteed-bright disk area for free, binary mode doesn't
    // (no disk yet), so "substantial" here just means "clearly not a
    // coincidental all-black match," not "most of the frame."
    ExpectTrue(nonBlackCpuPixels > static_cast<int>(cpuPixels.size()) / 50,
               "Binary test scene renders a substantial non-black image (sanity check)");

    // Same category of tolerance as the Kerr comparison above: TWO shadow
    // boundaries here instead of one, so a thin rim of pixels can tip
    // between captured/escaped under float-vs-double rounding, producing
    // a large max-diff outlier while the mean stays small.
    ExpectTrue(maxAbsDiff < 0.6f, "Binary: max per-channel CPU/GPU color difference stays within expected tolerance");
    ExpectTrue(meanAbsDiff < 0.02, "Binary: mean per-channel CPU/GPU color difference stays small across the whole image");
}
