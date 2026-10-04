#pragma once

#include <glm/glm.hpp>

#include "Photon.h"

namespace Physics {

struct GeodesicConfig {
    double schwarzschildRadius = 2.0; // always 2.0 in sim units; kept explicit rather than hardcoded at call sites
    double captureMultiplier = 1.001; // capture once r <= captureMultiplier * r_s
    double escapeRadius = 1.0e5;      // r beyond which a ray is considered to have reached the background
    double basePhiStep = 0.01;        // radians of phi per RK4 step before adaptive scaling
    double minPhiStep = 1.0e-6;
    double maxPhiStep = 0.02;
    int maxSteps = 200000;
    bool adaptiveStep = true;
    bool recordPath = false;
    int maxPathPoints = 2000; // decimated if recordPath is true

    // Stage 6: when enabled, integration stops early (before capture/escape)
    // at the first crossing of the equatorial (z=0) plane within
    // [diskInnerRadius, diskOuterRadius] -- see PhotonTraceResult::hitDisk.
    bool checkDisk = false;
    double diskInnerRadius = 6.0;
    double diskOuterRadius = 26.0;
    bool diskClockwise = true; // rotation sense as seen from +z, purely a visualization choice
};

// Integrates one photon's null geodesic through the equatorial-reduced
// Schwarzschild spacetime.
//
// Formulation: rather than integrating the 4 coupled geodesic equations in
// an affine parameter, we use the standard u=1/r reduction (see docs/physics.md
// and Wald/MTW/Carroll's treatment of Schwarzschild null geodesics):
//
//     (du/dphi)^2 = 1/b^2 - u^2 (1 - r_s u)
//     d^2u/dphi^2 = -u + (3/2) r_s u^2
//
// with b = L/E the conserved impact parameter. phi is monotonic along any
// geodesic with b != 0 (since dphi/dlambda = L/r^2 never changes sign), so
// it is always safe to use as the integration variable -- this is precisely
// why this formulation avoids the divide-by-zero that a naive dr/dlambda
// integration hits at a radial turning point.
//
// Stage 6 disk redshift (when checkDisk is set): at the disk crossing
// radius r, material in a circular equatorial orbit has locally-measured
// orbital speed v = 1/sqrt(r - r_s) (derived from the Schwarzschild circular
// timelike geodesic's coordinate angular velocity dphi/dt = sqrt(GM/r^3)
// projected into the local static observer's orthonormal frame -- this is
// also why v = 0.5c exactly at the ISCO r=3r_s, a well-known Schwarzschild
// result, used as a sanity check in tests/test_disk.cpp). The photon's own
// local propagation direction at the crossing point is recovered from the
// SAME conserved b used for the impact parameter (n_phi-hat(r) = b*sqrt(f(r))/r
// holds at any r along the geodesic, not just at the starting point).
// Combining the standard relativistic Doppler formula for the disk's motion
// with the static gravitational redshift factor sqrt(f(r)) gives the total
// frequency ratio g = observed/emitted (PhotonTraceResult::diskRedshiftFactor).
//
// origin/direction are given in ordinary Cartesian sim-space (direction is
// normalized internally); the initial impact parameter is derived from the
// exact static-observer tetrad at the origin, not a flat-space
// approximation, so this remains physically correct for a camera placed at
// any radius outside the horizon (see GeodesicSolver.cpp for the derivation).
PhotonTraceResult TracePhoton(const glm::dvec3& origin, const glm::dvec3& direction, const GeodesicConfig& config);

} // namespace Physics
