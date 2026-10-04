#pragma once

#include "Particle.h"

namespace Physics {

enum class IntegratorMethod {
    Euler,          // explicit (forward) Euler — 1st order, included as the
                    // deliberately-bad baseline for the error/stability comparison
    VelocityVerlet, // 2nd order, symplectic — good energy behavior for orbits
    RK4,            // classic 4th-order Runge-Kutta — used as the accurate reference
};

// Newtonian point-mass gravitational acceleration in simulation units, where
// the black hole has effective GM = 1 (see Constants.h derivation) and sits
// at the origin: a = -r_hat / r^2 = -position / |position|^3.
glm::dvec3 NewtonianAcceleration(const glm::dvec3& position);

// Advances one particle by dt (sim time units) using the given method.
// Does not itself decide capture/escape/unresolved — callers classify the
// resulting state against the black hole's Schwarzschild radius and an
// outer escape boundary.
void IntegrateStep(ParticleState& state, double dt, IntegratorMethod method);

} // namespace Physics
