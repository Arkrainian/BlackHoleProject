#include "MultiBodyGeodesicSolver.h"

#include <algorithm>
#include <cmath>

namespace Physics {

namespace {

// Superposed "doubled-Newtonian" acceleration -- see MultiBodyGeodesicSolver.h
// / BinaryGeodesicSolver.h for why this specific factor of 2 is the right
// one, not an arbitrary tuning constant.
glm::dvec3 Accel(const glm::dvec3& pos, const LensingBody* bodies, int bodyCount) {
    glm::dvec3 a(0.0);
    for (int i = 0; i < bodyCount; ++i) {
        const glm::dvec3 d = pos - bodies[i].position;
        const double r = glm::length(d);
        if (r > 1e-6) {
            a += -2.0 * bodies[i].gm * d / (r * r * r);
        }
    }
    return a;
}

double DistanceToNearest(const glm::dvec3& pos, const LensingBody* bodies, int bodyCount) {
    double best = 1e300;
    for (int i = 0; i < bodyCount; ++i) {
        best = std::min(best, glm::length(pos - bodies[i].position));
    }
    return best;
}

// Checks whether the segment prevPos->pos crosses z=0 within `body`'s own
// disk annulus; if so, fills in the disk-hit fields of `result` and
// returns true. bodyIndex is written into result.diskBodyIndex so the
// caller can pick the right Astronomy::AccretionDisk instance (different
// mass -> different real Kelvin scale) when coloring. Mirrors
// BinaryGeodesicSolver.cpp's CheckDiskCrossing exactly, generalized from
// one named body to any LensingBody.
bool CheckDiskCrossing(const glm::dvec3& prevPos, const glm::dvec3& pos, const glm::dvec3& vel,
                        const LensingBody& body, double innerRadius, double outerRadius, int bodyIndex,
                        PhotonTraceResult& result) {
    if ((prevPos.z > 0.0) == (pos.z > 0.0)) {
        return false; // no z=0 crossing this step
    }
    const double t = prevPos.z / (prevPos.z - pos.z);
    const glm::dvec3 hitPos = prevPos + t * (pos - prevPos);
    const double hitR = glm::length(hitPos - body.position);

    if (hitR < innerRadius || hitR > outerRadius) {
        return false;
    }

    const double vLocal = 1.0 / std::sqrt(std::max(1e-6, hitR - body.rs));
    const double gammaLocal = 1.0 / std::sqrt(std::max(1e-9, 1.0 - vLocal * vLocal));
    const glm::dvec3 diskVelocityDir = glm::normalize(glm::cross(glm::dvec3(0.0, 0.0, 1.0), hitPos - body.position));
    const double cosPsi = glm::dot(vel, diskVelocityDir);
    const double dopplerFactor = 1.0 / (gammaLocal * (1.0 - vLocal * cosPsi));
    const double gravitationalFactor = std::sqrt(std::max(0.0, 1.0 - body.rs / hitR));

    result.hitDisk = true;
    result.diskHitPosition = hitPos;
    result.diskHitRadius = hitR;
    result.diskRedshiftFactor = gravitationalFactor * dopplerFactor;
    result.diskBodyIndex = bodyIndex;
    return true;
}

} // namespace

PhotonTraceResult TracePhotonMultiBody(const glm::dvec3& origin, const glm::dvec3& direction,
                                        const LensingBody* bodies, int bodyCount,
                                        const MultiBodyGeodesicConfig& config) {
    PhotonTraceResult result;
    bodyCount = std::min(bodyCount, kMaxLensingBodies);
    if (bodyCount <= 0) {
        result.fate = PhotonFate::ESCAPED;
        result.escapeDirection = glm::normalize(direction);
        return result;
    }

    glm::dvec3 pos = origin;
    glm::dvec3 vel = glm::normalize(direction);
    glm::dvec3 prevPos = origin;

    result.closestApproach = DistanceToNearest(pos, bodies, bodyCount);

    for (int i = 0; i < config.maxSteps; ++i) {
        const double distToNearest = DistanceToNearest(pos, bodies, bodyCount);
        const double h = std::clamp(config.baseStep * distToNearest, config.minStep, config.maxStep);

        const glm::dvec3 k1x = vel;
        const glm::dvec3 k1v = Accel(pos, bodies, bodyCount);
        const glm::dvec3 k2x = vel + 0.5 * h * k1v;
        const glm::dvec3 k2v = Accel(pos + 0.5 * h * k1x, bodies, bodyCount);
        const glm::dvec3 k3x = vel + 0.5 * h * k2v;
        const glm::dvec3 k3v = Accel(pos + 0.5 * h * k2x, bodies, bodyCount);
        const glm::dvec3 k4x = vel + h * k3v;
        const glm::dvec3 k4v = Accel(pos + h * k3x, bodies, bodyCount);

        pos += (h / 6.0) * (k1x + 2.0 * k2x + 2.0 * k3x + k4x);
        vel += (h / 6.0) * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);

        // Constant coordinate speed by construction -- see BinaryGeodesicSolver.cpp's
        // matching comment.
        const double velLen = glm::length(vel);
        if (velLen > 1e-12) {
            vel /= velLen;
        }

        ++result.steps;

        double nearestDist = 1e300;
        bool anyCaptured = false;
        for (int b = 0; b < bodyCount; ++b) {
            const double d = glm::length(pos - bodies[b].position);
            nearestDist = std::min(nearestDist, d);
            if (d <= bodies[b].captureRadius) {
                anyCaptured = true;
            }
        }
        result.closestApproach = std::min(result.closestApproach, nearestDist);

        if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) {
            result.fate = PhotonFate::UNRESOLVED;
            return result;
        }

        // Disk check takes priority over capture/escape, same convention
        // every other solver in this project uses. Bodies are checked in
        // array order; if two disks somehow overlap, this takes whichever
        // the ray reaches in that order, not necessarily the nearer one --
        // same acceptable imprecision BinaryGeodesicSolver.cpp's own
        // comment already documents for the two-body case.
        if (config.checkDisk) {
            bool hitAnyDisk = false;
            for (int b = 0; b < bodyCount; ++b) {
                if (CheckDiskCrossing(prevPos, pos, vel, bodies[b], config.diskInnerRadius[static_cast<size_t>(b)],
                                       config.diskOuterRadius[static_cast<size_t>(b)], b, result)) {
                    hitAnyDisk = true;
                    break;
                }
            }
            if (hitAnyDisk) {
                return result;
            }
        }
        prevPos = pos;

        if (anyCaptured) {
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
