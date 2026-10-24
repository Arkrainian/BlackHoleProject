#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Rendering {

// Three camera modes sharing one object so the rest of the app (ray tracer,
// debug renderer) only ever deals with one `Camera`:
//   - Orbit: looks at the origin (where the black hole sits) from a given
//     distance, yaw and pitch. This is what every scientific control
//     (Einstein ring alignment, disk presets, etc.) assumes -- the source
//     direction and camera direction are collinear with the black hole
//     only because Orbit always points at the origin.
//   - FreeFly: an ordinary fly-around camera (position + look yaw/pitch,
//     independent of the black hole's location) for exploring the scene.
//   - Jet: position/forward/up are set externally every frame from
//     Jet::FlightCamera (see App.cpp), which derives them from the piloted
//     jet's physics state (chase or cockpit view). This class doesn't know
//     anything about the jet itself -- it just carries whatever that system
//     computed through the same Position()/Forward()/ViewMatrix() path
//     every other mode uses, so the ray tracer and debug renderer need no
//     jet-specific code at all.
// Distances/positions are in simulation units (see Physics/Constants.h --
// G=c=1, GM/c^2=1 for the current black hole).
class Camera {
public:
    enum class Mode { Orbit, FreeFly, Jet };

    struct Basis {
        glm::dvec3 forward;
        glm::dvec3 right;
        glm::dvec3 up;
    };

    // Starts in FreeFly, far out from the black hole (disk/jet presets top
    // out around ~100-220 sim units -- see AccretionDisk::outerRadius and
    // the Orbit distance presets below), so the sim opens with room to fly
    // in rather than parked on top of the hole in Orbit mode.
    Mode mode = Mode::FreeFly;

    // --- Orbit state ---
    double distance = 30.0;
    double yawRadians = 0.0;
    double pitchRadians = 0.3;

    // --- Free-fly state ---
    glm::dvec3 freeFlyPosition{0.0, 50.0, 800.0};
    // Yaw = pi so Forward() (sin(yaw), ..., cos(yaw)) points back toward the
    // origin from +Z, not away from it -- see Forward() below.
    double freeFlyYawRadians = 3.14159265358979323846;
    double freeFlyPitchRadians = 0.0;

    // --- Jet state (written externally each frame, see App.cpp) ---
    glm::dvec3 jetPosition{0.0};
    glm::dvec3 jetForward{0.0, 0.0, -1.0};
    glm::dvec3 jetUp{0.0, 1.0, 0.0}; // the jet's own up, not world up -- lets the chase cam roll with it

    double fovYDegrees = 45.0;

    glm::dvec3 Position() const {
        if (mode == Mode::FreeFly) {
            return freeFlyPosition;
        }
        if (mode == Mode::Jet) {
            return jetPosition;
        }
        const double cp = std::cos(pitchRadians);
        return glm::dvec3(
            distance * cp * std::sin(yawRadians),
            distance * std::sin(pitchRadians),
            distance * cp * std::cos(yawRadians));
    }

    // Direction the camera looks. Orbit always faces the origin; FreeFly
    // faces wherever its own yaw/pitch point; Jet faces wherever
    // Jet::FlightCamera last pointed it, independent of position.
    glm::dvec3 Forward() const {
        if (mode == Mode::FreeFly) {
            const double cp = std::cos(freeFlyPitchRadians);
            return glm::normalize(glm::dvec3(
                cp * std::sin(freeFlyYawRadians),
                std::sin(freeFlyPitchRadians),
                cp * std::cos(freeFlyYawRadians)));
        }
        if (mode == Mode::Jet) {
            return glm::normalize(jetForward);
        }
        return glm::normalize(-Position());
    }

    // Orthonormal basis for the current Forward(), picking a fallback
    // world-up when looking nearly along the up hint to avoid a degenerate
    // cross product. Orbit/FreeFly use world-up; Jet uses the jet's own up
    // so the chase camera rolls with the aircraft instead of re-leveling.
    Basis OrthonormalBasis() const {
        const glm::dvec3 forward = Forward();
        glm::dvec3 up = (mode == Mode::Jet) ? glm::normalize(jetUp) : glm::dvec3(0.0, 1.0, 0.0);
        if (std::abs(glm::dot(forward, up)) > 0.999) {
            up = glm::dvec3(0.0, 0.0, 1.0);
        }
        Basis basis;
        basis.forward = forward;
        basis.right = glm::normalize(glm::cross(forward, up));
        basis.up = glm::cross(basis.right, forward);
        return basis;
    }

    glm::mat4 ViewMatrix() const {
        const glm::dvec3 position = Position();
        const Basis basis = OrthonormalBasis();
        return glm::lookAt(glm::vec3(position), glm::vec3(position + basis.forward), glm::vec3(basis.up));
    }

    glm::mat4 ProjectionMatrix(float aspectRatio) const {
        return glm::perspective(glm::radians(static_cast<float>(fovYDegrees)), aspectRatio, 0.05f, 5000.0f);
    }
};

} // namespace Rendering
