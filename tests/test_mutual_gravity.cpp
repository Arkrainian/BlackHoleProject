#include "TestFramework.h"

#include <vector>

#include <glm/glm.hpp>

#include "Astronomy/PlanetBody.h"
#include "Astronomy/StarBody.h"
#include "Physics/BlackHole.h"
#include "Physics/GravitySource.h"

using namespace TestFramework;
using namespace Physics;

// Validates Physics::GravitySource/MutualGravityAccelSim and the
// StarBody::Update/PlanetBody::Update/UpdateGPU parameters that use them --
// object-level mutual gravity between spawned stars/planets (see
// src/Application/App.cpp's "Mutual Gravity" panel). Both stars here are
// placed VERY far from the primary black hole (position.x = 1.0e6) --
// Physics::CombinedGravityAccelSim always adds the primary's own pull (see
// its comment: StaticObserverGravityAccelSim runs unconditionally, even
// with no binary/extras/mutual sources), so at a more modest distance the
// primary's tiny but nonzero tidal pull on the pair shows up as spurious
// separation drift even with mutual gravity off, and can even swamp the
// mutual pull being tested for in test 2 below. At 1e6 units the primary's
// effect on a 20-unit-wide pair is many orders of magnitude smaller than
// the tuned mutual-gravity force (~1e-17 vs ~1e-3 in sim accel units), so
// these tests are purely about whether two objects pull on each other.
void RunMutualGravityTests() {
    // 1. With mutual gravity ON, two stars pull toward each other (their
    // separation decreases over time); with it OFF (the default), they
    // drift apart at their initial relative velocity, completely unchanged
    // by each other's presence.
    {
        Astronomy::StarBodySettings settings;
        settings.particleCount = 1;
        // Exactly zero, not merely small -- StarBody still scatters a
        // single particle to a random point *within* initialRadius (see
        // StarBody.cpp's RandomPointInSphere), so a "small" nonzero radius
        // still introduces run-to-run jitter on the order of that radius,
        // which swamped this test's sub-0.01 precision expectations the
        // first time this was tried.
        settings.initialRadius = 0.0;
        settings.totalMassKg = 1.98892e30; // 1 solar mass

        const glm::dvec3 center(1.0e6, 0.0, 0.0);
        const glm::dvec3 posA = center + glm::dvec3(-10.0, 0.0, 0.0);
        const glm::dvec3 posB = center + glm::dvec3(10.0, 0.0, 0.0);
        const double initialSeparation = glm::length(posA - posB);

        Astronomy::StarBody starA_on(posA, glm::dvec3(0.0), settings);
        Astronomy::StarBody starB_on(posB, glm::dvec3(0.0), settings);
        Astronomy::StarBody starA_off(posA, glm::dvec3(0.0), settings);
        Astronomy::StarBody starB_off(posB, glm::dvec3(0.0), settings);

        const double dt = 0.02;
        for (int step = 0; step < 200; ++step) {
            std::vector<GravitySource> sourcesForA{{starB_on.CenterOfMass(), starB_on.CurrentMassKg()}};
            std::vector<GravitySource> sourcesForB{{starA_on.CenterOfMass(), starA_on.CurrentMassKg()}};
            starA_on.Update(dt, nullptr, nullptr, &sourcesForA, 1.0);
            starB_on.Update(dt, nullptr, nullptr, &sourcesForB, 1.0);

            // Mutual gravity left at its default (off) -- no sources passed.
            starA_off.Update(dt);
            starB_off.Update(dt);
        }

        const double separationOn = glm::length(starA_on.CenterOfMass() - starB_on.CenterOfMass());
        const double separationOff = glm::length(starA_off.CenterOfMass() - starB_off.CenterOfMass());

        ExpectTrue(separationOn < initialSeparation,
                   "With mutual gravity on, two stars' separation decreases (they pull toward each other)");
        ExpectNear(separationOff, initialSeparation, 1e-9,
                   "With mutual gravity off (default), two stationary stars' separation is unchanged");
    }

    // 2. A star placed near a much more massive star curves measurably
    // toward it: starting at rest, it should pick up velocity directed
    // toward the massive star within a short run.
    {
        Astronomy::StarBodySettings lightSettings;
        lightSettings.particleCount = 1;
        lightSettings.initialRadius = 0.0; // deterministic -- see test 1's comment
        lightSettings.totalMassKg = 1.98892e30; // 1 solar mass

        Astronomy::StarBodySettings heavySettings = lightSettings;
        heavySettings.totalMassKg = 1.98892e31; // 10 solar masses

        const glm::dvec3 center(1.0e6, 0.0, 0.0);
        const glm::dvec3 lightPos = center + glm::dvec3(-15.0, 0.0, 0.0);
        const glm::dvec3 heavyPos = center + glm::dvec3(15.0, 0.0, 0.0);

        Astronomy::StarBody lightStar(lightPos, glm::dvec3(0.0), lightSettings);
        Astronomy::StarBody heavyStar(heavyPos, glm::dvec3(0.0), heavySettings);

        const double dt = 0.02;
        for (int step = 0; step < 100; ++step) {
            std::vector<GravitySource> sourcesForLight{{heavyStar.CenterOfMass(), heavyStar.CurrentMassKg()}};
            std::vector<GravitySource> sourcesForHeavy{{lightStar.CenterOfMass(), lightStar.CurrentMassKg()}};
            lightStar.Update(dt, nullptr, nullptr, &sourcesForLight, 1.0);
            heavyStar.Update(dt, nullptr, nullptr, &sourcesForHeavy, 1.0);
        }

        const glm::dvec3 displacement = lightStar.CenterOfMass() - lightPos;
        // Toward the heavy star means a positive x-displacement here.
        ExpectTrue(displacement.x > 0.02,
                   "A star near a much more massive star curves measurably toward it");
    }

    // 3. PlanetBody::UpdateGPU falls back to CPU (returns false, leaves
    // state untouched) when mutual gravity sources are supplied -- the GPU
    // kernel only knows the single-hole law, same precedent already
    // established for binary mode and extra black holes.
    {
        BlackHole blackHole(10.0);
        Astronomy::PlanetBodySettings settings;
        settings.particleCount = 10;
        Astronomy::PlanetBody planet(glm::dvec3(2000.0, 0.0, 0.0), glm::dvec3(0.0), settings);

        std::vector<GravitySource> mutualSources{{glm::dvec3(2050.0, 0.0, 0.0), 1.98892e30}};
        const bool gpuSucceeded = planet.UpdateGPU(0.02, blackHole, nullptr, nullptr, &mutualSources, 1.0);
        ExpectTrue(!gpuSucceeded,
                   "PlanetBody::UpdateGPU falls back to CPU (returns false) when mutual gravity sources are supplied");
    }
}
