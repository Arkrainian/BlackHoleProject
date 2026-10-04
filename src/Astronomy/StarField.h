#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Astronomy {

// A procedural background starfield sampled by direction (i.e. a skybox
// with no texture data). Deterministic: the same direction always returns
// the same star, so ray-traced lensing distortion is the only thing that
// changes the image between frames, not sampling noise. Not astronomical
// data -- purely a stand-in "background scene" for Stage 3's lensing
// demonstration, per the project brief's "place a background star field or
// background image" requirement (an image-based background can be added
// later as an alternative sampler with the same Sample() signature).
class StarField {
public:
    struct Settings {
        double density = 0.04;            // probability a given sky cell contains a star, [0,1]
        double brightness = 1.15;         // overall multiplier
        double angularStarRadius = 0.003; // radians; controls apparent star size
        double cellSizeRadians = 0.012;   // angular grid resolution used for star placement -- smaller = more, closer-packed stars
        std::uint32_t seed = 1337;
    };

    explicit StarField(const Settings& settings = {}) : m_settings(settings) {}

    // Returns the sky color in the given direction (unit vector). Black
    // where there is no star.
    glm::vec3 Sample(const glm::dvec3& direction) const;

    void SetSettings(const Settings& settings) { m_settings = settings; }
    const Settings& GetSettings() const { return m_settings; }

private:
    Settings m_settings;
};

} // namespace Astronomy
