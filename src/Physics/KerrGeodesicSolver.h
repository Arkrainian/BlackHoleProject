#pragma once

#include <glm/glm.hpp>

#include "Photon.h"

namespace Physics {

struct KerrGeodesicConfig {
    double captureMultiplier = 1.001; // capture once r <= captureMultiplier * r_outer_horizon
    double escapeRadius = 1.0e5;

    // Unlike the Schwarzschild solver's basePhiStep (an angle in phi, since
    // phi is monotonic and can be used as the integration variable), a
    // general Kerr photon orbit isn't planar, so there's no single
    // monotonic angle -- these are steps in the affine parameter lambda
    // instead, scaled by the current r (see KerrGeodesicSolver.cpp).
    // h = clamp(baseLambdaStep * r, minLambdaStep, maxLambdaStep) -- i.e.
    // the step naturally grows with r (dr/dlambda is O(1) far from the
    // hole, so a step proportional to r keeps the RELATIVE radial progress
    // roughly constant, the affine-parameter analogue of the Schwarzschild
    // solver's phi stepping). maxLambdaStep must stay well above
    // escapeRadius * baseLambdaStep or escaping rays will exhaust
    // maxSteps while crawling outward at a capped step size without ever
    // reaching escapeRadius -- this is exactly the bug tests/test_kerr.cpp
    // caught during development (an UNRESOLVED result purely from step
    // starvation, not physics).
    double baseLambdaStep = 0.02;
    double minLambdaStep = 1.0e-5;
    double maxLambdaStep = 5.0e4;
    int maxSteps = 200000;
    bool recordPath = false;
    int maxPathPoints = 2000;

    bool checkDisk = false;
    double diskInnerRadius = 6.0;
    double diskOuterRadius = 26.0;
    // Kerr disk material is assumed prograde (co-rotating with the black
    // hole's spin) -- the physically standard assumption for a real
    // accretion disk, since Lense-Thirring/Bardeen-Petterson torquing
    // aligns a disk with the spin over time. Unlike the Schwarzschild
    // solver's diskClockwise flag (a pure visualization choice with no
    // physically preferred direction there), Kerr's rotation sense is a
    // direct consequence of the spin's sign, so there is no separate
    // direction flag here.
};

// Integrates one photon's null geodesic through general (non-equatorial)
// Kerr spacetime in Boyer-Lindquist coordinates, via Carter's (1968)
// separation of the Hamilton-Jacobi equation -- see KerrGeodesicMath.h for
// the governing R(r)/Theta(theta) potentials and docs/physics.md for the
// full derivation and the spatial-embedding approximation this uses to
// convert the camera's Cartesian look direction into (r, theta, phi) and
// back. spin is the dimensionless Kerr parameter a* = a/M (sim units, same
// convention as BlackHole::Spin()); passing spin=0 integrates the a=0
// special case of the SAME equations (not a separate code path), which is
// checked against the dedicated Schwarzschild solver as a regression test
// in tests/test_kerr.cpp.
//
// CPU-only for now: no CUDA kernel calls this yet (see HANDOFF.md) -- the
// CUDA tracer remains Schwarzschild-only, and CPUTracer.cpp falls back to
// this solver (instead of the Schwarzschild one) whenever the configured
// black hole has nonzero spin, regardless of whether a CUDA device is
// present.
PhotonTraceResult TracePhotonKerr(const glm::dvec3& origin, const glm::dvec3& direction, double spin,
                                   const KerrGeodesicConfig& config);

} // namespace Physics
