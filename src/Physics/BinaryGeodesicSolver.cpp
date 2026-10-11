#include "BinaryGeodesicSolver.h"

#include <algorithm>
#include <cmath>

namespace Physics {

namespace {

// Superposed "doubled-Newtonian" acceleration -- see BinaryGeodesicSolver.h
// for why this specific factor of 2 is the right one, not an arbitrary
// tuning constant.
glm::dvec3 Accel(const glm::dvec3& pos, const glm::dvec3& body1Pos, double gm1, const glm::dvec3& body2Pos,
                  double gm2) {
    glm::dvec3 a(0.0);
    const glm::dvec3 d1 = pos - body1Pos;
    const double r1 = glm::length(d1);
    if (r1 > 1e-6) {
        a += -2.0 * gm1 * d1 / (r1 * r1 * r1);
    }
    const glm::dvec3 d2 = pos - body2Pos;
    const double r2 = glm::length(d2);
    if (r2 > 1e-6) {
        a += -2.0 * gm2 * d2 / (r2 * r2 * r2);
    }
    return a;
}

// Checks whether the segment prevPos->pos crosses z=0 within the given
// body's disk annulus; if so, fills in the disk-hit fields of `result`
// (using rs for THIS body) and returns true. bodyIndex is written into
// result.diskBodyIndex so the caller can pick the right AccretionDisk
// instance (different mass -> different real Kelvin scale) when coloring.
bool CheckDiskCrossing(const glm::dvec3& prevPos, const glm::dvec3& pos, const glm::dvec3& vel,
                        const glm::dvec3& bodyPos, double rs, double innerRadius, double outerRadius, int bodyIndex,
                        PhotonTraceResult& result) {
    if ((prevPos.z > 0.0) == (pos.z > 0.0)) {
        return false; // no z=0 crossing this step
    }
    const double t = prevPos.z / (prevPos.z - pos.z);
    const glm::dvec3 hitPos = prevPos + t * (pos - prevPos);
    const double hitR = glm::length(hitPos - bodyPos);

    if (hitR < innerRadius || hitR > outerRadius) {
        return false;
    }

    // Local circular-orbit speed and gravitational redshift, centered on
    // THIS body -- same Schwarzschild-shaped formulas the exact
    // Schwarzschild/Kerr solvers use (v=1/sqrt(r-rs), g_grav=sqrt(1-rs/r)),
    // applied locally around whichever mass owns this disk, consistent
    // with the overall "superposed single-body physics" approximation
    // this whole scheme already uses for light bending.
    const double vLocal = 1.0 / std::sqrt(std::max(1e-6, hitR - rs));
    const double gammaLocal = 1.0 / std::sqrt(std::max(1e-9, 1.0 - vLocal * vLocal));

    // Disk material's local velocity direction: tangential around THIS
    // body, in the same rotational sense the binary itself orbits in
    // (BinarySystem::Position1/2 move as phase increases via
    // cross(+z, position) -- see BinarySystem::AdvancePhase), not an
    // independently-configurable clockwise/counterclockwise choice the
    // way the single-body disk's diskClockwise flag is -- a disk's spin
    // direction isn't free to pick independently of the orbit it's
    // embedded in.
    const glm::dvec3 diskVelocityDir = glm::normalize(glm::cross(glm::dvec3(0.0, 0.0, 1.0), hitPos - bodyPos));

    // The photon's own propagation direction is already tracked directly
    // (unlike the Schwarzschild/Kerr solvers, which reconstruct it from a
    // conserved impact parameter) -- `vel` at the step where the crossing
    // was detected is a fine approximation of its direction AT the
    // crossing, consistent with this scheme's general precision level.
    const double cosPsi = glm::dot(vel, diskVelocityDir);
    const double dopplerFactor = 1.0 / (gammaLocal * (1.0 - vLocal * cosPsi));
    const double gravitationalFactor = std::sqrt(std::max(0.0, 1.0 - rs / hitR));

    result.hitDisk = true;
    result.diskHitPosition = hitPos;
    result.diskHitRadius = hitR;
    result.diskRedshiftFactor = gravitationalFactor * dopplerFactor;
    result.diskBodyIndex = bodyIndex;
    return true;
}

} // namespace

PhotonTraceResult TracePhotonBinary(const glm::dvec3& origin, const glm::dvec3& direction, const BinarySystem& system,
                                     const BinaryGeodesicConfig& config) {
    PhotonTraceResult result;

    // "Frozen lens": both masses' positions are read once, here, at
    // system's current phase, and held fixed for this photon's entire
    // flight -- see BinaryGeodesicSolver.h.
    const glm::dvec3 body1Pos = system.Position1();
    const glm::dvec3 body2Pos = system.Position2();
    const double gm1 = 1.0; // sim units, see BlackHole.h / BinarySystem.h convention
    const double gm2 = system.massRatio;
    const double rs1 = system.SchwarzschildRadius1();
    const double rs2 = system.SchwarzschildRadius2();
    const double captureR1 = config.captureMultiplier * rs1;
    const double captureR2 = config.captureMultiplier * rs2;

    glm::dvec3 pos = origin;
    glm::dvec3 vel = glm::normalize(direction);
    glm::dvec3 prevPos = origin;

    result.closestApproach = std::min(glm::length(pos - body1Pos), glm::length(pos - body2Pos));

    for (int i = 0; i < config.maxSteps; ++i) {
        const double distToNearest = std::min(glm::length(pos - body1Pos), glm::length(pos - body2Pos));
        const double h = std::clamp(config.baseStep * distToNearest, config.minStep, config.maxStep);

        const glm::dvec3 k1x = vel;
        const glm::dvec3 k1v = Accel(pos, body1Pos, gm1, body2Pos, gm2);
        const glm::dvec3 k2x = vel + 0.5 * h * k1v;
        const glm::dvec3 k2v = Accel(pos + 0.5 * h * k1x, body1Pos, gm1, body2Pos, gm2);
        const glm::dvec3 k3x = vel + 0.5 * h * k2v;
        const glm::dvec3 k3v = Accel(pos + 0.5 * h * k2x, body1Pos, gm1, body2Pos, gm2);
        const glm::dvec3 k4x = vel + h * k3v;
        const glm::dvec3 k4v = Accel(pos + h * k3x, body1Pos, gm1, body2Pos, gm2);

        pos += (h / 6.0) * (k1x + 2.0 * k2x + 2.0 * k3x + k4x);
        vel += (h / 6.0) * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);

        // The photon moves at constant coordinate speed (c=1) by
        // construction of this approximate scheme -- only its DIRECTION
        // bends under the superposed acceleration above, so renormalize
        // rather than letting the RK4 step's own magnitude drift.
        const double velLen = glm::length(vel);
        if (velLen > 1e-12) {
            vel /= velLen;
        }

        ++result.steps;

        const double dist1 = glm::length(pos - body1Pos);
        const double dist2 = glm::length(pos - body2Pos);
        result.closestApproach = std::min(result.closestApproach, std::min(dist1, dist2));

        if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) {
            result.fate = PhotonFate::UNRESOLVED;
            return result;
        }

        // Disk check takes priority over capture/escape this step, same
        // convention the Schwarzschild/Kerr solvers use (the disk is
        // optically thick, so a photon that reaches it never gets the
        // chance to go on and be captured/escape instead). Body 1's disk
        // is checked first arbitrarily; if the two disks somehow overlap
        // (large disks, small separation) this takes whichever the ray
        // happens to reach in the body-1-first order, not necessarily the
        // nearer one -- an acceptable imprecision for how rare that
        // configuration is, not silently wrong physics.
        if (config.checkDisk) {
            if (CheckDiskCrossing(prevPos, pos, vel, body1Pos, rs1, config.disk1InnerRadius,
                                   config.disk1OuterRadius, 0, result)) {
                return result;
            }
            if (CheckDiskCrossing(prevPos, pos, vel, body2Pos, rs2, config.disk2InnerRadius,
                                   config.disk2OuterRadius, 1, result)) {
                return result;
            }
        }
        prevPos = pos;

        if (dist1 <= captureR1 || dist2 <= captureR2) {
            result.fate = PhotonFate::CAPTURED;
            return result;
        }

        if (glm::length(pos) >= config.escapeRadius) {
            result.fate = PhotonFate::ESCAPED;
            result.escapeDirection = vel;
            return result;
        }
    }

    result.fate = PhotonFate::UNRESOLVED;
    return result;
}

} // namespace Physics
