// GPU photon geodesic tracer: one CUDA thread per pixel/ray, mirroring
// Physics::TracePhoton's non-radial branch (see GeodesicSolver.cpp) using
// the SAME shared ODE primitives from Physics/GeodesicMath.h -- the actual
// numerical integration is not reimplemented, only re-orchestrated into a
// flat per-thread kernel (CUDA device code can't use std::vector/
// std::function/exceptions the way the CPU version does).
//
// The near-radial special case (impact parameter ~0, camera aimed dead-on
// at the black hole to floating-point precision) IS special-cased here,
// mirroring the CPU tracer's trivial linear-r stepping -- see the
// planeNormalLen branch in TracePhotonDevice below and GeodesicSolver.cpp's
// matching branch. Without it, the main b-parametrized path's b -> 0
// produces a large-but-finite v0 that either integrates implausibly or
// fails the isfinite() guard, misclassifying the handful of pixels
// directly along the camera's exact forward axis as Unresolved.

#include "GeodesicKernel.h"

#include <cstdio>
#include <cstring>
#include <cuda_runtime.h>

#include "Physics/GeodesicMath.h"
#include "Physics/KerrGeodesicMath.h"
#include "Physics/KerrMetric.h"
#include "Physics/SchwarzschildMetric.h"

using namespace CudaTracing;

namespace {

__host__ __device__ inline KernelVec3 operator+(const KernelVec3& a, const KernelVec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
__host__ __device__ inline KernelVec3 operator-(const KernelVec3& a, const KernelVec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
__host__ __device__ inline KernelVec3 operator-(const KernelVec3& a) { return {-a.x, -a.y, -a.z}; }
__host__ __device__ inline KernelVec3 operator*(float s, const KernelVec3& a) { return {s * a.x, s * a.y, s * a.z}; }
__host__ __device__ inline float Dot(const KernelVec3& a, const KernelVec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
__host__ __device__ inline KernelVec3 Cross(const KernelVec3& a, const KernelVec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
__host__ __device__ inline float Length(const KernelVec3& a) { return sqrtf(Dot(a, a)); }
__host__ __device__ inline KernelVec3 Normalize(const KernelVec3& a) {
    const float len = Length(a);
    return (1.0f / len) * a;
}
template <typename T>
__host__ __device__ inline T DMax(T a, T b) { return a > b ? a : b; }
template <typename T>
__host__ __device__ inline T DMin(T a, T b) { return a < b ? a : b; }

// All arithmetic below is float, not double -- see the comment on
// KernelVec3 in GeodesicKernel.h for why. GeodesicMath::Accel/RK4Step/
// AdaptivePhiStep and Schwarzschild::LapseFactor are templated on T and
// deduce T=float here purely from these calls' argument types, so this is
// still the exact same shared ODE code the CPU path uses at T=double --
// not a reimplementation.
__device__ KernelPixelResult TracePhotonDevice(const KernelVec3& origin, const KernelVec3& directionIn,
                                                 const KernelGeodesicConfig& config) {
    KernelPixelResult result;

    const float rs = config.schwarzschildRadius;
    const float r0 = Length(origin);
    const float captureRadius = config.captureMultiplier * rs;

    if (r0 <= rs) {
        result.fate = KernelFate::Captured;
        return result;
    }

    const KernelVec3 dir = Normalize(directionIn);
    const KernelVec3 rHat = (1.0f / r0) * origin;

    const KernelVec3 planeNormalRaw = Cross(origin, dir);
    const float planeNormalLen = Length(planeNormalRaw);
    const float lapse0 = Physics::Schwarzschild::LapseFactor(r0, rs);

    if (planeNormalLen < 1.0e-6f) {
        // Near-radial ray (impact parameter ~0): mirrors the CPU tracer's
        // trivial linear-r stepping (see GeodesicSolver.cpp) rather than
        // falling through to the b-parametrized path below, where b -> 0
        // would blow up v = -nRadial/b. Disk crossing is intentionally not
        // checked here, matching the CPU path's behavior for this case.
        const float nRadial = Dot(dir, rHat); // +1 outward, -1 inward
        float r = r0;
        const float rawStep = (config.escapeRadius - rs) / config.maxSteps;
        const float step = rawStep > 1.0e-4f ? rawStep : 1.0e-4f;

        for (int i = 0; i < config.maxSteps; ++i) {
            r += nRadial * fmaxf(step, 1.0e-6f);

            if (r <= captureRadius) {
                result.fate = KernelFate::Captured;
                return result;
            }
            if (r >= config.escapeRadius) {
                result.fate = KernelFate::Escaped;
                result.escapeDirection = nRadial >= 0.0f ? rHat : -rHat;
                return result;
            }
        }
        result.fate = KernelFate::Unresolved;
        return result;
    }

    const KernelVec3 planeNormal = (1.0f / planeNormalLen) * planeNormalRaw;
    const KernelVec3 tangentialDir = Cross(planeNormal, rHat);

    const float nRadial = Dot(dir, rHat);
    const float nTangential = Dot(dir, tangentialDir);

    const float b = r0 * nTangential / sqrtf(lapse0);

    float u = 1.0f / r0;
    float v = -nRadial / b;
    float phi = 0.0f;

    KernelVec3 prevPos = origin;
    float prevPhi = 0.0f;

    for (int i = 0; i < config.maxSteps; ++i) {
        const float h = Physics::GeodesicMath::AdaptivePhiStep(u, rs, config.basePhiStep, config.minPhiStep,
                                                                 config.maxPhiStep, config.adaptiveStep);
        Physics::GeodesicMath::RK4Step(u, v, h, rs);
        phi += h;

        if (!isfinite(u) || !isfinite(v)) {
            result.fate = KernelFate::Unresolved;
            return result;
        }

        if (u <= 1.0f / config.escapeRadius) {
            result.fate = KernelFate::Escaped;
            result.escapeDirection = cosf(phi) * rHat + sinf(phi) * tangentialDir;
            return result;
        }

        const float r = 1.0f / u;
        const KernelVec3 pos = r * (cosf(phi) * rHat + sinf(phi) * tangentialDir);

        if (config.checkDisk && prevPos.z != pos.z && (prevPos.z > 0.0f) != (pos.z > 0.0f)) {
            const float t = prevPos.z / (prevPos.z - pos.z);
            const KernelVec3 hitPos = prevPos + t * (pos - prevPos);
            const float hitR = Length(hitPos);

            if (hitR >= config.diskInnerRadius && hitR <= config.diskOuterRadius) {
                const float hitPhi = prevPhi + t * (phi - prevPhi);

                const float nPhiHatAtHit = b * sqrtf(Physics::Schwarzschild::LapseFactor(hitR, rs)) / hitR;
                const float nRadialAtHit =
                    (v >= 0.0f ? -1.0f : 1.0f) * sqrtf(DMax(0.0f, 1.0f - nPhiHatAtHit * nPhiHatAtHit));
                const KernelVec3 radialDirAtHit = cosf(hitPhi) * rHat + sinf(hitPhi) * tangentialDir;
                const KernelVec3 tangentialDirAtHit = (-sinf(hitPhi)) * rHat + cosf(hitPhi) * tangentialDir;
                const KernelVec3 photonDir = nRadialAtHit * radialDirAtHit + nPhiHatAtHit * tangentialDirAtHit;

                KernelVec3 diskVelocityDir = Normalize(Cross(KernelVec3{0.0f, 0.0f, 1.0f}, hitPos));
                if (!config.diskClockwise) diskVelocityDir = -diskVelocityDir;
                const float vLocal = 1.0f / sqrtf(DMax(1e-6f, hitR - rs));
                const float gammaLocal = 1.0f / sqrtf(DMax(1e-9f, 1.0f - vLocal * vLocal));

                const float cosPsi = Dot(photonDir, diskVelocityDir);
                const float dopplerFactor = 1.0f / (gammaLocal * (1.0f - vLocal * cosPsi));
                const float gravitationalFactor = sqrtf(Physics::Schwarzschild::LapseFactor(hitR, rs));

                result.hitDisk = true;
                result.diskHitRadius = hitR;
                result.diskRedshiftFactor = gravitationalFactor * dopplerFactor;
                return result;
            }
        }
        prevPos = pos;
        prevPhi = phi;

        if (r <= captureRadius) {
            result.fate = KernelFate::Captured;
            return result;
        }
    }

    result.fate = KernelFate::Unresolved;
    return result;
}

__global__ void TracePhotonsKernel(KernelCameraFrame camera, KernelGeodesicConfig config, int width, int height,
                                    KernelPixelResult* output) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const float ndcX = ((x + 0.5f) / width * 2.0f - 1.0f) * camera.aspect * camera.tanHalfFovY;
    const float ndcY = (1.0f - (y + 0.5f) / height * 2.0f) * camera.tanHalfFovY;

    const KernelVec3 direction = Normalize(camera.forward + ndcX * camera.right + ndcY * camera.up);
    output[static_cast<size_t>(y) * width + x] = TracePhotonDevice(camera.position, direction, config);
}

// Kerr counterpart of TracePhotonDevice -- mirrors Physics::TracePhotonKerr
// (src/Physics/KerrGeodesicSolver.cpp) step for step, in float rather than
// double (see KernelVec3's comment for why), calling the SAME shared
// Physics::Kerr / Physics::KerrGeodesicMath templated functions the CPU
// solver uses (deduced at T=float here), not a reimplementation of the
// physics. See docs/physics.md Sec. 10 for the full derivation this
// follows (Carter's separated Hamilton-Jacobi equations, the ZAMO-tetrad
// initial conditions, the turning-point retry loop, and the
// Cunningham/Luminet disk redshift formula).
__device__ KernelPixelResult TracePhotonKerrDevice(const KernelVec3& origin, const KernelVec3& directionIn,
                                                     float spin, const KernelKerrConfig& config) {
    KernelPixelResult result;

    const float a = spin;
    const float r0 = Length(origin);
    const float outerHorizon = 1.0f + sqrtf(fmaxf(0.0f, 1.0f - a * a));
    const float captureRadius = config.captureMultiplier * outerHorizon;

    if (r0 <= outerHorizon) {
        result.fate = KernelFate::Captured;
        return result;
    }

    const float theta0 = acosf(fminf(1.0f, fmaxf(-1.0f, origin.z / r0)));
    const float phi0 = atan2f(origin.y, origin.x);
    const float cosTheta0 = cosf(theta0);
    const float sinTheta0 = fmaxf(1.0e-6f, sinf(theta0));

    const KernelVec3 rHat = (1.0f / r0) * origin;
    const float cosPhi0 = cosf(phi0);
    const float sinPhi0 = sinf(phi0);
    const KernelVec3 thetaHat{cosTheta0 * cosPhi0, cosTheta0 * sinPhi0, -sinTheta0};
    const KernelVec3 phiHat{-sinPhi0, cosPhi0, 0.0f};

    const KernelVec3 dir = Normalize(directionIn);
    const float nr = Dot(dir, rHat);
    const float nth = Dot(dir, thetaHat);
    const float nph = Dot(dir, phiHat);

    const float sigma0 = Physics::Kerr::Sigma(r0, cosTheta0, a);
    const float bigA0 = Physics::Kerr::BigA(r0, sinTheta0, a);
    const float alpha0 = Physics::Kerr::ZamoLapse(r0, cosTheta0, sinTheta0, a);
    const float omega0 = Physics::Kerr::FrameDragOmega(r0, sinTheta0, a);

    const float pth0 = sqrtf(sigma0) * nth;
    const float Lz = sinTheta0 * sqrtf(bigA0 / sigma0) * nph;
    const float E = alpha0 + omega0 * Lz;
    const float Q = pth0 * pth0 + cosTheta0 * cosTheta0 * (Lz * Lz / (sinTheta0 * sinTheta0) - a * a * E * E);

    float r = r0, theta = theta0, phi = phi0;
    float rSign = (nr >= 0.0f) ? 1.0f : -1.0f;
    float thSign = (nth >= 0.0f) ? 1.0f : -1.0f;

    float prevCosTheta = cosTheta0;
    float prevR = r0;
    // Unlike the CPU solver, this kernel's KernelPixelResult has no
    // diskHitPosition field (matching the existing Schwarzschild device
    // kernel, which also omits it) and the Cunningham/Luminet redshift
    // below only needs hitR -- so there's no need to track prevPhi here.

    constexpr float kPi = 3.14159265358979323846f;

    for (int i = 0; i < config.maxSteps; ++i) {
        const float h0 = fminf(config.maxLambdaStep, fmaxf(config.minLambdaStep, config.baseLambdaStep * r));

        float rTry = r, thetaTry = theta, phiTry = phi;
        float rSignTry = rSign, thSignTry = thSign, h = h0;

        // Turning-point handling: same retry-with-flipped-sign-and-halved-
        // step scheme as the CPU solver (see KerrGeodesicSolver.cpp) --
        // float tolerance (1e-4) is looser than the CPU solver's double
        // tolerance (1e-6) since float RK4 steps carry more roundoff.
        // Capped at 3, same reasoning as the CPU solver: this loop is on
        // the hot path for every ray near the photon sphere, and on GPU
        // it ALSO costs warp divergence (threads in a warp that hit
        // different retry counts can't retire together), so keeping it
        // short matters even more here than on CPU.
        for (int attempt = 0; attempt < 3; ++attempt) {
            rTry = r;
            thetaTry = theta;
            phiTry = phi;
            Physics::KerrGeodesicMath::RK4Step(rTry, thetaTry, phiTry, h, a, E, Lz, Q, rSignTry, thSignTry);

            const float thetaClamped = fminf(kPi - 1e-6f, fmaxf(1e-6f, thetaTry));
            const float Rcheck = Physics::KerrGeodesicMath::RadialPotential(rTry, a, E, Lz, Q);
            const float Thcheck = Physics::KerrGeodesicMath::PolarPotential(
                cosf(thetaClamped), fmaxf(1e-6f, sinf(thetaClamped)), a, E, Lz, Q);

            const bool overshootR = Rcheck < -1e-4f;
            const bool overshootTheta = Thcheck < -1e-4f;
            if (!overshootR && !overshootTheta) break;
            if (overshootR) rSignTry = -rSignTry;
            if (overshootTheta) thSignTry = -thSignTry;
            h *= 0.5f;
        }

        r = rTry;
        theta = fminf(kPi - 1e-6f, fmaxf(1e-6f, thetaTry));
        phi = phiTry;
        rSign = rSignTry;
        thSign = thSignTry;

        if (!isfinite(r) || !isfinite(theta) || !isfinite(phi)) {
            result.fate = KernelFate::Unresolved;
            return result;
        }

        const float sinTheta = sinf(theta);
        const float cosTheta = cosf(theta);
        const KernelVec3 pos{r * sinTheta * cosf(phi), r * sinTheta * sinf(phi), r * cosTheta};

        if (config.checkDisk && (prevCosTheta > 0.0f) != (cosTheta > 0.0f)) {
            const float t = prevCosTheta / (prevCosTheta - cosTheta);
            const float hitR = prevR + t * (r - prevR);

            if (hitR >= config.diskInnerRadius && hitR <= config.diskOuterRadius && hitR > outerHorizon) {
                // Cunningham (1975) / Luminet (1979) prograde-disk redshift
                // -- see docs/physics.md Sec. 10 and KerrGeodesicSolver.cpp
                // for the derivation; identical formula, just in float.
                const float lambdaRatio = Lz / E;
                const float omega = 1.0f / (powf(hitR, 1.5f) + a);
                const float gSq = 1.0f - 3.0f / hitR + 2.0f * a * omega;
                const float denom = 1.0f + omega * lambdaRatio;
                const float g = sqrtf(fmaxf(0.0f, gSq)) / fmaxf(1e-6f, fabsf(denom));

                result.hitDisk = true;
                result.diskHitRadius = hitR;
                result.diskRedshiftFactor = g;
                return result;
            }
        }

        prevCosTheta = cosTheta;
        prevR = r;

        if (r <= captureRadius) {
            result.fate = KernelFate::Captured;
            return result;
        }
        if (r >= config.escapeRadius) {
            result.fate = KernelFate::Escaped;
            const float st = sinf(theta);
            result.escapeDirection = KernelVec3{st * cosf(phi), st * sinf(phi), cosf(theta)};
            return result;
        }
    }

    result.fate = KernelFate::Unresolved;
    return result;
}

__global__ void TraceKerrPhotonsKernel(KernelCameraFrame camera, KernelKerrConfig config, float spin, int width,
                                        int height, KernelPixelResult* output) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const float ndcX = ((x + 0.5f) / width * 2.0f - 1.0f) * camera.aspect * camera.tanHalfFovY;
    const float ndcY = (1.0f - (y + 0.5f) / height * 2.0f) * camera.tanHalfFovY;

    const KernelVec3 direction = Normalize(camera.forward + ndcX * camera.right + ndcY * camera.up);
    output[static_cast<size_t>(y) * width + x] = TracePhotonKerrDevice(camera.position, direction, spin, config);
}

// Binary black holes (deliberately approximate superposed "doubled
// Newtonian" light bending) -- mirrors Physics::TracePhotonBinary
// (BinaryGeodesicSolver.cpp) step for step. See
// Physics/BinaryGeodesicSolver.h for why this scheme is approximate (no
// exact two-body GR metric exists at all) rather than a simplification of
// some more exact method this project chose not to implement.
__device__ KernelVec3 BinaryAccel(const KernelVec3& pos, const KernelVec3& body1Pos, float gm1,
                                   const KernelVec3& body2Pos, float gm2) {
    KernelVec3 a{0.0f, 0.0f, 0.0f};
    const KernelVec3 d1 = pos - body1Pos;
    const float r1 = Length(d1);
    if (r1 > 1.0e-6f) {
        a = a + (-2.0f * gm1 / (r1 * r1 * r1)) * d1;
    }
    const KernelVec3 d2 = pos - body2Pos;
    const float r2 = Length(d2);
    if (r2 > 1.0e-6f) {
        a = a + (-2.0f * gm2 / (r2 * r2 * r2)) * d2;
    }
    return a;
}

// Mirrors the CPU solver's CheckDiskCrossing (BinaryGeodesicSolver.cpp):
// checks the segment prevPos->pos for a z=0 crossing within this body's
// disk annulus, and if so fills in result's disk-hit fields.
__device__ bool BinaryCheckDiskCrossing(const KernelVec3& prevPos, const KernelVec3& pos, const KernelVec3& vel,
                                         const KernelVec3& bodyPos, float rs, float innerRadius, float outerRadius,
                                         int bodyIndex, KernelPixelResult& result) {
    if ((prevPos.z > 0.0f) == (pos.z > 0.0f)) {
        return false;
    }
    const float t = prevPos.z / (prevPos.z - pos.z);
    const KernelVec3 hitPos = prevPos + t * (pos - prevPos);
    const float hitR = Length(hitPos - bodyPos);

    if (hitR < innerRadius || hitR > outerRadius) {
        return false;
    }

    const float vLocal = 1.0f / sqrtf(fmaxf(1.0e-6f, hitR - rs));
    const float gammaLocal = 1.0f / sqrtf(fmaxf(1.0e-9f, 1.0f - vLocal * vLocal));

    const KernelVec3 zHat{0.0f, 0.0f, 1.0f};
    const KernelVec3 diskVelocityDir = Normalize(Cross(zHat, hitPos - bodyPos));

    const float cosPsi = Dot(vel, diskVelocityDir);
    const float dopplerFactor = 1.0f / (gammaLocal * (1.0f - vLocal * cosPsi));
    const float gravitationalFactor = sqrtf(fmaxf(0.0f, 1.0f - rs / hitR));

    result.hitDisk = true;
    result.diskHitRadius = hitR;
    result.diskRedshiftFactor = gravitationalFactor * dopplerFactor;
    result.diskBodyIndex = bodyIndex;
    return true;
}

__device__ KernelPixelResult TracePhotonBinaryDevice(const KernelVec3& origin, const KernelVec3& directionIn,
                                                       const KernelBinaryConfig& config,
                                                       const KernelBinaryBodies& bodies) {
    KernelPixelResult result;

    KernelVec3 pos = origin;
    KernelVec3 vel = Normalize(directionIn);
    KernelVec3 prevPos = origin;

    for (int i = 0; i < config.maxSteps; ++i) {
        const float dist1 = Length(pos - bodies.position1);
        const float dist2 = Length(pos - bodies.position2);
        const float distToNearest = fminf(dist1, dist2);
        const float h = fminf(config.maxStep, fmaxf(config.minStep, config.baseStep * distToNearest));

        const KernelVec3 k1x = vel;
        const KernelVec3 k1v = BinaryAccel(pos, bodies.position1, bodies.gm1, bodies.position2, bodies.gm2);
        const KernelVec3 k2x = vel + (0.5f * h) * k1v;
        const KernelVec3 k2v =
            BinaryAccel(pos + (0.5f * h) * k1x, bodies.position1, bodies.gm1, bodies.position2, bodies.gm2);
        const KernelVec3 k3x = vel + (0.5f * h) * k2v;
        const KernelVec3 k3v =
            BinaryAccel(pos + (0.5f * h) * k2x, bodies.position1, bodies.gm1, bodies.position2, bodies.gm2);
        const KernelVec3 k4x = vel + h * k3v;
        const KernelVec3 k4v = BinaryAccel(pos + h * k3x, bodies.position1, bodies.gm1, bodies.position2, bodies.gm2);

        pos = pos + (h / 6.0f) * (k1x + 2.0f * k2x + 2.0f * k3x + k4x);
        vel = vel + (h / 6.0f) * (k1v + 2.0f * k2v + 2.0f * k3v + k4v);

        // Constant coordinate speed by construction of this approximate
        // scheme -- only direction bends, so renormalize rather than
        // trusting the RK4 step's own magnitude.
        const float velLen = Length(vel);
        if (velLen > 1.0e-12f) {
            vel = (1.0f / velLen) * vel;
        }

        if (!isfinite(pos.x) || !isfinite(pos.y) || !isfinite(pos.z)) {
            result.fate = KernelFate::Unresolved;
            return result;
        }

        const float newDist1 = Length(pos - bodies.position1);
        const float newDist2 = Length(pos - bodies.position2);

        if (config.checkDisk) {
            if (BinaryCheckDiskCrossing(prevPos, pos, vel, bodies.position1, bodies.rs1, config.disk1InnerRadius,
                                         config.disk1OuterRadius, 0, result)) {
                return result;
            }
            if (BinaryCheckDiskCrossing(prevPos, pos, vel, bodies.position2, bodies.rs2, config.disk2InnerRadius,
                                         config.disk2OuterRadius, 1, result)) {
                return result;
            }
        }
        prevPos = pos;

        if (newDist1 <= bodies.captureRadius1 || newDist2 <= bodies.captureRadius2) {
            result.fate = KernelFate::Captured;
            return result;
        }

        if (Length(pos) >= config.escapeRadius) {
            result.fate = KernelFate::Escaped;
            result.escapeDirection = vel;
            return result;
        }
    }

    result.fate = KernelFate::Unresolved;
    return result;
}

__global__ void TraceBinaryPhotonsKernel(KernelCameraFrame camera, KernelBinaryConfig config,
                                          KernelBinaryBodies bodies, int width, int height,
                                          KernelPixelResult* output) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const float ndcX = ((x + 0.5f) / width * 2.0f - 1.0f) * camera.aspect * camera.tanHalfFovY;
    const float ndcY = (1.0f - (y + 0.5f) / height * 2.0f) * camera.tanHalfFovY;

    const KernelVec3 direction = Normalize(camera.forward + ndcX * camera.right + ndcY * camera.up);
    output[static_cast<size_t>(y) * width + x] = TracePhotonBinaryDevice(camera.position, direction, config, bodies);
}

// Persistent device (and pinned host) buffers, reused across calls instead
// of cudaMalloc/cudaFree'd every frame. In a live-rendering app this
// function is called ~10+ times a second at a size that only changes when
// the window is resized or the resolution slider moves -- allocating and
// freeing GPU memory on that cadence is pure overhead (cudaMalloc is not a
// cheap syscall-free operation), and was a real, measurable contributor to
// the app feeling laggy despite the kernel itself being fast. Grows to fit
// the largest size requested so far; never shrinks (avoids realloc churn
// when toggling the resolution slider back and forth). This app only ever
// calls LaunchGeodesicKernel from one thread (the render loop), so plain
// statics are safe here -- no synchronization needed.
KernelPixelResult* g_deviceResults = nullptr;
KernelPixelResult* g_hostResultsPinned = nullptr; // page-locked host memory: faster cudaMemcpy than a plain std::vector
size_t g_bufferCapacity = 0;

bool EnsureBufferCapacity(size_t pixelCount) {
    if (pixelCount <= g_bufferCapacity && g_deviceResults != nullptr) {
        return true;
    }
    if (g_deviceResults) cudaFree(g_deviceResults);
    if (g_hostResultsPinned) cudaFreeHost(g_hostResultsPinned);
    g_deviceResults = nullptr;
    g_hostResultsPinned = nullptr;
    g_bufferCapacity = 0;

    cudaError_t err = cudaMalloc(&g_deviceResults, pixelCount * sizeof(KernelPixelResult));
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA: cudaMalloc failed: %s\n", cudaGetErrorString(err));
        return false;
    }
    err = cudaMallocHost(&g_hostResultsPinned, pixelCount * sizeof(KernelPixelResult));
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA: cudaMallocHost failed: %s\n", cudaGetErrorString(err));
        cudaFree(g_deviceResults);
        g_deviceResults = nullptr;
        return false;
    }
    g_bufferCapacity = pixelCount;
    return true;
}

} // namespace

namespace CudaTracing {

bool CudaDeviceAvailable() {
    int count = 0;
    const cudaError_t err = cudaGetDeviceCount(&count);
    return err == cudaSuccess && count > 0;
}

const KernelPixelResult* LaunchGeodesicKernel(const KernelCameraFrame& camera, const KernelGeodesicConfig& config,
                                               int width, int height) {
    const size_t pixelCount = static_cast<size_t>(width) * height;

    if (!EnsureBufferCapacity(pixelCount)) {
        return nullptr;
    }

    const dim3 block(16, 16);
    const dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    TracePhotonsKernel<<<grid, block>>>(camera, config, width, height, g_deviceResults);

    cudaError_t err = cudaGetLastError();
    if (err == cudaSuccess) {
        err = cudaDeviceSynchronize();
    }
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA: kernel launch failed: %s\n", cudaGetErrorString(err));
        return nullptr;
    }

    // Straight into the pinned buffer, which the caller reads directly --
    // no intermediate std::vector copy (see the header comment).
    cudaMemcpy(g_hostResultsPinned, g_deviceResults, pixelCount * sizeof(KernelPixelResult), cudaMemcpyDeviceToHost);
    return g_hostResultsPinned;
}

const KernelPixelResult* LaunchKerrGeodesicKernel(const KernelCameraFrame& camera, const KernelKerrConfig& config,
                                                   float spin, int width, int height) {
    const size_t pixelCount = static_cast<size_t>(width) * height;

    // Shares EnsureBufferCapacity/g_deviceResults/g_hostResultsPinned with
    // LaunchGeodesicKernel above -- same KernelPixelResult element type,
    // and only one of the two kernels is ever launched for a given frame
    // (App.cpp picks one based on BlackHole::Spin()), so there's no
    // concurrent-use hazard to separate buffers for.
    if (!EnsureBufferCapacity(pixelCount)) {
        return nullptr;
    }

    const dim3 block(16, 16);
    const dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    TraceKerrPhotonsKernel<<<grid, block>>>(camera, config, spin, width, height, g_deviceResults);

    cudaError_t err = cudaGetLastError();
    if (err == cudaSuccess) {
        err = cudaDeviceSynchronize();
    }
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA: Kerr kernel launch failed: %s\n", cudaGetErrorString(err));
        return nullptr;
    }

    cudaMemcpy(g_hostResultsPinned, g_deviceResults, pixelCount * sizeof(KernelPixelResult), cudaMemcpyDeviceToHost);
    return g_hostResultsPinned;
}

const KernelPixelResult* LaunchBinaryGeodesicKernel(const KernelCameraFrame& camera, const KernelBinaryConfig& config,
                                                     const KernelBinaryBodies& bodies, int width, int height) {
    const size_t pixelCount = static_cast<size_t>(width) * height;

    // Shares the same persistent buffers as the other two Launch*
    // functions -- see LaunchKerrGeodesicKernel's comment for why that's
    // safe (same element type, only one kernel launched per frame).
    if (!EnsureBufferCapacity(pixelCount)) {
        return nullptr;
    }

    const dim3 block(16, 16);
    const dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    TraceBinaryPhotonsKernel<<<grid, block>>>(camera, config, bodies, width, height, g_deviceResults);

    cudaError_t err = cudaGetLastError();
    if (err == cudaSuccess) {
        err = cudaDeviceSynchronize();
    }
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA: binary kernel launch failed: %s\n", cudaGetErrorString(err));
        return nullptr;
    }

    cudaMemcpy(g_hostResultsPinned, g_deviceResults, pixelCount * sizeof(KernelPixelResult), cudaMemcpyDeviceToHost);
    return g_hostResultsPinned;
}

} // namespace CudaTracing
