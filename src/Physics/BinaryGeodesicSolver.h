#pragma once

#include <glm/glm.hpp>

#include "BinarySystem.h"
#include "Photon.h"

namespace Physics {

struct BinaryGeodesicConfig {
    double captureMultiplier = 1.001; // capture once within captureMultiplier * either body's Schwarzschild radius
    double escapeRadius = 1.0e4;      // distance from the barycenter beyond which a ray is considered escaped
    // Step size h = clamp(baseStep * (distance to the nearest mass), minStep,
    // maxStep) -- grows far from both masses, shrinks close to either one,
    // the same trick KerrGeodesicSolver.cpp uses (step proportional to r)
    // and for the same reason: a FIXED step would need a prohibitive step
    // count to cross a large escapeRadius (this was measured directly --
    // see tests/test_binary.cpp's history for why this isn't a hypothetical
    // concern), while a step proportional to distance keeps the total
    // step count roughly logarithmic in escapeRadius/launch-distance
    // instead of linear in it.
    double baseStep = 0.05;
    double minStep = 1.0e-4;
    double maxStep = 1.0e4;
    int maxSteps = 20000;

    // Each body gets its own thin equatorial disk, coplanar with the
    // orbital plane (z=0, same plane BinarySystem::Position1()/Position2()
    // already live in) -- a simplification, not a claim that a real binary
    // disk is always exactly coplanar with the orbit. Both disks are
    // treated independently: a ray can only hit one (whichever it crosses
    // z=0 within range of first), same "optically thick, stop at first
    // crossing" rule the Schwarzschild/Kerr disks use.
    bool checkDisk = false;
    double disk1InnerRadius = 6.0, disk1OuterRadius = 26.0;
    double disk2InnerRadius = 6.0, disk2OuterRadius = 26.0;
};

// Traces one photon through the field of the two point masses in `system`,
// at system's CURRENT phase (see BinarySystem::Position1()/Position2() --
// the caller is responsible for advancing system.phase over time via
// BinarySystem::AdvancePhase; this function reads it as-is and holds it
// fixed for one photon's trace, see the "frozen lens" note below) using a
// deliberately approximate scheme: EACH mass's light-bending effect is
// modeled as a Newtonian-like acceleration of TWICE the Newtonian
// magnitude (2GM/r^2, directed toward that mass), superposed (summed)
// across both masses, applied to a photon moving at constant coordinate
// speed 1. The "double the Newtonian deflection" trick is the standard,
// well-known way to recover the correct GR weak-field light-bending angle
// (4GM/(bc^2), the same formula tests/test_deflection.cpp validates for
// the real Schwarzschild solver) from a cheap, easy-to-superpose
// Newtonian-shaped force law -- see docs/physics.md Sec. 11 for the full
// derivation, the single-mass reduction this is checked against in
// tests/test_binary.cpp, and the explicit list of what this does NOT
// capture (there is no known closed-form exact binary-black-hole metric
// at all, so "approximate" here is not a simplification of a more exact
// method available in this project -- it is the best tractable option):
//   - No exact strong-field physics: the real photon sphere, exact
//     critical impact parameter, and frame dragging near either hole are
//     not reproduced quantitatively, only a qualitatively plausible
//     capture near each mass.
//   - "Frozen lens": each mass's position is evaluated once at launch
//     time and held fixed for that photon's entire flight (the standard
//     simplification for light-crossing times much shorter than the
//     orbital period -- not re-evaluated as the binary continues to
//     orbit during a single photon's traversal).
//   - No gravitational radiation / inspiral: BinarySystem is a fixed
//     circular orbit, not a decaying one.
//   - Disk redshift uses the already-tracked velocity vector directly
//     (not a reconstructed conserved impact parameter the way the
//     Schwarzschild/Kerr solvers do it), combined with the same
//     Schwarzschild-shaped local-orbital-speed/gravitational-redshift
//     formulas centered on whichever body the disk belongs to -- see
//     TracePhotonBinary's definition for the derivation.
PhotonTraceResult TracePhotonBinary(const glm::dvec3& origin, const glm::dvec3& direction, const BinarySystem& system,
                                     const BinaryGeodesicConfig& config);

} // namespace Physics
