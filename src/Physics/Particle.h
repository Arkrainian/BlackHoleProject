#pragma once

#include <glm/glm.hpp>

namespace Physics {

// State of a Stage-1 Newtonian test particle, in simulation units (see
// Constants.h): position/velocity around a black hole of unit GM sitting at
// the origin. Velocity is measured in units of c, so a "photon-like" test
// particle starts with |velocity| = 1.
struct ParticleState {
    glm::dvec3 position{0.0};
    glm::dvec3 velocity{0.0};
};

enum class ParticleFate {
    ACTIVE,     // still being integrated
    CAPTURED,   // fell within the Schwarzschild radius
    ESCAPED,    // crossed the outer boundary moving outward
    UNRESOLVED, // hit the step budget without resolving (near-critical orbit)
};

} // namespace Physics
