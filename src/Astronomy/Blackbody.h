#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace Astronomy {

// A simplified, perceptual approximation of the blackbody color locus (NOT
// an exact spectral integration of the Planck function against CIE
// color-matching curves -- that would need real spectral rendering). Maps
// an effective Kelvin-ish temperature through a small set of hand-picked
// keypoints -- cool red/orange, through warm white, to blue-white hot -- and
// linearly interpolates between them on a log scale. Good enough for
// visually distinguishing temperature without claiming spectral accuracy.
//
// Factored out of AccretionDisk.cpp (where it originated) so Universe/
// StarCatalog.h's real-star rendering can reuse the exact same approximation
// instead of a second, independently-drifting color ramp.
inline glm::vec3 ApproximateBlackbodyHue(double kelvin) {
    struct Keypoint { double k; glm::vec3 color; };
    static const Keypoint kPoints[] = {
        {1000.0, {1.00f, 0.20f, 0.02f}},
        {2500.0, {1.00f, 0.40f, 0.10f}},
        {4000.0, {1.00f, 0.65f, 0.35f}},
        {5500.0, {1.00f, 0.85f, 0.65f}},
        {6600.0, {1.00f, 1.00f, 1.00f}},
        {10000.0, {0.75f, 0.85f, 1.00f}},
        {20000.0, {0.60f, 0.72f, 1.00f}},
    };
    constexpr int n = sizeof(kPoints) / sizeof(kPoints[0]);

    kelvin = std::clamp(kelvin, kPoints[0].k, kPoints[n - 1].k);
    for (int i = 0; i + 1 < n; ++i) {
        if (kelvin <= kPoints[i + 1].k) {
            const double t = std::log(kelvin / kPoints[i].k) / std::log(kPoints[i + 1].k / kPoints[i].k);
            return glm::mix(kPoints[i].color, kPoints[i + 1].color, static_cast<float>(t));
        }
    }
    return kPoints[n - 1].color;
}

} // namespace Astronomy
