#include "CPUTracer.h"

#include <algorithm>
#include <execution>
#include <numeric>

using Physics::PhotonFate;
using Physics::TracePhoton;

namespace RayTracing {

std::vector<glm::vec3> RenderImage(const CameraFrame& camera, const RenderSettings& settings,
                                    const BackgroundSampler& background, const DiskSampler& disk) {
    std::vector<glm::vec3> image(static_cast<size_t>(settings.width) * settings.height, glm::vec3(0.0f));

    std::vector<int> rows(settings.height);
    std::iota(rows.begin(), rows.end(), 0);

    // Every pixel's geodesic trace is fully independent (no shared state),
    // so this is embarrassingly parallel -- the CPU-side equivalent of the
    // "one thread per ray" design the CUDA tracer will use in Stage 7.
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < settings.width; ++x) {
            // NDC in [-1, 1], y flipped so row 0 is the top of the image.
            const double ndcX = ((x + 0.5) / settings.width * 2.0 - 1.0) * camera.aspect * camera.tanHalfFovY;
            const double ndcY = (1.0 - (y + 0.5) / settings.height * 2.0) * camera.tanHalfFovY;

            const glm::dvec3 direction = glm::normalize(camera.forward + ndcX * camera.right + ndcY * camera.up);
            const Physics::PhotonTraceResult result = TracePhoton(camera.position, direction, settings.geodesicConfig);

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
