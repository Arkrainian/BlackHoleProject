#pragma once

#include <glm/glm.hpp>

namespace Physics {

// Closed-form Newtonian (Keplerian) orbit for a test particle around a unit
// point mass (GM = 1 in sim units — see Constants.h). Used as the analytic
// ground truth that Stage 1's Euler/Verlet/RK4 integrators are checked
// against: for pure 1/r^2 gravity the true orbit is an exact conic section,
// so any drift away from this curve is purely numerical integration error,
// not a modeling error.
struct KeplerElements {
    double semiLatusRectum = 0.0; // p = L^2 / GM
    double eccentricity = 0.0;    // e = sqrt(1 + 2*E*L^2 / GM^2)
    glm::dvec3 periapsisDir{1.0, 0.0, 0.0}; // unit vector toward periapsis (from Laplace-Runge-Lenz vector)
    glm::dvec3 orbitNormal{0.0, 0.0, 1.0};  // unit vector normal to the orbital plane
};

// Derives the conic-section orbit elements from an initial state, assuming
// Newtonian gravity with GM = 1 and the black hole fixed at the origin.
KeplerElements ComputeKeplerElements(const glm::dvec3& position, const glm::dvec3& velocity);

// Evaluates the analytic orbit radius at true anomaly theta (radians,
// measured from the periapsis direction within the orbital plane):
// r(theta) = p / (1 + e*cos(theta)).
// Returns a negative value if the orbit never reaches that anomaly
// (e.g. beyond a hyperbolic orbit's asymptote).
double KeplerRadiusAtTrueAnomaly(const KeplerElements& elements, double trueAnomaly);

// Given a current position on (or near) the orbit, returns the true anomaly
// theta implied by its direction relative to the periapsis direction. Used
// to compare a numerically-integrated point against the analytic radius at
// the same angle.
double KeplerTrueAnomalyOf(const KeplerElements& elements, const glm::dvec3& position);

} // namespace Physics
