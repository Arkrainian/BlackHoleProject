#include "TestFramework.h"

#include <array>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "Astronomy/StarBody.h"
#include "Physics/BinaryGeodesicSolver.h"
#include "Physics/BinarySystem.h"
#include "Physics/BlackHole.h"
#include "Physics/MultiBlackHoleSystem.h"
#include "Physics/MultiBodyGeodesicSolver.h"

using namespace TestFramework;
using namespace Physics;

// Validates Physics::TracePhotonMultiBody (the generalized N-body lensing
// solver that activates once 3+ bodies -- primary + binary's second body
// if active + user-placed Physics::ExtraBlackHoles -- are in the scene;
// see src/Application/App.cpp's "Extra Black Holes" panel) and the
// Astronomy::CombinedGravityAccelSim/IsCapturedByAny helpers star/planet
// gravity use for the same purpose.
void RunMultiBodyTests() {
    // 1. Reduction check: with exactly the same two bodies as a BinarySystem
    // (primary + one extra placed at BinarySystem::Position2() with
    // matching mass/radius), TracePhotonMultiBody should agree closely with
    // the existing, independently-implemented TracePhotonBinary for the
    // same ray -- a strong regression/consistency check that generalizing
    // from 2 named bodies to an array didn't change the physics.
    {
        BinarySystem system;
        system.massRatio = 0.6;
        system.separation = 40.0;
        system.phase = 0.7;

        BinaryGeodesicConfig binaryConfig;
        binaryConfig.escapeRadius = 1.0e4;
        binaryConfig.maxSteps = 20000;

        std::array<LensingBody, 2> bodies{};
        // BinarySystem::Position1() is NOT the origin -- body 1 orbits the
        // shared barycenter right alongside body 2 (confirmed the hard way:
        // an earlier version of this test hardcoded glm::dvec3(0.0) here
        // and failed, which is what caught the same bug in CPUTracer.cpp's
        // BuildLensingBodies -- see its fix).
        bodies[0] = LensingBody{system.Position1(), 1.0, system.SchwarzschildRadius1(),
                                 binaryConfig.captureMultiplier * system.SchwarzschildRadius1()};
        bodies[1] = LensingBody{system.Position2(), system.massRatio, system.SchwarzschildRadius2(),
                                 binaryConfig.captureMultiplier * system.SchwarzschildRadius2()};

        MultiBodyGeodesicConfig multiConfig;
        multiConfig.escapeRadius = binaryConfig.escapeRadius;
        multiConfig.maxSteps = binaryConfig.maxSteps;

        const glm::dvec3 origin(-60.0, 8.0, 0.0);
        const glm::dvec3 direction(1.0, 0.0, 0.0);

        const PhotonTraceResult binaryResult = TracePhotonBinary(origin, direction, system, binaryConfig);
        const PhotonTraceResult multiResult = TracePhotonMultiBody(origin, direction, bodies.data(), 2, multiConfig);

        ExpectTrue(binaryResult.fate == multiResult.fate,
                   "TracePhotonMultiBody with 2 bodies agrees with TracePhotonBinary on fate");
        ExpectNear(binaryResult.closestApproach, multiResult.closestApproach, 1e-6,
                   "TracePhotonMultiBody's closest approach matches TracePhotonBinary's");
        if (binaryResult.fate == PhotonFate::ESCAPED && multiResult.fate == PhotonFate::ESCAPED) {
            const double cosAngle = glm::clamp(glm::dot(binaryResult.escapeDirection, multiResult.escapeDirection),
                                                -1.0, 1.0);
            ExpectTrue(std::acos(cosAngle) < 1e-4,
                       "TracePhotonMultiBody's escape direction matches TracePhotonBinary's closely");
        }
    }

    // 2. Capture against an extra (non-primary) body's own horizon, not
    // just body 0's.
    {
        std::array<LensingBody, 3> bodies{};
        bodies[0] = LensingBody{glm::dvec3(0.0), 1.0, 2.0, 2.002};
        bodies[1] = LensingBody{glm::dvec3(30.0, 0.0, 0.0), 0.5, 1.0, 1.001};
        bodies[2] = LensingBody{glm::dvec3(-30.0, 0.0, 0.0), 0.8, 1.6, 1.6016};

        MultiBodyGeodesicConfig config;
        config.escapeRadius = 1.0e4;
        config.maxSteps = 20000;

        // Aimed straight at body 1 (not the primary at the origin) from
        // nearby, with essentially zero impact parameter -- should be
        // captured by body 1, not escape or hit the primary.
        const glm::dvec3 origin(30.0, 0.0, 10.0);
        const glm::dvec3 direction(0.0, 0.0, -1.0);
        const PhotonTraceResult result = TracePhotonMultiBody(origin, direction, bodies.data(), 3, config);
        ExpectTrue(result.fate == PhotonFate::CAPTURED, "A ray aimed directly at an extra body is captured, not escaped");
    }

    // 3. Disk-hit attribution: a ray aimed at an extra body's own disk
    // (not the primary's) should report hitDisk=true with diskBodyIndex
    // pointing at that body.
    {
        std::array<LensingBody, 2> bodies{};
        bodies[0] = LensingBody{glm::dvec3(0.0), 1.0, 2.0, 2.002};
        // Far enough from the primary (500 sim units) that NO trajectory
        // starting near it could ever cross z=0 within the primary's own
        // disk radius (max 26) by accident -- an earlier version of this
        // test placed the extra body only 50 units out, and the ray's real
        // (strongly deflected, not a straight fall) path happened to swing
        // past it and cross z=0 within the PRIMARY's disk range instead,
        // which was a flaw in the test's geometry, not the solver (the
        // solver's own body/index bookkeeping was already correct, as
        // confirmed by the two tests above this one).
        const glm::dvec3 extraPos(500.0, 0.0, 0.0);
        bodies[1] = LensingBody{extraPos, 1.0, 2.0, 2.002};

        MultiBodyGeodesicConfig config;
        config.escapeRadius = 1.0e4;
        config.maxSteps = 20000;
        config.checkDisk = true;
        config.diskInnerRadius[0] = 6.0;
        config.diskOuterRadius[0] = 26.0;
        config.diskInnerRadius[1] = 6.0;
        config.diskOuterRadius[1] = 26.0;

        // A steep, close approach (small x-offset relative to height) so
        // deflection doesn't have room to fling it past the body before it
        // crosses z=0.
        const glm::dvec3 origin = extraPos + glm::dvec3(10.0, 0.0, 15.0);
        const glm::dvec3 direction(0.0, 0.0, -1.0);
        const PhotonTraceResult result = TracePhotonMultiBody(origin, direction, bodies.data(), 2, config);
        ExpectTrue(result.hitDisk, "A ray aimed at an extra body's disk registers a hit");
        if (result.hitDisk) {
            ExpectTrue(result.diskBodyIndex == 1, "The hit is attributed to the extra body's index, not the primary's");
        }
    }

    // 4. Star gravity: a star placed much closer to an extra black hole
    // than to the primary should fall toward and be captured by the EXTRA
    // one, confirming Astronomy::StarBody actually routes through
    // CombinedGravityAccelSim/IsCapturedByAny rather than ignoring
    // extraBlackHoles (same bug class as the binary-mode fix from last
    // session, now covered for extras too).
    {
        std::vector<ExtraBlackHole> extras(1);
        extras[0].gmRatio = 1.0;
        extras[0].position = glm::dvec3(50.0, 0.0, 0.0);

        Astronomy::StarBodySettings settings;
        settings.particleCount = 1;
        settings.initialRadius = 0.01; // effectively a point, for a deterministic single-particle test

        // Starts just outside the extra black hole's horizon, at rest --
        // should fall in and be captured within a handful of steps, NOT
        // drift toward the (much farther away) primary at the origin.
        const glm::dvec3 startPos = extras[0].position + glm::dvec3(3.0, 0.0, 0.0);
        Astronomy::StarBody star(startPos, glm::dvec3(0.0), settings);

        bool captured = false;
        const double dt = 0.02;
        for (int step = 0; step < 2000 && !captured; ++step) {
            star.Update(dt, nullptr, &extras);
            if (star.IsFullyConsumed()) captured = true;
        }
        ExpectTrue(captured, "A star placed near an extra black hole falls in and is captured by it");
    }
}
