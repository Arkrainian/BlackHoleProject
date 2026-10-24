#pragma once

#include <array>

#include <glm/glm.hpp>

#include "MultiBlackHoleSystem.h"
#include "Photon.h"

namespace Physics {

struct MultiBodyGeodesicConfig {
    double escapeRadius = 1.0e4;
    double baseStep = 0.05;
    double minStep = 1.0e-4;
    double maxStep = 1.0e4;
    int maxSteps = 20000;

    // Parallel to the `bodies` array passed to TracePhotonMultiBody --
    // diskInnerRadius[i]/diskOuterRadius[i] describe body i's own disk
    // (same "each body gets its own thin equatorial, z=0-coplanar disk"
    // simplification BinaryGeodesicConfig already makes, generalized from
    // exactly two named disks to an array).
    bool checkDisk = false;
    std::array<double, kMaxLensingBodies> diskInnerRadius{};
    std::array<double, kMaxLensingBodies> diskOuterRadius{};
};

// Generalizes BinaryGeodesicSolver.h's TracePhotonBinary from exactly two
// named bodies to `bodyCount` (<= kMaxLensingBodies) bodies in `bodies` --
// the SAME approximate "doubled-Newtonian" superposition scheme (each
// body's light-bending modeled as 2GM/r^2 toward it, summed across every
// body, applied to a photon at constant coordinate speed 1), the same
// frozen-lens/no-exact-strong-field/no-gravitational-radiation caveats,
// and the same disk-crossing/redshift treatment per body -- see
// BinaryGeodesicSolver.h's own documentation for the full derivation and
// caveat list, which applies here verbatim with "either mass" generalized
// to "any active body."
//
// This is a genuinely separate implementation, not a refactor of
// TracePhotonBinary -- same pattern Schwarzschild/Kerr/Binary already use
// (no shared integration code between solvers, only the shared
// PhotonTraceResult return type) -- so existing single-hole/Kerr/binary
// behavior is provably unaffected by this addition. Used whenever 3 or
// more bodies are active at once (the primary plus binary's second body
// and/or 1+ ExtraBlackHoles); with exactly 1 or 2 bodies active, the
// existing Schwarzschild/Kerr/Binary solvers are used instead (see
// CPUTracer.cpp's dispatch).
PhotonTraceResult TracePhotonMultiBody(const glm::dvec3& origin, const glm::dvec3& direction,
                                        const LensingBody* bodies, int bodyCount,
                                        const MultiBodyGeodesicConfig& config);

} // namespace Physics
