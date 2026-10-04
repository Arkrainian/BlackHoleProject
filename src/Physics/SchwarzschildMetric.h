#pragma once

#include "GeodesicMath.h" // for the BH_HOST_DEVICE macro

namespace Physics::Schwarzschild {

// Metric function f(r) = 1 - r_s/r that appears throughout the Schwarzschild
// metric ds^2 = -f(r) dt^2 + dr^2/f(r) + r^2(dtheta^2 + sin^2(theta) dphi^2).
// Shared by the geodesic solver (tetrad construction, see GeodesicSolver.cpp)
// and later the accretion-disk redshift calculation (Stage 6), so it lives
// here rather than being duplicated. BH_HOST_DEVICE since the CUDA kernel
// (cuda/geodesic_kernel.cu) also calls this directly for its disk redshift
// calculation. Templated on T for the same reason as GeodesicMath.h's
// functions: the CPU path stays at T=double, the CUDA kernel uses T=float.
template <typename T>
BH_HOST_DEVICE inline T LapseFactor(T r, T schwarzschildRadius) {
    return T(1.0) - schwarzschildRadius / r;
}

} // namespace Physics::Schwarzschild
