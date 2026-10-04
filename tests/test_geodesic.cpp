#include "TestFramework.h"

#include <glm/glm.hpp>

#include "Physics/BlackHole.h"
#include "Physics/GeodesicSolver.h"

using namespace TestFramework;
using namespace Physics;

namespace {

GeodesicConfig DefaultTestConfig() {
    GeodesicConfig config;
    config.escapeRadius = 1.0e6;
    config.maxSteps = 300000;
    config.basePhiStep = 0.005;
    return config;
}

} // namespace

void RunGeodesicTests() {
    const double bCrit = BlackHole::CriticalImpactParameterSim();
    const double r0 = 1.0e4; // effectively "from infinity" relative to r_s = 2

    // Ray aimed inward with b just below the critical impact parameter
    // should be captured.
    {
        const double b = 0.98 * bCrit;
        GeodesicConfig config = DefaultTestConfig();
        const PhotonTraceResult result = TracePhoton(glm::dvec3(-r0, b, 0.0), glm::dvec3(1.0, 0.0, 0.0), config);
        ExpectTrue(result.fate == PhotonFate::CAPTURED,
                   "b = 0.98 * b_crit is captured by the black hole");
    }

    // Ray aimed inward with b just above the critical impact parameter
    // should escape (after strong deflection).
    {
        const double b = 1.02 * bCrit;
        GeodesicConfig config = DefaultTestConfig();
        const PhotonTraceResult result = TracePhoton(glm::dvec3(-r0, b, 0.0), glm::dvec3(1.0, 0.0, 0.0), config);
        ExpectTrue(result.fate == PhotonFate::ESCAPED,
                   "b = 1.02 * b_crit escapes the black hole");
    }

    // A photon launched exactly tangentially at the photon sphere (r = 3GM/c^2
    // = 1.5 r_s) should have an impact parameter matching b_crit to high
    // precision -- this is the defining property of the photon sphere as the
    // unstable circular photon orbit, and a direct cross-check between
    // GeodesicSolver's tetrad-based impact-parameter formula and
    // BlackHole::CriticalImpactParameterSim()'s closed-form value.
    {
        const double rPhoton = BlackHole::PhotonSphereRadiusSim();
        GeodesicConfig config = DefaultTestConfig();
        const PhotonTraceResult result = TracePhoton(glm::dvec3(rPhoton, 0.0, 0.0), glm::dvec3(0.0, 1.0, 0.0), config);
        ExpectNear(result.impactParameter, bCrit, 1e-9,
                   "Tangential ray at the photon sphere has impact parameter b_crit");
    }

    // Sanity check the ESCAPED/CAPTURED classification never leaves a ray
    // ACTIVE or UNRESOLVED for impact parameters far from the critical value.
    {
        GeodesicConfig config = DefaultTestConfig();
        const PhotonTraceResult farEscape = TracePhoton(glm::dvec3(-r0, 5.0 * bCrit, 0.0), glm::dvec3(1.0, 0.0, 0.0), config);
        ExpectTrue(farEscape.fate == PhotonFate::ESCAPED, "A ray with b = 5*b_crit clearly escapes");

        const PhotonTraceResult farCapture = TracePhoton(glm::dvec3(-r0, 0.1 * bCrit, 0.0), glm::dvec3(1.0, 0.0, 0.0), config);
        ExpectTrue(farCapture.fate == PhotonFate::CAPTURED, "A ray with b = 0.1*b_crit is clearly captured");
    }
}
