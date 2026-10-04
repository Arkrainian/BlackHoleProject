#include "KeplerOrbit.h"

#include <algorithm>
#include <cmath>

namespace Physics {

KeplerElements ComputeKeplerElements(const glm::dvec3& position, const glm::dvec3& velocity) {
    constexpr double GM = 1.0; // sim units convention, see Constants.h

    const glm::dvec3 angularMomentum = glm::cross(position, velocity); // L vector
    const double L2 = glm::dot(angularMomentum, angularMomentum);
    const double r = glm::length(position);
    const double v2 = glm::dot(velocity, velocity);

    // Specific orbital energy: E = v^2/2 - GM/r.
    const double energy = 0.5 * v2 - GM / r;

    KeplerElements elements;
    elements.semiLatusRectum = L2 / GM;
    elements.eccentricity = std::sqrt(std::max(0.0, 1.0 + 2.0 * energy * L2 / (GM * GM)));

    if (L2 > 1e-12) {
        elements.orbitNormal = glm::normalize(angularMomentum);
    }

    // Laplace-Runge-Lenz vector (points from focus toward periapsis):
    // A = v x L - GM * r_hat
    const glm::dvec3 lrl = glm::cross(velocity, angularMomentum) - GM * (position / r);
    const double lrlLen = glm::length(lrl);
    if (lrlLen > 1e-9) {
        elements.periapsisDir = lrl / lrlLen;
    } else {
        // Near-circular orbit: LRL vector is ill-defined, periapsis direction
        // is arbitrary. Fall back to the initial position direction so the
        // reference curve is still continuous and well-defined.
        elements.periapsisDir = glm::normalize(position);
    }

    return elements;
}

double KeplerRadiusAtTrueAnomaly(const KeplerElements& elements, double trueAnomaly) {
    const double denom = 1.0 + elements.eccentricity * std::cos(trueAnomaly);
    if (denom <= 1e-9) {
        return -1.0; // beyond the hyperbolic asymptote, orbit doesn't reach this angle
    }
    return elements.semiLatusRectum / denom;
}

double KeplerTrueAnomalyOf(const KeplerElements& elements, const glm::dvec3& position) {
    const glm::dvec3 radial = glm::normalize(position);
    const glm::dvec3 tangential = glm::cross(elements.orbitNormal, elements.periapsisDir);

    const double cosTheta = glm::clamp(glm::dot(radial, elements.periapsisDir), -1.0, 1.0);
    const double sinTheta = glm::dot(radial, tangential);

    return std::atan2(sinTheta, cosTheta);
}

} // namespace Physics
