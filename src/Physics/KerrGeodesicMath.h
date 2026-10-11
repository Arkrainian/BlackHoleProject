#pragma once

#include <cmath>

#include "GeodesicMath.h" // BH_HOST_DEVICE
#include "KerrMetric.h"

// Null-geodesic right-hand sides for Kerr spacetime, via Carter's (1968)
// separation of the Hamilton-Jacobi equation. This is the standard
// formulation used by essentially every Kerr ray tracer (Bardeen, Press &
// Teukolsky 1972 sec. II; reproduced in e.g. Chan/Psaltis/Ozel 2013 "GRay",
// Dexter & Agol's geokerr, Vincent et al.'s GYOTO) -- unlike Schwarzschild's
// u(phi) reduction (GeodesicMath.h), a general (non-equatorial) Kerr photon
// orbit is NOT planar, so there is no single monotonic angle to integrate
// against; instead each conserved quantity (E, Lz, Carter's Q) gives a
// decoupled first-order ODE in the affine parameter lambda, each of the form
// (dx/dlambda)^2 = (something in x alone), so the only new numerical
// subtlety vs. Schwarzschild is handling the +/- sign choice at a turning
// point (R(r)=0 or Theta(theta)=0) -- see KerrGeodesicSolver.cpp.
//
// Templated on T exactly like GeodesicMath.h, for the same reason (future
// CUDA port at T=float) -- the CPU solver instantiates at T=double today;
// no CUDA kernel calls this yet (Kerr is CPU-only for now, see
// KerrGeodesicSolver.h).
namespace Physics::KerrGeodesicMath {

// Radial potential R(r) = [E(r^2+a^2) - a*Lz]^2 - Delta*[(Lz-a*E)^2 + Q].
// Sigma^2 (dr/dlambda)^2 = R(r). R(r) < 0 is unphysical for the chosen
// (E,Lz,Q) at that r -- it marks a turning point the integrator must not
// cross without flipping the radial sign (handled by the caller).
template <typename T>
BH_HOST_DEVICE inline T RadialPotential(T r, T a, T E, T Lz, T Q) {
    const T P = E * (r * r + a * a) - a * Lz;
    const T d = Kerr::Delta(r, a);
    const T term = Lz - a * E;
    return P * P - d * (term * term + Q);
}

// Polar potential Theta(theta) = Q - cos^2(theta)*(Lz^2/sin^2(theta) - a^2*E^2).
// Sigma^2 (dtheta/dlambda)^2 = Theta(theta).
// Note: deliberately uses a manual ternary rather than std::max/std::clamp
// throughout this file (and KerrMetric.h's BH_HOST_DEVICE functions) -- the
// project's existing shared host/device math (GeodesicMath.h's
// AdaptivePhiStep) avoids <algorithm> the same way, since nvcc's device
// compilation of std::max/std::clamp isn't guaranteed without extra compile
// flags this project doesn't set, unlike std::sqrt/sin/cos/pow, which nvcc
// does provide device overloads for.
template <typename T>
BH_HOST_DEVICE inline T PolarPotential(T cosTheta, T sinTheta, T a, T E, T Lz, T Q) {
    T sin2 = sinTheta * sinTheta;
    if (sin2 < T(1e-12)) sin2 = T(1e-12);
    return Q - cosTheta * cosTheta * (Lz * Lz / sin2 - a * a * E * E);
}

// Sigma * (dphi/dlambda) = -(a*E - Lz/sin^2(theta)) + a*[E(r^2+a^2)-a*Lz]/Delta.
template <typename T>
BH_HOST_DEVICE inline T PhiRate(T r, T sinTheta, T a, T E, T Lz) {
    T sin2 = sinTheta * sinTheta;
    if (sin2 < T(1e-12)) sin2 = T(1e-12);
    const T d = Kerr::Delta(r, a);
    const T P = E * (r * r + a * a) - a * Lz;
    return -(a * E - Lz / sin2) + a * P / d;
}

// Packed derivatives of (r, theta, phi) w.r.t. affine parameter lambda,
// given the current signed radial/polar motion direction (rSign, thSign in
// {-1,+1}, flipped by the caller at R=0/Theta=0 turning points).
template <typename T>
struct StateDerivative {
    T dr;
    T dtheta;
    T dphi;
};

template <typename T>
BH_HOST_DEVICE inline StateDerivative<T> Derivatives(T r, T cosTheta, T sinTheta, T a, T E, T Lz, T Q, T rSign,
                                                       T thSign) {
    const T sigma = Kerr::Sigma(r, cosTheta, a);
    const T R = RadialPotential(r, a, E, Lz, Q);
    const T Theta = PolarPotential(cosTheta, sinTheta, a, E, Lz, Q);

    const T Rclamped = R < T(0.0) ? T(0.0) : R;
    const T Thetaclamped = Theta < T(0.0) ? T(0.0) : Theta;

    StateDerivative<T> out;
    out.dr = rSign * std::sqrt(Rclamped) / sigma;
    out.dtheta = thSign * std::sqrt(Thetaclamped) / sigma;
    out.dphi = PhiRate(r, sinTheta, a, E, Lz) / sigma;
    return out;
}

// One RK4 step of the (r, theta, phi) system in affine parameter lambda, at
// fixed radial/polar travel-direction signs (the caller is responsible for
// detecting turning points -- R(r)=0 or Theta(theta)=0 -- and flipping the
// relevant sign; see KerrGeodesicSolver.cpp). phi's derivative doesn't feed
// back into dr/dtheta (Kerr is axisymmetric, the metric has no phi
// dependence), so it's safe to integrate by simple quadrature alongside the
// (r, theta) subsystem rather than needing its own coupled stages.
template <typename T>
BH_HOST_DEVICE inline void RK4Step(T& r, T& theta, T& phi, T h, T a, T E, T Lz, T Q, T rSign, T thSign) {
    const StateDerivative<T> k1 = Derivatives(r, std::cos(theta), std::sin(theta), a, E, Lz, Q, rSign, thSign);
    const T r2 = r + T(0.5) * h * k1.dr;
    const T th2 = theta + T(0.5) * h * k1.dtheta;
    const StateDerivative<T> k2 = Derivatives(r2, std::cos(th2), std::sin(th2), a, E, Lz, Q, rSign, thSign);
    const T r3 = r + T(0.5) * h * k2.dr;
    const T th3 = theta + T(0.5) * h * k2.dtheta;
    const StateDerivative<T> k3 = Derivatives(r3, std::cos(th3), std::sin(th3), a, E, Lz, Q, rSign, thSign);
    const T r4 = r + h * k3.dr;
    const T th4 = theta + h * k3.dtheta;
    const StateDerivative<T> k4 = Derivatives(r4, std::cos(th4), std::sin(th4), a, E, Lz, Q, rSign, thSign);

    r += (h / T(6.0)) * (k1.dr + T(2.0) * k2.dr + T(2.0) * k3.dr + k4.dr);
    theta += (h / T(6.0)) * (k1.dtheta + T(2.0) * k2.dtheta + T(2.0) * k3.dtheta + k4.dtheta);
    phi += (h / T(6.0)) * (k1.dphi + T(2.0) * k2.dphi + T(2.0) * k3.dphi + k4.dphi);
}

} // namespace Physics::KerrGeodesicMath
