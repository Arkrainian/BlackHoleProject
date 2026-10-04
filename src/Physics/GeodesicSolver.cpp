#include "GeodesicSolver.h"

#include <algorithm>
#include <cmath>

#include "GeodesicMath.h"
#include "SchwarzschildMetric.h"

namespace Physics {

namespace {

using GeodesicMath::Accel;
using GeodesicMath::RK4Step;

double AdaptivePhiStep(double u, double rs, const GeodesicConfig& config) {
    return GeodesicMath::AdaptivePhiStep(u, rs, config.basePhiStep, config.minPhiStep, config.maxPhiStep,
                                          config.adaptiveStep);
}

} // namespace

PhotonTraceResult TracePhoton(const glm::dvec3& origin, const glm::dvec3& direction, const GeodesicConfig& config) {
    PhotonTraceResult result;

    const double rs = config.schwarzschildRadius;
    const double r0 = glm::length(origin);
    const double captureRadius = config.captureMultiplier * rs;

    if (r0 <= rs) {
        // Camera/origin already inside the horizon -- not a physically
        // meaningful ray to trace; report it captured immediately.
        result.fate = PhotonFate::CAPTURED;
        result.closestApproach = r0;
        return result;
    }

    const glm::dvec3 dir = glm::normalize(direction);
    const glm::dvec3 rHat = origin / r0;

    const glm::dvec3 planeNormalRaw = glm::cross(origin, dir);
    const double planeNormalLen = glm::length(planeNormalRaw);

    const double lapse0 = Schwarzschild::LapseFactor(r0, rs); // f(r0) = 1 - r_s/r0

    // Near-radial ray (impact parameter ~0): the u(phi) formulation is
    // singular here (b -> 0 makes 1/b^2 diverge) because phi is simply
    // undefined for a purely radial trajectory. Physically this case is
    // trivial anyway: a radial null geodesic moves at constant d(r)/d(affine
    // parameter), so we step r directly instead of going through phi.
    if (planeNormalLen < 1.0e-9) {
        const double nRadial = glm::dot(dir, rHat); // +1 outward, -1 inward
        result.impactParameter = 0.0;

        double r = r0;
        const double step = (config.escapeRadius - rs) / config.maxSteps > 1e-4
                                 ? (config.escapeRadius - rs) / config.maxSteps
                                 : 1e-4;

        if (config.recordPath) result.path.push_back(origin);

        for (int i = 0; i < config.maxSteps; ++i) {
            r += nRadial * std::max(step, 1e-6);
            ++result.steps;
            result.closestApproach = std::min(result.closestApproach, r);

            if (config.recordPath && result.path.size() < static_cast<size_t>(config.maxPathPoints)) {
                result.path.push_back(r * rHat);
            }
            if (r <= captureRadius) {
                result.fate = PhotonFate::CAPTURED;
                return result;
            }
            if (r >= config.escapeRadius) {
                result.fate = PhotonFate::ESCAPED;
                result.totalDeflectionAngle = 0.0; // radial rays are undeflected by symmetry
                result.escapeDirection = nRadial >= 0.0 ? rHat : -rHat;
                return result;
            }
        }
        result.fate = PhotonFate::UNRESOLVED;
        return result;
    }

    const glm::dvec3 planeNormal = planeNormalRaw / planeNormalLen;
    const glm::dvec3 tangentialDir = glm::cross(planeNormal, rHat); // unit, orthogonal to rHat, in-plane

    const double nRadial = glm::dot(dir, rHat);         // n_r-hat, local orthonormal radial component
    const double nTangential = glm::dot(dir, tangentialDir); // n_phi-hat, local orthonormal tangential component

    // Exact impact parameter from the static observer's orthonormal tetrad
    // at r0 (see GeodesicSolver.h derivation): b = r0 * n_phi-hat / sqrt(f(r0)).
    const double b = r0 * nTangential / std::sqrt(lapse0);
    result.impactParameter = std::abs(b);

    double u = 1.0 / r0;
    double v = -nRadial / b; // du/dphi at phi=0, derived alongside b above
    double phi = 0.0;

    glm::dvec3 prevPos = origin;
    double prevPhi = 0.0;

    if (config.recordPath) result.path.push_back(origin);

    for (int i = 0; i < config.maxSteps; ++i) {
        const double h = AdaptivePhiStep(u, rs, config);

        RK4Step(u, v, h, rs);
        phi += h;
        ++result.steps;

        if (!std::isfinite(u) || !std::isfinite(v)) {
            // Genuine numerical breakdown (overflow/NaN), not a step that
            // merely overshot past u=0 -- that case is handled below.
            result.fate = PhotonFate::UNRESOLVED;
            result.totalDeflectionAngle = phi - 3.14159265358979323846;
            return result;
        }

        if (u <= 1.0 / config.escapeRadius) {
            // u <= 0 unambiguously means r has passed through +infinity
            // within this step (a fixed-size phi step can easily overshoot
            // the escape threshold at large r0, since du/dphi stays ~O(1/b)
            // even as u -> 0 -- there is no reason for the step to land
            // exactly on u=0). Either way the photon has escaped.
            result.fate = PhotonFate::ESCAPED;
            result.totalDeflectionAngle = phi - 3.14159265358979323846;
            result.closestApproach = std::min(result.closestApproach, r0);
            // The step overshot past u=0, so the position reconstruction
            // formula (valid for r=1/u > 0) can't be evaluated at this
            // exact phi -- but the DIRECTION of travel is still meaningful
            // and that's all a skybox lookup needs, so use the direction
            // implied by phi alone (unit radial position at this angle).
            result.escapeDirection = std::cos(phi) * rHat + std::sin(phi) * tangentialDir;
            return result;
        }

        const double r = 1.0 / u;
        result.closestApproach = std::min(result.closestApproach, r);

        const glm::dvec3 pos = r * (std::cos(phi) * rHat + std::sin(phi) * tangentialDir);

        if (config.recordPath && result.path.size() < static_cast<size_t>(config.maxPathPoints)) {
            result.path.push_back(pos);
        }

        // Stage 6: disk is modeled as opaque, so the FIRST crossing of the
        // equatorial plane within its radial extent ends the trace.
        if (config.checkDisk && prevPos.z != pos.z && (prevPos.z > 0.0) != (pos.z > 0.0)) {
            const double t = prevPos.z / (prevPos.z - pos.z); // interpolation fraction in [0,1]
            const glm::dvec3 hitPos = prevPos + t * (pos - prevPos);
            const double hitR = glm::length(hitPos);

            if (hitR >= config.diskInnerRadius && hitR <= config.diskOuterRadius) {
                const double hitPhi = prevPhi + t * (phi - prevPhi);

                // Local photon propagation direction at the crossing point,
                // from the SAME conserved b (see GeodesicSolver.h).
                const double nPhiHatAtHit = b * std::sqrt(Schwarzschild::LapseFactor(hitR, rs)) / hitR;
                const double nRadialAtHit = (v >= 0.0 ? -1.0 : 1.0) *
                                             std::sqrt(std::max(0.0, 1.0 - nPhiHatAtHit * nPhiHatAtHit));
                const glm::dvec3 radialDirAtHit = std::cos(hitPhi) * rHat + std::sin(hitPhi) * tangentialDir;
                const glm::dvec3 tangentialDirAtHit = -std::sin(hitPhi) * rHat + std::cos(hitPhi) * tangentialDir;
                const glm::dvec3 photonDir = nRadialAtHit * radialDirAtHit + nPhiHatAtHit * tangentialDirAtHit;

                // Disk material's local velocity direction (purely
                // azimuthal in the global equatorial plane) and speed
                // v = 1/sqrt(r - r_s) -- see GeodesicSolver.h.
                glm::dvec3 diskVelocityDir = glm::normalize(glm::cross(glm::dvec3(0.0, 0.0, 1.0), hitPos));
                if (!config.diskClockwise) diskVelocityDir = -diskVelocityDir;
                const double vLocal = 1.0 / std::sqrt(std::max(1e-6, hitR - rs));
                const double gammaLocal = 1.0 / std::sqrt(std::max(1e-9, 1.0 - vLocal * vLocal));

                const double cosPsi = glm::dot(photonDir, diskVelocityDir);
                const double dopplerFactor = 1.0 / (gammaLocal * (1.0 - vLocal * cosPsi));
                const double gravitationalFactor = std::sqrt(Schwarzschild::LapseFactor(hitR, rs));

                result.hitDisk = true;
                result.diskHitPosition = hitPos;
                result.diskHitRadius = hitR;
                result.diskRedshiftFactor = gravitationalFactor * dopplerFactor;
                result.totalDeflectionAngle = hitPhi - 3.14159265358979323846;
                return result;
            }
        }
        prevPos = pos;
        prevPhi = phi;

        if (r <= captureRadius) {
            result.fate = PhotonFate::CAPTURED;
            result.totalDeflectionAngle = phi - 3.14159265358979323846;
            return result;
        }
        // (the u <= 1/escapeRadius check above already covers the escape case)
    }

    result.fate = PhotonFate::UNRESOLVED;
    result.totalDeflectionAngle = phi - 3.14159265358979323846;
    return result;
}

} // namespace Physics
