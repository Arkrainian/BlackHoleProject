#pragma once

#include <algorithm>
#include <cmath>

#include "GeodesicMath.h" // for the BH_HOST_DEVICE macro

// Boyer-Lindquist Kerr metric quantities, in sim units (G = c = M = 1, so
// mass-geometrized spin a* = a/M is just "a" here, a in [-1, 1]).
//
// Metric (Boyer-Lindquist t, r, theta, phi):
//   ds^2 = -(1 - 2r/Sigma) dt^2 - (4ar sin^2(theta)/Sigma) dt dphi
//          + (Sigma/Delta) dr^2 + Sigma dtheta^2
//          + (A sin^2(theta)/Sigma) dphi^2
// with Sigma = r^2 + a^2 cos^2(theta), Delta = r^2 - 2r + a^2,
// A = (r^2+a^2)^2 - a^2 Delta sin^2(theta).
//
// a > 0 means the hole spins toward +phi (same sense StarBody/AccretionDisk
// already call "counter-clockwise as seen from +z" when diskClockwise is
// false) -- see KerrGeodesicSolver.h for how that sign convention threads
// through to prograde/retrograde photon orbits.
//
// All formulas below reduce exactly to the existing Schwarzschild ones
// (BlackHole.h, SchwarzschildMetric.h) at a=0 -- this is checked by
// tests/test_kerr.cpp, not just asserted.
namespace Physics::Kerr {

template <typename T>
BH_HOST_DEVICE inline T Sigma(T r, T cosTheta, T a) {
    return r * r + a * a * cosTheta * cosTheta;
}

template <typename T>
BH_HOST_DEVICE inline T Delta(T r, T a) {
    return r * r - T(2.0) * r + a * a;
}

// A(r,theta) = (r^2+a^2)^2 - a^2 Delta sin^2(theta). Appears in g_phiphi,
// the ZAMO lapse/shift, and the frame-dragging angular velocity.
template <typename T>
BH_HOST_DEVICE inline T BigA(T r, T sinTheta, T a) {
    const T rr_aa = r * r + a * a;
    return rr_aa * rr_aa - a * a * Delta(r, a) * sinTheta * sinTheta;
}

// Frame-dragging (ZAMO) angular velocity omega = -g_tphi/g_phiphi = 2ar/A.
template <typename T>
BH_HOST_DEVICE inline T FrameDragOmega(T r, T sinTheta, T a) {
    return T(2.0) * a * r / BigA(r, sinTheta, a);
}

// ZAMO lapse alpha = sqrt(Sigma*Delta/A).
template <typename T>
BH_HOST_DEVICE inline T ZamoLapse(T r, T cosTheta, T sinTheta, T a) {
    const T s = Sigma(r, cosTheta, a);
    const T d = Delta(r, a);
    const T A = BigA(r, sinTheta, a);
    T num = s * d;
    if (num < T(0.0)) num = T(0.0); // manual clamp, not std::max -- see KerrGeodesicMath.h's note
    return std::sqrt(num / A);
}

// Outer/inner horizons r+/- = M +/- sqrt(M^2 - a^2) (sim units, M=1).
// Clamped so a slightly-over-extremal spin (shouldn't happen -- BlackHole
// clamps a*) degrades gracefully instead of taking sqrt of a negative.
inline double OuterHorizonSim(double a) {
    const double disc = std::max(0.0, 1.0 - a * a);
    return 1.0 + std::sqrt(disc);
}

inline double InnerHorizonSim(double a) {
    const double disc = std::max(0.0, 1.0 - a * a);
    return 1.0 - std::sqrt(disc);
}

// Ergosphere (static limit surface) r_erg(theta) = M + sqrt(M^2 - a^2 cos^2(theta)).
// At the equator this equals the Schwarzschild-horizon value 2M; at the
// poles it coincides with the outer horizon -- both standard, well-known
// Kerr results, checked in tests/test_kerr.cpp.
inline double ErgosphereRadiusSim(double a, double cosTheta) {
    const double disc = std::max(0.0, 1.0 - a * a * cosTheta * cosTheta);
    return 1.0 + std::sqrt(disc);
}

// Equatorial circular photon-orbit radius (Bardeen 1973). prograde=true
// gives the orbit co-rotating with the hole's spin (smaller radius, down
// to r=M at extremal spin); prograde=false gives the counter-rotating one
// (larger radius, up to r=4M at extremal spin). Reduces to r=3M at a=0
// for both, matching the existing Schwarzschild photon sphere.
inline double EquatorialPhotonOrbitRadiusSim(double a, bool prograde) {
    const double sign = prograde ? -1.0 : 1.0;
    const double arg = std::clamp(sign * a, -1.0, 1.0);
    return 2.0 * (1.0 + std::cos((2.0 / 3.0) * std::acos(arg)));
}

// Equatorial ISCO radius (Bardeen, Press & Teukolsky 1972). Same
// prograde/retrograde convention as above; reduces to r=6M at a=0.
inline double EquatorialIscoRadiusSim(double a, bool prograde) {
    const double sign = prograde ? 1.0 : -1.0;
    const double oneMinusA2 = std::max(0.0, 1.0 - a * a);
    const double cbrt1mA2 = std::cbrt(oneMinusA2);
    const double z1 = 1.0 + cbrt1mA2 * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    const double z2 = std::sqrt(3.0 * a * a + z1 * z1);
    const double inner = (3.0 - z1) * (3.0 + z1 + 2.0 * z2);
    return 3.0 + z2 - sign * std::sqrt(std::max(0.0, inner));
}

} // namespace Physics::Kerr
