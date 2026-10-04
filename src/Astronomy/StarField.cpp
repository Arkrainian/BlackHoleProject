#include "StarField.h"

#include <cmath>

namespace Astronomy {

namespace {

// A small integer hash (murmur3-style finalizer mixing) used to turn a sky
// grid cell + a "salt" (which random quantity we want) into a deterministic
// pseudo-random 32-bit value. Not cryptographic, just needs to decorrelate
// nearby cells well enough to look like a starfield.
std::uint32_t Hash(std::int32_t x, std::int32_t y, std::int32_t salt, std::uint32_t seed) {
    std::uint32_t h = seed;
    h ^= static_cast<std::uint32_t>(x) * 0x9E3779B1u;
    h ^= static_cast<std::uint32_t>(y) * 0x85EBCA77u;
    h ^= static_cast<std::uint32_t>(salt) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

double HashToUnitFloat(std::uint32_t h) {
    return (h & 0x00FFFFFFu) / static_cast<double>(0x01000000u);
}

} // namespace

glm::vec3 StarField::Sample(const glm::dvec3& direction) const {
    const glm::dvec3 dir = glm::normalize(direction);

    // Standard spherical coordinates: theta = polar angle from +y, phi =
    // azimuth in the x-z plane. Used purely as an angular grid for star
    // placement (density does distort mildly near the poles, which is
    // visually irrelevant for a starfield background).
    const double theta = std::acos(glm::clamp(dir.y, -1.0, 1.0)); // [0, pi]
    const double phi = std::atan2(dir.z, dir.x) + 3.14159265358979323846; // [0, 2pi)

    const double cellSize = m_settings.cellSizeRadians;
    const std::int32_t cellTheta = static_cast<std::int32_t>(std::floor(theta / cellSize));
    const std::int32_t cellPhi = static_cast<std::int32_t>(std::floor(phi / cellSize));

    const double presenceRoll = HashToUnitFloat(Hash(cellTheta, cellPhi, 0, m_settings.seed));
    if (presenceRoll >= m_settings.density) {
        return glm::vec3(0.0f); // empty sky cell
    }

    // Jitter the star's position within its cell so the field doesn't look
    // like a grid, plus per-star brightness/color-temperature variation.
    const double jitterU = HashToUnitFloat(Hash(cellTheta, cellPhi, 1, m_settings.seed));
    const double jitterV = HashToUnitFloat(Hash(cellTheta, cellPhi, 2, m_settings.seed));
    const double brightnessRoll = HashToUnitFloat(Hash(cellTheta, cellPhi, 3, m_settings.seed));
    const double colorRoll = HashToUnitFloat(Hash(cellTheta, cellPhi, 4, m_settings.seed));

    const double starTheta = (cellTheta + jitterU) * cellSize;
    const double starPhi = (cellPhi + jitterV) * cellSize - 3.14159265358979323846;

    const glm::dvec3 starDir(
        std::sin(starTheta) * std::cos(starPhi),
        std::cos(starTheta),
        std::sin(starTheta) * std::sin(starPhi));

    const double angularDistance = std::acos(glm::clamp(glm::dot(dir, starDir), -1.0, 1.0));

    // A small fraction of stars are "hero" stars -- noticeably bigger and
    // brighter, breaking up what would otherwise be a visually uniform haze
    // of same-sized points and giving the field some depth/variety (most
    // real starfields read as a few standout bright stars among a much
    // larger faint population, not a uniform density of identical dots).
    const double heroRoll = HashToUnitFloat(Hash(cellTheta, cellPhi, 5, m_settings.seed));
    const bool isHeroStar = heroRoll < 0.035;
    // Kept well under half the cell size (this function only tests the one
    // candidate star belonging to the sampled direction's own grid cell, no
    // neighbor search) so a hero star's disc doesn't spill past its cell
    // boundary and clip visibly.
    const double sizeMultiplier = isHeroStar ? 1.8 : 1.0;
    const double heroBrightnessMultiplier = isHeroStar ? 3.0 : 1.0;

    const double starRadius = m_settings.angularStarRadius * sizeMultiplier;
    if (angularDistance > starRadius) {
        return glm::vec3(0.0f);
    }

    const double falloff = 1.0 - angularDistance / starRadius; // 1 at center, 0 at edge
    const double intensity =
        falloff * falloff * (0.4 + 0.6 * brightnessRoll) * m_settings.brightness * heroBrightnessMultiplier;

    // Warm (reddish) to cool (bluish) color temperature variation, loosely
    // evoking real stellar color diversity without claiming to model it.
    const glm::vec3 warm(1.0f, 0.85f, 0.65f);
    const glm::vec3 cool(0.75f, 0.85f, 1.0f);
    const glm::vec3 color = glm::mix(warm, cool, static_cast<float>(colorRoll));

    return color * static_cast<float>(intensity);
}

} // namespace Astronomy
