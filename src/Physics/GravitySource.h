#pragma once

#include <algorithm>
#include <vector>

#include <glm/glm.hpp>

namespace Physics {

// A spawned star or planet, reduced to a single point mass for the
// purposes of pulling on OTHER spawned stars/planets (object-level mutual
// gravity -- see Astronomy/StarBody.h and Astronomy/PlanetBody.h for the
// per-object CenterOfMass()/CurrentMassKg() accessors this is built from).
// Deliberately lighter than ExtraBlackHole: no Schwarzschild radius, no
// capture check -- a star or planet getting close to another isn't an
// event horizon, and there's no collision/merge model here, just a
// softened pull (see kMutualGravityMinDistance below).
struct GravitySource {
    glm::dvec3 position{0.0};
    double massKg = 0.0;
};

namespace {

// Reference mass for the mutual-gravity force law below: one solar mass,
// not Earth mass -- stars (the primary use case for "binary star system"
// dynamics) default to 1 solar mass, and the first tuning pass (scripted
// probe, see HANDOFF.md) discovered that referencing Earth mass makes
// massRatio for a solar-mass star ~3.3e5, which blows the per-step
// velocity kick out to many times the objects' own separation at ordinary
// spawn distances/timesteps -- every combination of coefficient/min-
// distance tried against that reference flung the pair apart to huge
// separations within a few hundred steps. Referencing solar mass instead
// keeps massRatio near 1 for the common star-star case (and
// correspondingly tiny for Earth-mass planets, consistent with "even off
// is a physically-honest choice" below).
constexpr double kMutualGravityReferenceMassKg = 1.98892e30;

// Standalone, tuned sim-unit mutual-gravity force constant -- NOT the real
// (other object's mass / black hole's mass) / r^2 formula, for the exact
// same reason PlanetBody's own self-gravity isn't: at any black hole mass
// this app allows, two spawned stars/planets' real mutual pull on each
// other is many orders of magnitude too small to ever visibly matter (and
// the real ratio's scale depends on the configured black hole mass, which
// a fixed multiplier on the real formula could never compensate for
// uniformly). Tuned via scripted probe (see HANDOFF.md): two 1-solar-mass
// stars spawned ~20 sim units apart with a modest tangential kick spiral
// together over several tens of sim-time units, swing past each other, and
// spread back out -- a visible, bounded "close encounter" rather than a
// runaway ejection -- and a pair left ~500 units apart stays essentially
// unaffected (confirming distant/unrelated objects aren't destabilized).
constexpr double kMutualGravityCoefficient = 1.5;

// Minimum separation mutual gravity is evaluated at -- without this, the
// 1/r^2 law diverges as two objects' centers approach each other, which
// (same lesson learned from PlanetBody's self-gravity cutoff) can blow up
// the explicit integrator. This does NOT model a collision or merger --
// it's purely a numerical safety floor. The two objects' actual positions
// can still pass closer than this (the floor only clamps the force, not
// the real distance) -- the probe above saw real closest-approach
// distances well under 1.0 without the integrator diverging once this
// floor was in place.
constexpr double kMutualGravityMinDistance = 3.0;

} // namespace

// Sums the tuned mutual-gravity pull from every entry in `sources` toward
// `position`, scaled by `strength` (the UI's "Mutual Gravity strength"
// slider -- 0 behaves like mutual gravity off). Used by Astronomy::
// StarBody::Update and Astronomy::PlanetBody::Update/UpdateGPU, via
// Physics::CombinedGravityAccelSim, alongside the existing binary/extra-
// black-hole terms. No capture check here -- see GravitySource's own
// comment for why.
inline glm::dvec3 MutualGravityAccelSim(const glm::dvec3& position, const std::vector<GravitySource>& sources,
                                         double strength) {
    glm::dvec3 accel(0.0);
    if (strength <= 0.0) return accel;
    for (const GravitySource& source : sources) {
        const glm::dvec3 delta = source.position - position;
        const double rawR = glm::length(delta);
        if (rawR < 1e-9) continue; // exactly coincident -- no well-defined direction, skip rather than divide by ~0
        const glm::dvec3 direction = delta / rawR;
        const double r = std::max(rawR, kMutualGravityMinDistance);
        const double massRatio = source.massKg / kMutualGravityReferenceMassKg;
        const double magnitude = kMutualGravityCoefficient * strength * massRatio / (r * r);
        accel += direction * magnitude;
    }
    return accel;
}

} // namespace Physics
