#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Physics/BinarySystem.h"
#include "Physics/MultiBlackHoleSystem.h"

namespace Astronomy {

// One particle of a StarBody -- see StarBody below for why a star is a
// cluster of these rather than a single rigid point.
struct StarParticle {
    glm::dvec3 position;
    glm::dvec3 velocity;

    // Short rolling history of recent positions, oldest first, capped at
    // StarBodySettings::trailLength -- drawn as a short polyline per
    // particle so the cluster reads as a stretching streak (the
    // "spaghetti") rather than a handful of disconnected dots once tidal
    // forces start separating the particles.
    std::vector<glm::dvec3> trail;

    // True once this particle has crossed the event horizon. Consumed
    // particles stop integrating and stop drawing -- "sucked in" for real,
    // not faded out cosmetically.
    bool consumed = false;
};

struct StarBodySettings {
    // Sim units -- the star's initial physical size, i.e. the radius of the
    // small sphere its particles are seeded within. Real stars are many
    // orders of magnitude smaller than a black hole's Schwarzschild radius,
    // but this sim deliberately exaggerates it (comparable to a few r_s) so
    // the tidal spread is visible on screen within a reasonable orbit
    // rather than needing a telescope-grade zoom to notice.
    double initialRadius = 1.2;
    int particleCount = 28;
    int trailLength = 50;
    glm::vec3 color = glm::vec3(1.0f, 0.92f, 0.75f);

    // SI kilograms. Stars never had a mass concept before mutual gravity
    // (Physics::GravitySource) needed one -- it's still not fed into the
    // per-particle force law (every particle stays a massless test
    // particle under the black hole's/binary's/extra-black-holes' field,
    // same as always), only used so this star can act as a gravity SOURCE
    // for other spawned stars/planets. Defaults to one solar mass.
    double totalMassKg = 1.98892e30;
};

// A star, modeled as a small cluster of independent test particles rather
// than one rigid body. This is deliberate, not a simplification of
// convenience: tidal disruption ("spaghettification") is exactly what
// happens when an extended body's near side and far side feel measurably
// different gravity and nothing holds them together against that
// difference. Simulating ~28 particles that each independently free-fall
// under the EXACT same point-mass law as everything else in this sim
// (Physics::StaticObserverGravityAccelSim -- Newtonian far away, the exact
// GR static-observer acceleration near the horizon) reproduces that
// stretching as a direct CONSEQUENCE of real physics, with no separate
// "stretch the mesh" formula standing in for it. What's intentionally left
// out: the star's own self-gravity (which in reality holds it together
// until tidal forces exceed it -- see the Roche/tidal-disruption radius).
// Omitting it means every star here is already at the moment self-gravity
// has lost; there's no "approaching but still intact" phase. Documented as
// a limitation, same spirit as docs/physics.md's others.
class StarBody {
public:
    StarBody(const glm::dvec3& centerPosition, const glm::dvec3& centerVelocity,
             const StarBodySettings& settings = {});

    // Integrates every non-consumed particle one step (semi-implicit Euler,
    // matching Jet::FlightPhysics's integration style) and marks any that
    // crossed the horizon as consumed. binary, when non-null, switches
    // both the gravity law and the horizon check to pull toward/check
    // against BOTH of the binary system's bodies at their own current
    // positions (Physics::BinarySystem::GravityAccelSim/IsCaptured)
    // instead of a single mass at the origin -- pass it whenever binary
    // mode is active, or the star will behave as if an invisible single
    // hole still sits at the barycenter instead of responding to the two
    // actual bodies on screen. extraBlackHoles, when non-null, adds every
    // listed Physics::ExtraBlackHole's own pull/horizon on top of whatever
    // else is active (binary and extras can both be set at once) -- see
    // Physics/MultiBlackHoleSystem.h. mutualGravitySources/
    // mutualGravityStrength, when non-null, add every listed
    // Physics::GravitySource's tuned mutual pull on top of everything else
    // -- see Physics/GravitySource.h for why this is a deliberately
    // exaggerated, standalone force law, same spirit as PlanetBody's own
    // self-gravity.
    void Update(double deltaTime, const Physics::BinarySystem* binary = nullptr,
                const std::vector<Physics::ExtraBlackHole>* extraBlackHoles = nullptr,
                const std::vector<Physics::GravitySource>* mutualGravitySources = nullptr,
                double mutualGravityStrength = 1.0);

    bool IsFullyConsumed() const;
    const std::vector<StarParticle>& Particles() const { return m_particles; }
    const glm::vec3& Color() const { return m_settings.color; }

    // For Physics::GravitySource construction (mutual gravity, see
    // App.cpp): this star's current center of mass (plain average of live
    // particle positions -- all particles are assumed equal mass, same
    // approximation the rest of StarBody already makes) and its current
    // mass (totalMassKg scaled by the live/total particle-count fraction,
    // the simplest available proxy for "how much of this star is left"
    // since individual particles don't carry their own mass).
    glm::dvec3 CenterOfMass() const;
    double CurrentMassKg() const;

private:
    std::vector<StarParticle> m_particles;
    StarBodySettings m_settings;
};

} // namespace Astronomy
