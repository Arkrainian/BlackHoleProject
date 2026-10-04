#include "TestFramework.h"

#include "Physics/BlackHole.h"
#include "Physics/Constants.h"

using namespace TestFramework;

// Validates BlackHole's characteristic radii against the closed-form
// Schwarzschild predictions (Stage 1 only needs the sim-unit values; the
// SI-conversion check exercises Constants::SimulationUnits separately).
void RunSchwarzschildTests() {
    ExpectNear(Physics::BlackHole::SchwarzschildRadiusSim(), 2.0, 1e-12,
               "Schwarzschild radius r_s = 2GM/c^2 equals 2.0 in sim units");

    ExpectNear(Physics::BlackHole::PhotonSphereRadiusSim(), 3.0, 1e-12,
               "Photon sphere r_ph = 3GM/c^2 equals 3.0 in sim units");

    ExpectNear(Physics::BlackHole::PhotonSphereRadiusSim(),
               1.5 * Physics::BlackHole::SchwarzschildRadiusSim(), 1e-12,
               "Photon sphere equals 1.5 * Schwarzschild radius");

    // b_crit = 3*sqrt(3) * GM/c^2 ~= 5.196152...
    ExpectNear(Physics::BlackHole::CriticalImpactParameterSim(), 5.196152422706632, 1e-9,
               "Critical impact parameter b_crit = 3*sqrt(3) GM/c^2");

    Physics::BlackHole sun(1.0);
    // Known value: Schwarzschild radius of the Sun is ~2.95 km.
    ExpectNear(sun.SchwarzschildRadiusMeters(), 2954.06, 1.0,
               "Schwarzschild radius of a 1 solar mass black hole is ~2954 m");
}
