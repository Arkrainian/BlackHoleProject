#pragma once

#include <cmath>

#include "Constants.h"

// Diagnostic tidal-disruption formulas (spec sections 28.3/28.4). Both
// functions here are explicitly labeled weak-field Newtonian approximations
// -- they exist so the UI can show the user a number to compare the actual
// simulated spring-network behavior (Astronomy/PlanetBody.h) against, NOT to
// drive or gate that behavior. A planet in this simulator deforms and
// fractures because its internal springs are actually stressed past their
// strength threshold; these formulas never touch that logic.
namespace Physics {

// Difference in radial gravitational acceleration between a point at the
// near and far edge of a body of characteristic radius R_p, a distance r
// from a point mass M_bh, to leading order in R_p/r:
//   delta_a_tidal ~= 2 * G * M_bh * R_p / r^3
// Takes blackHoleMassKg (SI) and planetRadiusSim/rSim in simulation units
// (GM_bh/c^2 = 1), and returns the tidal acceleration in sim units
// (lengths in units of GM_bh/c^2, time in units of GM_bh/c^3) so it's
// directly comparable to Physics::StaticObserverGravityAccelSim's output.
// Because sim units already absorb G*M_bh into the length scale, this
// reduces to 2*R_p/r^3 once both lengths are given in sim units --
// blackHoleMassKg is accepted anyway (and unused in the sim-unit formula)
// so the signature stays self-documenting and this function reads the same
// way at every call site regardless of which quantities happen to cancel.
inline double NewtonianTidalAccelerationSim(double /*blackHoleMassKg*/, double planetRadiusSim, double rSim) {
    if (rSim <= 0.0) return 0.0;
    return 2.0 * planetRadiusSim / (rSim * rSim * rSim);
}

// Classical fluid-body Roche limit (Newtonian, idealized -- see spec 28.4):
//   r_Roche ~= 2.44 * R_p * (M_bh / M_p)^(1/3)
// This is an informative reference radius, not a destruction switch: real
// disruption depends on material strength, rotation, orbital eccentricity,
// and the relativistic field near the hole, none of which this formula
// knows about. planetRadiusSim is in sim units (same convention as above);
// the mass ratio is dimensionless so it doesn't matter that blackHoleMassKg
// and planetMassKg are both SI kilograms. Returns the Roche radius in sim
// units.
inline double RocheLimitSim(double planetRadiusSim, double blackHoleMassKg, double planetMassKg) {
    if (planetMassKg <= 0.0) return 0.0;
    const double massRatio = blackHoleMassKg / planetMassKg;
    return 2.44 * planetRadiusSim * std::cbrt(massRatio);
}

} // namespace Physics
