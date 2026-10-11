#pragma once

#include <vector>

#include <glm/glm.hpp>

namespace Physics {

enum class PhotonFate {
    ACTIVE,     // still being integrated
    CAPTURED,   // crossed inside the capture radius (just outside r_s)
    ESCAPED,    // reached the outer boundary
    UNRESOLVED, // hit the step budget without resolving (near-critical orbit, b ~ b_crit)
};

// Full record of one traced photon, used by both the Stage 2 debug view and
// (from Stage 3 on) the per-pixel ray tracer.
struct PhotonTraceResult {
    PhotonFate fate = PhotonFate::ACTIVE;

    double impactParameter = 0.0; // b = L/E, sim units, sign-free magnitude
    double closestApproach = 1e300; // minimum r reached along the path, sim units
    double totalDeflectionAngle = 0.0; // valid only when launched from r0 >> r_s (see GeodesicSolver.cpp)
    int steps = 0;

    // Direction (unit vector, world Cartesian) the ray was travelling in
    // when it crossed the escape radius -- i.e. the direction from the
    // black hole toward the point on the celestial sphere this ray
    // appears to have come from. Only meaningful when fate == ESCAPED;
    // this is exactly what the Stage 3 lensing renderer needs to sample
    // the background sky per pixel.
    glm::dvec3 escapeDirection{0.0};

    // Stage 6: set when GeodesicConfig::checkDisk is enabled and the ray
    // crosses the equatorial plane within [diskInnerRadius, diskOuterRadius]
    // before escaping or being captured -- the disk is treated as optically
    // thick, so integration stops at the first crossing. diskRedshiftFactor
    // is g = observed/emitted frequency ratio (gravitational + Doppler
    // combined; see GeodesicSolver.cpp), meaningful only when hitDisk is
    // true and only exact when relativisticEffects's inputs were used.
    bool hitDisk = false;
    glm::dvec3 diskHitPosition{0.0};
    double diskHitRadius = 0.0;
    double diskRedshiftFactor = 1.0;

    // Binary-only (set by TracePhotonBinary): which body's disk was hit,
    // 0 or 1 -- meaningless (left at its default 0) for Schwarzschild/Kerr,
    // which have only one body anyway. Lets the caller pick between two
    // separate AccretionDisk instances (different mass -> different real
    // Kelvin scale) when coloring a binary disk hit.
    int diskBodyIndex = 0;

    // Kerr-only (set by TracePhotonKerr; left at these defaults, unused,
    // by the Schwarzschild TracePhoton -- which has no separate Carter
    // constant and reports everything through impactParameter=b=L/E
    // alone). Exposed for the Photon Inspector UI (App.cpp) so a Kerr
    // ray's actual conserved quantities can be read off directly, not
    // just the single derived b.
    double kerrEnergyE = 0.0;
    double kerrAngularMomentumLz = 0.0;
    double kerrCarterQ = 0.0;

    std::vector<glm::dvec3> path; // populated only when recordPath is requested
};

} // namespace Physics
