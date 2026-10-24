#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "Blackbody.h"

// Procedural "stellar surface" texture generation, used by Star System
// View (App.cpp) to texture the visited star's sphere instead of leaving it
// a single flat color.
//
// Deliberately NOT real photographic surface imagery, and deliberately not
// a downloaded one either. Genuine resolved surface imagery exists for the
// Sun (space telescopes image its granulation directly) but for
// essentially no other star -- even the best current ground-based
// interferometry resolves only a handful of nearby giants (e.g.
// Betelgeuse) into a few blurry pixels, not a texture. Rather than pass off
// a real Sun photograph as "the texture" for a catalog of ~35 different
// stars, or silently use a real image for exactly one of them and a fake
// one for the rest with no visible distinction, every star in this app
// gets the same honestly-labeled procedural treatment: a random mottled
// "granulation" pattern (soft overlapping blobs, not a physically simulated
// convection model) tinted by the star's real effective temperature via
// Astronomy::ApproximateBlackbodyHue -- real color, invented surface detail.
namespace Astronomy {

// width*height RGB pixels, row-major top-to-bottom (the layout
// Rendering::Renderer::UploadImage expects). `seed` is deterministic (the
// same star always gets the same pattern across frames/launches) --
// typically derived from the star's catalog index.
inline std::vector<glm::vec3> GenerateStarTexture(double kelvin, unsigned int seed, int width = 256, int height = 128) {
    std::vector<glm::vec3> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
    const glm::vec3 baseHue = ApproximateBlackbodyHue(kelvin);

    struct Blob { double u, v, size, brightness; };
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_real_distribution<double> sizeDist(0.015, 0.05);

    constexpr int kBlobCount = 140;
    std::vector<Blob> blobs;
    blobs.reserve(kBlobCount);
    for (int i = 0; i < kBlobCount; ++i) {
        blobs.push_back({unit(rng), unit(rng), sizeDist(rng), unit(rng) * 0.5 + 0.7});
    }

    for (int y = 0; y < height; ++y) {
        const double v = (y + 0.5) / height;
        for (int x = 0; x < width; ++x) {
            const double u = (x + 0.5) / width;
            double brightness = 1.0;
            for (const Blob& blob : blobs) {
                double du = u - blob.u;
                du -= std::round(du); // wrap at the U seam so the sphere tiles seamlessly around longitude
                const double dv = v - blob.v;
                const double distSq = du * du + dv * dv;
                const double falloff = std::exp(-distSq / (blob.size * blob.size));
                brightness += (blob.brightness - 1.0) * falloff;
            }
            brightness = std::clamp(brightness, 0.55, 1.3);
            pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                baseHue * static_cast<float>(brightness);
        }
    }
    return pixels;
}

} // namespace Astronomy
