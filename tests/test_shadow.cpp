#include "TestFramework.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "Physics/BlackHole.h"
#include "Physics/GeodesicSolver.h"
#include "Physics/SchwarzschildMetric.h"

using namespace TestFramework;
using namespace Physics;

// Stage 5: the event horizon (r = r_s, a property of spacetime itself) and
// the black-hole SHADOW (the dark disk an observer actually sees, an
// observer-dependent angular size) are not the same thing. This file makes
// that distinction quantitative rather than just asserted in a comment.
//
// For a static observer at areal radius r0, the exact apparent angular
// radius of the shadow follows from the same tetrad relation GeodesicSolver
// uses for the impact parameter (b = r0 * sin(theta) / sqrt(f(r0)), where
// theta is the angle between the ray and the direction straight at the
// black hole): the shadow boundary is where b = b_crit, so
//
//     sin(theta_shadow) = b_crit * sqrt(f(r0)) / r0
//
// This is the standard result for the Schwarzschild shadow's angular size
// (see e.g. Synge 1966's original "escape cones" analysis). It reduces to
// theta_shadow ~= b_crit/r0 only in the r0 -> infinity limit; at finite r0
// it is measurably larger than that -- and dramatically larger than the
// horizon's own naive flat-space angular size asin(r_s/r0), which is the
// point this test exists to demonstrate.

namespace {

// Traces a ray from a static observer at (r0, 0, 0) aimed at angle theta
// from the direction straight at the black hole (theta=0 is purely radial
// inward), and returns whether it escapes.
PhotonFate TraceAtAngle(double r0, double theta, const GeodesicConfig& config) {
    const glm::dvec3 origin(r0, 0.0, 0.0);
    const glm::dvec3 direction(-std::cos(theta), std::sin(theta), 0.0);
    return TracePhoton(origin, direction, config).fate;
}

// Bisects for the critical angle separating ESCAPED from CAPTURED/UNRESOLVED
// (the latter two both count as "shadow" per the project's definition: any
// ray that fails to reach the background). Assumes escape is monotonic in
// theta on [thetaLow, thetaHigh], which holds here since b is monotonic in
// theta.
double BisectShadowBoundary(double r0, double thetaLow, double thetaHigh, const GeodesicConfig& config) {
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (thetaLow + thetaHigh);
        const PhotonFate fate = TraceAtAngle(r0, mid, config);
        if (fate == PhotonFate::ESCAPED) {
            thetaHigh = mid;
        } else {
            thetaLow = mid;
        }
    }
    return 0.5 * (thetaLow + thetaHigh);
}

} // namespace

void RunShadowTests() {
    const double rs = BlackHole::SchwarzschildRadiusSim();
    const double bCrit = BlackHole::CriticalImpactParameterSim();

    GeodesicConfig config;
    config.escapeRadius = 1.0e6;
    config.maxSteps = 200000;
    config.basePhiStep = 0.005;

    // Two observer distances: one moderately close (strong relativistic
    // correction) and one far away (should approach the asymptotic formula).
    const double observerDistances[] = {10.0, 1000.0};

    for (double r0 : observerDistances) {
        const double lapse = Schwarzschild::LapseFactor(r0, rs);
        const double predictedShadowAngle = std::asin(std::clamp(bCrit * std::sqrt(lapse) / r0, -1.0, 1.0));
        const double naiveHorizonAngle = std::asin(std::clamp(rs / r0, -1.0, 1.0));

        const double numericShadowAngle = BisectShadowBoundary(r0, 0.0, 3.14159265358979323846 / 2.0, config);

        std::printf("  r0=%.0f: predicted shadow angle=%.6f rad, numeric=%.6f rad, naive horizon angle=%.6f rad\n",
                    r0, predictedShadowAngle, numericShadowAngle, naiveHorizonAngle);

        ExpectNear(numericShadowAngle, predictedShadowAngle, 1e-4,
                    "Numerically-bisected shadow boundary matches the exact shadow-angle formula at r0=" + std::to_string(r0));

        // The core Stage 5 claim, made quantitative: the shadow is
        // substantially larger than the horizon's own naive angular size --
        // they are not the same thing. Both angles shrink toward 0 as
        // r0 -> infinity, so the meaningful comparison is their RATIO, not
        // their absolute difference: it should stay near b_crit/r_s =
        // 3*sqrt(3)/2 ~= 2.598 at any observer distance (in the small-angle
        // limit both formulas are directly proportional to 1/r0, so the
        // ratio becomes theta_shadow/theta_horizon ~= b_crit/r_s exactly).
        ExpectTrue(predictedShadowAngle > 2.0 * naiveHorizonAngle,
                    "Shadow angular radius is at least 2x the horizon's naive angular size at r0=" + std::to_string(r0));
    }

    // As r0 -> infinity, the exact formula must reduce to the textbook
    // asymptotic one, theta_shadow ~= b_crit / r0 (small-angle limit).
    {
        const double r0 = 1.0e6;
        const double lapse = Schwarzschild::LapseFactor(r0, rs);
        const double predictedShadowAngle = std::asin(bCrit * std::sqrt(lapse) / r0);
        const double asymptoticFormula = bCrit / r0;
        ExpectNear(predictedShadowAngle, asymptoticFormula, 1e-9,
                    "Exact shadow-angle formula reduces to b_crit/r0 for a very distant observer");
    }
}
