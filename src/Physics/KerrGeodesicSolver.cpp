#include "KerrGeodesicSolver.h"

#include <algorithm>
#include <cmath>

#include "KerrGeodesicMath.h"
#include "KerrMetric.h"

namespace Physics {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

PhotonTraceResult TracePhotonKerr(const glm::dvec3& origin, const glm::dvec3& direction, double spin,
                                   const KerrGeodesicConfig& config) {
    PhotonTraceResult result;

    // Clamp strictly inside the extremal bound: at a*=1 exactly the inner
    // and outer horizons coincide and several formulas below (Delta=0 at
    // r=M) become degenerate. 0.998 is the standard astrophysical
    // "Thorne limit" quoted for real accreting black holes anyway, so this
    // is a physically-motivated clamp, not just a numerical convenience.
    const double a = std::clamp(spin, -0.998, 0.998);

    const double r0 = glm::length(origin);
    const double outerHorizon = Kerr::OuterHorizonSim(a);
    const double captureRadius = config.captureMultiplier * outerHorizon;

    if (r0 <= outerHorizon) {
        result.fate = PhotonFate::CAPTURED;
        result.closestApproach = r0;
        return result;
    }

    // See KerrGeodesicSolver.h / KerrMetric.h: (r, theta) here are treated
    // as literal spherical coordinates of the camera's Cartesian position.
    // This is the standard simplified spatial embedding used by most
    // real-time Kerr renderers (the true Boyer-Lindquist embedding into
    // flat 3-space is oblate-spheroidal) -- it only affects how a 3D look
    // direction and the final escape direction are read off; every
    // conserved quantity and all of the actual geodesic dynamics below
    // depend only on the (r, theta) VALUES, which are exact.
    const double theta0 = std::acos(std::clamp(origin.z / r0, -1.0, 1.0));
    const double phi0 = std::atan2(origin.y, origin.x);
    const double cosTheta0 = std::cos(theta0);
    const double sinTheta0 = std::max(1e-9, std::sin(theta0));

    const glm::dvec3 rHat = origin / r0;
    const double cosPhi0 = std::cos(phi0);
    const double sinPhi0 = std::sin(phi0);
    const glm::dvec3 thetaHat(cosTheta0 * cosPhi0, cosTheta0 * sinPhi0, -sinTheta0);
    const glm::dvec3 phiHat(-sinPhi0, cosPhi0, 0.0);

    const glm::dvec3 dir = glm::normalize(direction);
    const double nr = glm::dot(dir, rHat);
    const double nth = glm::dot(dir, thetaHat);
    const double nph = glm::dot(dir, phiHat);

    const double sigma0 = Kerr::Sigma(r0, cosTheta0, a);
    const double bigA0 = Kerr::BigA(r0, sinTheta0, a);
    const double alpha0 = Kerr::ZamoLapse(r0, cosTheta0, sinTheta0, a);
    const double omega0 = Kerr::FrameDragOmega(r0, sinTheta0, a);

    // Conserved quantities from the camera's local ZAMO tetrad (see
    // docs/physics.md for the full derivation). E_local=1 is an arbitrary
    // normalization -- only ratios like Lz/E matter for a null geodesic's
    // shape, same role as the Schwarzschild solver tracking b=L/E rather
    // than L, E separately. At a=0 this reduces exactly to that solver's
    // b = r0*nTangential/sqrt(f(r0)) (checked in tests/test_kerr.cpp).
    const double pth0 = std::sqrt(sigma0) * nth;
    const double Lz = sinTheta0 * std::sqrt(bigA0 / sigma0) * nph;
    const double E = alpha0 + omega0 * Lz;
    const double Q =
        pth0 * pth0 + cosTheta0 * cosTheta0 * (Lz * Lz / (sinTheta0 * sinTheta0) - a * a * E * E);

    result.impactParameter = std::abs(Lz / std::max(std::abs(E), 1e-12));
    result.kerrEnergyE = E;
    result.kerrAngularMomentumLz = Lz;
    result.kerrCarterQ = Q;

    double r = r0;
    double theta = theta0;
    double phi = phi0;
    double rSign = (nr >= 0.0) ? 1.0 : -1.0;
    double thSign = (nth >= 0.0) ? 1.0 : -1.0;

    double prevCosTheta = cosTheta0;
    double prevR = r0;
    double prevPhi = phi0;

    if (config.recordPath) {
        result.path.push_back(origin);
    }

    for (int i = 0; i < config.maxSteps; ++i) {
        const double h0 = std::clamp(config.baseLambdaStep * r, config.minLambdaStep, config.maxLambdaStep);

        double rTry = r, thetaTry = theta, phiTry = phi;
        double rSignTry = rSign, thSignTry = thSign, h = h0;

        // A turning point (R(r)=0 or Theta(theta)=0) means dr/dlambda or
        // dtheta/dlambda should hit exactly zero and reverse -- a finite
        // RK4 step can overshoot past the root into the unphysical R<0 /
        // Theta<0 region. Detect that and retry from the SAME starting
        // state with the relevant sign flipped and the step halved
        // (standard turning-point handling for this kind of separated
        // ODE), rather than letting sqrt(max(0,...)) silently clip the
        // overshoot and lose the reflection.
        // Capped at 3 (not more): each attempt costs a full RK4 step (4
        // stages, each with its own sin/cos pair -- unlike Schwarzschild's
        // RK4Step, which is pure polynomial arithmetic), and this loop
        // runs on EVERY integration step for rays that graze close to the
        // photon sphere -- exactly the rays forming the shadow boundary,
        // i.e. a performance-critical ring of pixels, not a rare edge
        // case. 3 halvings (an 8x step-size reduction) resolves a turning
        // point to well below pixel-visible precision; chasing it further
        // was a measured, user-visible frame-time cost for no
        // corresponding visual improvement.
        for (int attempt = 0; attempt < 3; ++attempt) {
            rTry = r;
            thetaTry = theta;
            phiTry = phi;
            KerrGeodesicMath::RK4Step(rTry, thetaTry, phiTry, h, a, E, Lz, Q, rSignTry, thSignTry);

            const double thetaTryClamped = std::clamp(thetaTry, 1e-6, kPi - 1e-6);
            const double Rcheck = KerrGeodesicMath::RadialPotential(rTry, a, E, Lz, Q);
            const double Thcheck = KerrGeodesicMath::PolarPotential(
                std::cos(thetaTryClamped), std::max(1e-9, std::sin(thetaTryClamped)), a, E, Lz, Q);

            const bool overshootR = Rcheck < -1e-6;
            const bool overshootTheta = Thcheck < -1e-6;
            if (!overshootR && !overshootTheta) {
                break;
            }
            if (overshootR) rSignTry = -rSignTry;
            if (overshootTheta) thSignTry = -thSignTry;
            h *= 0.5;
        }

        r = rTry;
        theta = std::clamp(thetaTry, 1e-6, kPi - 1e-6);
        phi = phiTry;
        rSign = rSignTry;
        thSign = thSignTry;
        ++result.steps;

        if (!std::isfinite(r) || !std::isfinite(theta) || !std::isfinite(phi)) {
            result.fate = PhotonFate::UNRESOLVED;
            return result;
        }

        result.closestApproach = std::min(result.closestApproach, r);

        const double sinTheta = std::sin(theta);
        const double cosTheta = std::cos(theta);
        const glm::dvec3 pos(r * sinTheta * std::cos(phi), r * sinTheta * std::sin(phi), r * cosTheta);

        if (config.recordPath && result.path.size() < static_cast<size_t>(config.maxPathPoints)) {
            result.path.push_back(pos);
        }

        // Equatorial-plane disk crossing, detected the same way as the
        // Schwarzschild solver (a sign change, here in cos(theta) rather
        // than Cartesian z, since r>0 always makes them equivalent).
        if (config.checkDisk && (prevCosTheta > 0.0) != (cosTheta > 0.0)) {
            const double t = prevCosTheta / (prevCosTheta - cosTheta);
            const double hitR = prevR + t * (r - prevR);

            if (hitR >= config.diskInnerRadius && hitR <= config.diskOuterRadius && hitR > outerHorizon) {
                const double hitPhi = prevPhi + t * (phi - prevPhi);

                // Cunningham (1975) / Luminet (1979) closed-form redshift
                // for a photon emitted by matter on a prograde circular
                // equatorial Kerr geodesic orbit and received by a distant
                // observer: g = sqrt(1 - 3/r + 2*a*Omega) / (1 + Omega*lambda),
                // Omega = 1/(r^1.5+a) the orbital angular velocity, lambda =
                // Lz/E the photon's own conserved specific angular momentum.
                // This is the standard result used throughout the Kerr
                // accretion-disk-imaging literature; reduces to the
                // Schwarzschild solver's gravitational*Doppler product at
                // a=0 (both describe the same physics via different,
                // independently-standard derivations).
                const double lambdaRatio = Lz / E;
                const double omega = 1.0 / (std::pow(hitR, 1.5) + a);
                const double gSq = 1.0 - 3.0 / hitR + 2.0 * a * omega;
                const double denom = 1.0 + omega * lambdaRatio;
                const double g = std::sqrt(std::max(0.0, gSq)) / std::max(1e-6, std::abs(denom));

                result.hitDisk = true;
                result.diskHitRadius = hitR;
                result.diskHitPosition =
                    glm::dvec3(hitR * std::cos(hitPhi), hitR * std::sin(hitPhi), 0.0);
                result.diskRedshiftFactor = g;
                // abs(hitPhi - phi0) - pi, not hitPhi alone: unlike the
                // Schwarzschild solver's local tangent-plane phi (which is
                // 0 AT the launch point by construction), this solver's
                // phi is the absolute world-frame azimuth (atan2 of
                // position), so it starts at whatever phi0 happens to be
                // for the launch position -- subtracting it off is what
                // makes "swept angle minus pi" meaningful as an excess-
                // over-a-straight-line quantity. The abs() matters too: a
                // retrograde ray's Lz has the opposite sign, so ITS phi
                // decreases over time rather than increasing -- without
                // abs() that case sweeps roughly -pi instead of +pi, giving
                // a wildly wrong (~-2pi) raw value. Caught by
                // tests/test_kerr.cpp's weak-field deflection check, which
                // got nonsense values (~pi for all rays, then a correct
                // prograde but ~2pi-magnitude retrograde) before these two
                // fixes, in that order.
                result.totalDeflectionAngle = std::abs(hitPhi - phi0) - kPi;
                return result;
            }
        }

        prevCosTheta = cosTheta;
        prevR = r;
        prevPhi = phi;

        if (r <= captureRadius) {
            result.fate = PhotonFate::CAPTURED;
            // (phi - phi0) - pi, same convention/caveat as the disk-hit
            // branch above: the azimuthal angle swept SINCE LAUNCH (not
            // phi's absolute world-frame value, which starts at phi0, not
            // 0 -- see that branch's comment) minus the pi an undeflected
            // straight-line path would sweep. Exact only for the
            // equatorial-plane special case (theta held at pi/2
            // throughout); for a general off-equatorial Kerr ray this is
            // a diagnostic "net azimuthal winding," not a rigorously
            // defined 3D deflection angle.
            result.totalDeflectionAngle = std::abs(phi - phi0) - kPi; // see disk-hit branch above for why abs()
            return result;
        }
        if (r >= config.escapeRadius) {
            result.fate = PhotonFate::ESCAPED;
            const double st = std::sin(theta);
            result.escapeDirection = glm::dvec3(st * std::cos(phi), st * std::sin(phi), std::cos(theta));
            result.totalDeflectionAngle = std::abs(phi - phi0) - kPi; // see disk-hit branch above for why abs()
            return result;
        }
    }

    result.fate = PhotonFate::UNRESOLVED;
    return result;
}

} // namespace Physics
