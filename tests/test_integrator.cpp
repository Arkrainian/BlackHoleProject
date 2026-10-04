#include "TestFramework.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "Physics/KeplerOrbit.h"
#include "Physics/NewtonianIntegrator.h"

using namespace TestFramework;
using namespace Physics;

namespace {

// Integrates a circular orbit (r=5, v=sqrt(GM/r)=sqrt(1/5)) for one full
// period and returns the max |r(t) - r0| deviation -- for a perfectly
// circular orbit the exact answer is a constant radius, so any deviation is
// pure integration error. Returns the deviation for the requested method.
double CircularOrbitRadialDrift(IntegratorMethod method, double dt, int steps) {
    const double r0 = 5.0;
    const double v0 = std::sqrt(1.0 / r0); // circular orbit speed for GM=1

    ParticleState state;
    state.position = glm::dvec3(r0, 0.0, 0.0);
    state.velocity = glm::dvec3(0.0, v0, 0.0);

    double maxDrift = 0.0;
    for (int i = 0; i < steps; ++i) {
        IntegrateStep(state, dt, method);
        const double r = glm::length(state.position);
        maxDrift = std::max(maxDrift, std::abs(r - r0));
    }
    return maxDrift;
}

} // namespace

void RunIntegratorTests() {
    // A circular orbit's period is T = 2*pi*sqrt(r^3/GM) = 2*pi*sqrt(125) for r=5, GM=1.
    const double r0 = 5.0;
    const double period = 2.0 * 3.14159265358979323846 * std::sqrt(r0 * r0 * r0);
    const int steps = 2000;
    const double dt = period / steps;

    const double eulerDrift = CircularOrbitRadialDrift(IntegratorMethod::Euler, dt, steps);
    const double verletDrift = CircularOrbitRadialDrift(IntegratorMethod::VelocityVerlet, dt, steps);
    const double rk4Drift = CircularOrbitRadialDrift(IntegratorMethod::RK4, dt, steps);

    std::printf("  (radial drift over one orbit: Euler=%.5f  Verlet=%.5f  RK4=%.5f)\n",
                eulerDrift, verletDrift, rk4Drift);

    // Forward Euler is 1st-order and non-symplectic: it should visibly leak
    // energy over a full orbit, drifting far more than the higher-order
    // methods. This is exactly the "measure error and stability" comparison
    // Stage 1 is meant to demonstrate.
    ExpectTrue(eulerDrift > 10.0 * rk4Drift,
               "Euler's radial drift over one orbit is much larger than RK4's");
    ExpectTrue(eulerDrift > 3.0 * verletDrift,
               "Euler's radial drift over one orbit is much larger than Velocity Verlet's");

    // RK4 should track a circular orbit extremely tightly at this step count.
    ExpectTrue(rk4Drift < 0.01, "RK4 keeps a circular orbit's radius within 0.01 over one period");

    // Velocity Verlet is symplectic: it should stay bounded and not blow up,
    // even though it's only 2nd order (its drift is typically small and
    // oscillatory rather than growing).
    ExpectTrue(verletDrift < 0.5, "Velocity Verlet keeps a circular orbit's radius bounded over one period");

    // Cross-check against the analytic Kepler description directly: a
    // circular orbit has eccentricity 0 and semi-latus-rectum r0.
    const KeplerElements elements = ComputeKeplerElements(
        glm::dvec3(r0, 0.0, 0.0), glm::dvec3(0.0, std::sqrt(1.0 / r0), 0.0));
    ExpectNear(elements.eccentricity, 0.0, 1e-9, "Circular orbit has eccentricity ~0");
    ExpectNear(elements.semiLatusRectum, r0, 1e-9, "Circular orbit's semi-latus rectum equals its radius");
}
