#include "CPUTracer.h"

#include <algorithm>
#include <array>
#include <execution>
#include <numeric>

using Physics::LensingBody;
using Physics::PhotonFate;
using Physics::TracePhoton;
using Physics::TracePhotonBinary;
using Physics::TracePhotonKerr;
using Physics::TracePhotonMultiBody;

namespace RayTracing {

namespace {

// Builds the flattened body list Physics::TracePhotonMultiBody needs from
// whichever of {primary, binary's second body, extra black holes} are
// active this frame -- built ONCE per RenderImage call, not per pixel.
// Index assignment (and therefore PhotonTraceResult::diskBodyIndex, and
// settings.multiBodyConfig's per-index disk radii, which the caller --
// App.cpp -- must fill in using this SAME order): 0 = primary, 1 =
// binary's second body (only present if settings.binaryEnabled), then
// extraBlackHoles in order.
int BuildLensingBodies(const RenderSettings& settings, std::array<LensingBody, Physics::kMaxLensingBodies>& bodies) {
    int count = 0;
    const double captureMultiplier = settings.geodesicConfig.captureMultiplier;

    // The primary is NOT fixed at the origin once binary mode is also
    // active -- BinarySystem::Position1() shows body 1 (the primary) orbits
    // the shared barycenter right alongside body 2, both offset from the
    // origin. Only when binary mode is off does the primary sit still at
    // the origin (the ordinary single-hole case).
    const glm::dvec3 primaryPos = settings.binaryEnabled ? settings.binarySystem.Position1() : glm::dvec3(0.0);
    bodies[static_cast<size_t>(count++)] = LensingBody{primaryPos, 1.0, 2.0, captureMultiplier * 2.0};

    if (settings.binaryEnabled && count < Physics::kMaxLensingBodies) {
        const double rs2 = settings.binarySystem.SchwarzschildRadius2();
        bodies[static_cast<size_t>(count++)] =
            LensingBody{settings.binarySystem.Position2(), settings.binarySystem.massRatio, rs2, captureMultiplier * rs2};
    }

    for (const Physics::ExtraBlackHole& extra : settings.extraBlackHoles) {
        if (count >= Physics::kMaxLensingBodies) break;
        const double rs = extra.SchwarzschildRadiusSim();
        bodies[static_cast<size_t>(count++)] = LensingBody{extra.position, extra.gmRatio, rs, captureMultiplier * rs};
    }

    return count;
}

} // namespace

std::vector<glm::vec3> RenderImage(const CameraFrame& camera, const RenderSettings& settings,
                                    const BackgroundSampler& background, const DiskSampler& disk) {
    std::vector<glm::vec3> image(static_cast<size_t>(settings.width) * settings.height, glm::vec3(0.0f));

    std::vector<int> rows(settings.height);
    std::iota(rows.begin(), rows.end(), 0);

    // Built once, shared (read-only) across every pixel's parallel trace --
    // see Physics::TracePhotonMultiBody's own header for why this path
    // (rather than the Schwarzschild/Kerr/Binary ones below) is used only
    // once 3+ bodies are active, leaving existing 1-2-body behavior
    // completely unchanged.
    std::array<LensingBody, Physics::kMaxLensingBodies> multiBodyBodies{};
    const int multiBodyCount = BuildLensingBodies(settings, multiBodyBodies);
    const bool useMultiBody = multiBodyCount >= 3;

    // Every pixel's geodesic trace is fully independent (no shared state),
    // so this is embarrassingly parallel -- the CPU-side equivalent of the
    // "one thread per ray" design the CUDA tracer will use in Stage 7.
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < settings.width; ++x) {
            // NDC in [-1, 1], y flipped so row 0 is the top of the image.
            const double ndcX = ((x + 0.5) / settings.width * 2.0 - 1.0) * camera.aspect * camera.tanHalfFovY;
            const double ndcY = (1.0 - (y + 0.5) / settings.height * 2.0) * camera.tanHalfFovY;

            const glm::dvec3 direction = glm::normalize(camera.forward + ndcX * camera.right + ndcY * camera.up);
            const Physics::PhotonTraceResult result =
                useMultiBody
                    ? TracePhotonMultiBody(camera.position, direction, multiBodyBodies.data(), multiBodyCount,
                                           settings.multiBodyConfig)
                : settings.binaryEnabled
                    ? TracePhotonBinary(camera.position, direction, settings.binarySystem, settings.binaryConfig)
                : settings.spin != 0.0
                    ? TracePhotonKerr(camera.position, direction, settings.spin, settings.kerrConfig)
                    : TracePhoton(camera.position, direction, settings.geodesicConfig);

            glm::vec3 color(0.0f); // captured or unresolved -> the black hole's shadow
            if (result.hitDisk && disk) {
                color = disk(result);
            } else if (result.fate == PhotonFate::ESCAPED) {
                color = background(result.escapeDirection);
            }

            image[static_cast<size_t>(y) * settings.width + x] = color;
        }
    });

    return image;
}

} // namespace RayTracing
