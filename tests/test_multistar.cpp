#include "TestFramework.h"

#include <cmath>

#include "Physics/Constants.h"
#include "Physics/GravitySource.h"
#include "Universe/MultiStarSystem.h"

using namespace TestFramework;
using namespace Universe;

// Validates MultiStarSystem.h's real-data builders and
// GenerateProceduralSystem, plus a sanity check on the tuned mutual-gravity
// law (Physics/GravitySource.h) App.cpp's N-body integration reuses for
// multi-star systems -- independent of any App.cpp/rendering code.
void RunMultiStarTests() {
    // Alpha Centauri AB: real masses, two-body.
    {
        const MultiStarSystemEntry ab = BuildAlphaCentauriBinary();
        ExpectTrue(ab.members.size() == 2, "Alpha Centauri AB has exactly 2 members");
        ExpectTrue(ab.kind == MultiStarKind::TwoBody, "Alpha Centauri AB is classified as TwoBody");
        ExpectTrue(ab.isRealSystem, "Alpha Centauri AB is flagged as a real system");
        ExpectTrue(ab.members[0].massSolar > ab.members[1].massSolar,
                   "Alpha Centauri A is listed as more massive than B, as in reality");
    }

    // Alpha Centauri System: the same pair plus the real third member.
    {
        const MultiStarSystemEntry triple = BuildAlphaCentauriTriple();
        ExpectTrue(triple.members.size() == 3, "Alpha Centauri System (triple) has exactly 3 members");
        ExpectTrue(triple.kind == MultiStarKind::ThreeBody, "Alpha Centauri System (triple) is classified as ThreeBody");
        ExpectTrue(triple.isRealSystem, "Alpha Centauri System (triple) is flagged as a real system");
        bool hasProxima = false;
        for (const StarMember& member : triple.members) {
            if (member.name == "Proxima Centauri") hasProxima = true;
        }
        ExpectTrue(hasProxima, "Alpha Centauri System (triple) includes Proxima Centauri");
    }

    // GenerateProceduralSystem: correct member count, kind classification,
    // flagged as non-real, and a deterministic seed reproduces the same
    // system (so repeated UI clicks with the same seed wouldn't silently
    // vary -- this app always increments its seed counter per click, but
    // the generator ITSELF must be deterministic for that guarantee to
    // mean anything).
    {
        const MultiStarSystemEntry two = GenerateProceduralSystem(2, 42);
        ExpectTrue(two.members.size() == 2, "Procedural 2-star system has exactly 2 members");
        ExpectTrue(two.kind == MultiStarKind::TwoBody, "Procedural 2-star system is classified as TwoBody");
        ExpectTrue(!two.isRealSystem, "Procedural system is flagged as NOT a real system");

        const MultiStarSystemEntry three = GenerateProceduralSystem(3, 42);
        ExpectTrue(three.kind == MultiStarKind::ThreeBody, "Procedural 3-star system is classified as ThreeBody");

        const MultiStarSystemEntry six = GenerateProceduralSystem(6, 42);
        ExpectTrue(six.members.size() == 6, "Procedural 6-star system has exactly 6 members");
        ExpectTrue(six.kind == MultiStarKind::NBody, "Procedural 6-star system (N>=4) is classified as NBody");

        const MultiStarSystemEntry repeat = GenerateProceduralSystem(6, 42);
        bool allMatch = true;
        for (size_t i = 0; i < six.members.size(); ++i) {
            if (std::abs(six.members[i].massSolar - repeat.members[i].massSolar) > 1e-12) allMatch = false;
        }
        ExpectTrue(allMatch, "The same seed reproduces the same procedural system's masses exactly");

        // Every generated mass should imply a real-ish positive Kelvin
        // temperature via the mass-temperature power law -- catches a
        // sign/domain error in std::pow for an edge-case mass without
        // needing to hardcode an expected temperature.
        bool allTemperaturesSane = true;
        for (const StarMember& member : six.members) {
            if (!(member.temperatureK > 1000.0 && member.temperatureK < 50000.0)) allTemperaturesSane = false;
        }
        ExpectTrue(allTemperaturesSane, "Every procedurally generated star's temperature is in a plausible real range (1000-50000 K)");
    }

    // The tuned mutual-gravity law App.cpp's N-body integration reuses:
    // two 1-solar-mass stars should pull toward each other (sanity check
    // on direction, independent of App.cpp's own integration loop).
    {
        const glm::dvec3 posA(10.0, 0.0, 0.0);
        const glm::dvec3 posB(-10.0, 0.0, 0.0);
        std::vector<Physics::GravitySource> sourcesForA = {{posB, Physics::Constants::SOLAR_MASS_KG}};
        const glm::dvec3 accelOnA = Physics::MutualGravityAccelSim(posA, sourcesForA, 1.0);
        ExpectTrue(accelOnA.x < 0.0, "Mutual gravity pulls star A toward star B (negative X direction)");
        ExpectNear(accelOnA.y, 0.0, 1e-12, "Mutual gravity between two coplanar stars has no Y component");
    }
}
