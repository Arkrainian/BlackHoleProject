#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "BlackHole.h"

namespace Physics {

// Two point masses in a circular mutual (Newtonian) orbit about their common
// barycenter, in sim units where GM1/c^2 = 1 (the SAME convention
// BlackHole.h uses for "the" black hole -- body 1 here IS that black hole;
// massRatio is body 2's mass relative to it, so GM2 = massRatio in these
// units). This is NOT an attempt at an exact two-body solution in general
// relativity -- no closed-form exact solution for two orbiting black holes
// exists at all, let alone one simple enough to integrate photons through
// in real time. What this provides is the two masses' POSITIONS as a
// function of time, exactly correct for Newtonian circular-orbit two-body
// mechanics (itself an approximation to the real GR two-body problem, which
// would also inspiral and radiate gravitational waves -- neither modeled
// here; see BinaryGeodesicSolver.h for how photons are then traced through
// this moving-masses configuration, and docs/physics.md for the full list
// of what is and isn't modeled).
struct BinarySystem {
    double massRatio = 1.0;   // GM2/GM1; 1.0 = equal masses
    double separation = 40.0; // sim units, distance between the two masses (constant -- circular orbit only)
    double phase = 0.0;       // radians, current orbital phase (angle of body 1 from +x, measured at the barycenter)

    // Orbital angular velocity from Kepler's third law generalized to two
    // bodies: Omega^2 = G(M1+M2)/separation^3. In these units GM1=1, so
    // GM_total = 1+massRatio.
    double AngularVelocity() const { return std::sqrt((1.0 + massRatio) / (separation * separation * separation)); }

    double OrbitalPeriod() const { return 2.0 * glm::pi<double>() / AngularVelocity(); }

    // Distances from the barycenter (which sits at the origin -- the SAME
    // point every other scenario in this app already orbits/centers on, so
    // enabling binary mode doesn't require moving the camera's pivot).
    double Radius1() const { return separation * massRatio / (1.0 + massRatio); }
    double Radius2() const { return separation / (1.0 + massRatio); }

    // Each body's Schwarzschild radius in these shared sim units: rs1 is
    // always exactly 2.0 (same as BlackHole::SchwarzschildRadiusSim(),
    // since body 1 literally IS that black hole); rs2 = 2*massRatio.
    double SchwarzschildRadius1() const { return 2.0; }
    double SchwarzschildRadius2() const { return 2.0 * massRatio; }

    // Both bodies orbit in the z=0 plane, diametrically opposite across the
    // barycenter, same angular velocity (rigid circular orbit).
    glm::dvec3 Position1() const { return Radius1() * glm::dvec3(std::cos(phase), std::sin(phase), 0.0); }
    glm::dvec3 Position2() const { return -Radius2() * glm::dvec3(std::cos(phase), std::sin(phase), 0.0); }

    void AdvancePhase(double dt) { phase += AngularVelocity() * dt; }

    // Binary-aware version of Physics::StaticObserverGravityAccelSim: sums
    // each body's own PointMassGravityAccelSim pull at its OWN current
    // position/mass/radius, rather than pulling a test particle toward the
    // origin as if a single mass sat there. This is what StarBody/
    // PlanetBody need once binary mode is active -- without it, a star or
    // planet spawned "around the origin" (the barycenter, still the right
    // pivot -- see this struct's own Radius1()/Radius2() comment) would
    // only ever feel a phantom single-hole pull toward that empty point,
    // never toward either of the two actual, visually rendered masses.
    // Same "doubled Newtonian" superposition approximation
    // BinaryGeodesicSolver already uses for photons (see its own header):
    // not an exact two-body GR solution, just each body's own static-
    // observer law added together.
    glm::dvec3 GravityAccelSim(const glm::dvec3& position) const {
        return PointMassGravityAccelSim(position - Position1(), 1.0, SchwarzschildRadius1()) +
               PointMassGravityAccelSim(position - Position2(), massRatio, SchwarzschildRadius2());
    }

    // True if position has crossed EITHER body's own horizon -- the
    // binary-aware replacement for a single `length(position) < rs`
    // check. whichBody, if non-null, is set to 0 or 1 to say which.
    bool IsCaptured(const glm::dvec3& position, int* whichBody = nullptr) const {
        if (glm::length(position - Position1()) < SchwarzschildRadius1()) {
            if (whichBody) *whichBody = 0;
            return true;
        }
        if (glm::length(position - Position2()) < SchwarzschildRadius2()) {
            if (whichBody) *whichBody = 1;
            return true;
        }
        return false;
    }
};

} // namespace Physics
