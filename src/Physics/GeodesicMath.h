#pragma once

// BH_HOST_DEVICE marks functions callable from both plain C++ (the CPU
// tracer, compiled by MSVC/GCC/Clang) and CUDA device code (the GPU tracer,
// compiled by nvcc) -- __host__ __device__ is a no-op outside of .cu files,
// so this header compiles unmodified under either toolchain. This is the
// literal shared core the project's design calls for: the CPU integrator in
// GeodesicSolver.cpp and the GPU kernel in cuda/geodesic_kernel.cu both call
// THESE functions for the actual ODE step, so the two never drift apart --
// only the surrounding control flow (loops, branching, output layout)
// differs per platform, which is unavoidable given CUDA kernels can't use
// std::vector/std::function.
#if defined(__CUDACC__)
#define BH_HOST_DEVICE __host__ __device__
#else
#define BH_HOST_DEVICE
#endif

namespace Physics::GeodesicMath {

// Templated on the floating-point type T rather than hardcoded to double:
// the CPU path (GeodesicSolver.cpp) instantiates this at T=double, exactly
// as before -- every validated test result is unaffected. The CUDA kernel
// instantiates it at T=float instead, because consumer NVIDIA GPUs (this
// project was profiled on an RTX 4070 Ti Super) deliberately cripple
// double-precision throughput to a small fraction of their single-precision
// rate -- unlike a CPU, where double and float cost about the same, on this
// class of GPU double-precision arithmetic is the dominant cost of the
// entire kernel. T is deduced from the caller's argument types, so no CPU
// call site needs to change to keep using double.

// Right-hand side of the reduced null-geodesic ODE (see GeodesicSolver.h
// for the derivation): d^2u/dphi^2 = -u + (3/2) r_s u^2.
template <typename T>
BH_HOST_DEVICE inline T Accel(T u, T rs) {
    return -u + T(1.5) * rs * u * u;
}

// One RK4 step of the (u, v=du/dphi) system.
template <typename T>
BH_HOST_DEVICE inline void RK4Step(T& u, T& v, T h, T rs) {
    const T k1u = v;
    const T k1v = Accel(u, rs);

    const T k2u = v + T(0.5) * h * k1v;
    const T k2v = Accel(u + T(0.5) * h * k1u, rs);

    const T k3u = v + T(0.5) * h * k2v;
    const T k3v = Accel(u + T(0.5) * h * k2u, rs);

    const T k4u = v + h * k3v;
    const T k4v = Accel(u + h * k3u, rs);

    u += (h / T(6.0)) * (k1u + T(2.0) * k2u + T(2.0) * k3u + k4u);
    v += (h / T(6.0)) * (k1v + T(2.0) * k2v + T(2.0) * k3v + k4v);
}

// Curvature-adaptive step heuristic (see GeodesicSolver.cpp's
// AdaptivePhiStep for the reasoning): shrinks d(phi) where |Accel| is
// large, clamped to [minPhiStep, maxPhiStep].
template <typename T>
BH_HOST_DEVICE inline T AdaptivePhiStep(T u, T rs, T basePhiStep, T minPhiStep, T maxPhiStep, bool adaptive) {
    if (!adaptive) {
        return basePhiStep;
    }
    T accel = Accel(u, rs);
    if (accel < T(0.0)) accel = -accel;
    constexpr T curvatureGain = T(40.0);
    const T scale = T(1.0) / (T(1.0) + curvatureGain * accel);
    T step = basePhiStep * scale;
    if (step < minPhiStep) step = minPhiStep;
    if (step > maxPhiStep) step = maxPhiStep;
    return step;
}

} // namespace Physics::GeodesicMath
