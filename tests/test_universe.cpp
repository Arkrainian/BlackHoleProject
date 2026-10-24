#include "TestFramework.h"

#include <algorithm>
#include <string>

#include <glm/glm.hpp>

#include "Universe/ExoplanetCatalog.h"
#include "Universe/StarCatalog.h"

using namespace TestFramework;
using namespace Universe;

// Validates StarCatalog.h's real-data catalog and the RA/Dec/distance ->
// Cartesian conversion CartesianLy() relies on, independent of the rest of
// the app's rendering/UI code.
void RunUniverseTests() {
    const std::vector<CatalogObject> catalog = BuildCatalog();

    ExpectTrue(!catalog.empty(), "Catalog is non-empty");

    // A star directly on the celestial equator at RA=0h should land exactly
    // on the +X axis at its catalog distance, with no Y component -- the
    // simplest possible check of CartesianLy()'s formula, independent of
    // any specific catalog entry.
    {
        CatalogObject onEquator{"test", ObjectType::Star, 0.0, 0.0, 10.0, 1.0, 5000.0, kUnknownSpin, ""};
        const glm::dvec3 pos = onEquator.CartesianLy();
        ExpectNear(pos.x, 10.0, 1e-9, "RA=0h, Dec=0 deg lands on +X at the catalog distance");
        ExpectNear(pos.y, 0.0, 1e-9, "RA=0h, Dec=0 deg has zero Y");
        ExpectNear(pos.z, 0.0, 1e-9, "RA=0h, Dec=0 deg has zero Z");
    }

    // A star at the north celestial pole (Dec=+90) should land on the
    // rotation axis (here Z) regardless of RA, at exactly the catalog
    // distance -- Polaris is close to this by design (it's the pole star),
    // used here as a real-entry sanity check rather than a synthetic one.
    {
        bool found = false;
        for (const CatalogObject& obj : catalog) {
            if (std::string(obj.name) == "Polaris") {
                found = true;
                const glm::dvec3 pos = obj.CartesianLy();
                ExpectNear(glm::length(pos), obj.distanceLy, 1e-6, "Polaris's Cartesian position has the catalog distance as its magnitude");
                ExpectTrue(pos.z > 0.9 * obj.distanceLy, "Polaris (Dec ~89.26 deg) sits almost entirely along the polar (Z) axis");
            }
        }
        ExpectTrue(found, "Catalog contains Polaris");
    }

    // Every catalog entry's Cartesian magnitude must reproduce its own
    // listed distance (CartesianLy() is a pure spherical->Cartesian
    // conversion, so this must hold exactly for every entry, not just the
    // two spot-checked above).
    {
        bool allMatch = true;
        for (const CatalogObject& obj : catalog) {
            const double magnitude = glm::length(obj.CartesianLy());
            if (std::abs(magnitude - obj.distanceLy) > 1e-6 * std::max(1.0, obj.distanceLy)) {
                allMatch = false;
                break;
            }
        }
        ExpectTrue(allMatch, "Every catalog entry's Cartesian position magnitude matches its listed distance");
    }

    // Real-world sanity check: Alpha Centauri (the nearest star system to
    // the Sun other than the Sun itself) must be closer than Sirius (the
    // next nearest bright star used in this catalog) -- catches a
    // transposed/garbled distance field without needing to hardcode every
    // digit of either value.
    {
        double alphaCenDist = -1.0, siriusDist = -1.0;
        for (const CatalogObject& obj : catalog) {
            if (std::string(obj.name) == "Alpha Centauri A") alphaCenDist = obj.distanceLy;
            if (std::string(obj.name) == "Sirius A") siriusDist = obj.distanceLy;
        }
        ExpectTrue(alphaCenDist > 0.0 && siriusDist > 0.0, "Catalog contains both Alpha Centauri A and Sirius A");
        ExpectTrue(alphaCenDist < siriusDist, "Alpha Centauri is closer to Sol than Sirius, as in reality");
    }

    // Black hole mass sanity: Sagittarius A* (the Milky Way's own central
    // black hole, ~4.3e6 solar masses) must be far less massive than TON
    // 618 (~6.6e10 solar masses, one of the most massive black holes known)
    // -- these are both real, independently cross-checkable figures.
    {
        double sgrAMass = -1.0, ton618Mass = -1.0;
        for (const CatalogObject& obj : catalog) {
            if (std::string(obj.name) == "Sagittarius A*") sgrAMass = obj.massSolar;
            if (std::string(obj.name) == "TON 618") ton618Mass = obj.massSolar;
        }
        ExpectTrue(sgrAMass > 0.0 && ton618Mass > 0.0, "Catalog contains both Sagittarius A* and TON 618");
        ExpectTrue(sgrAMass < ton618Mass, "Sagittarius A* is far less massive than TON 618, as in reality");
    }

    // Only Star and BlackHoleObj entries are travel targets (App.cpp);
    // Galaxy entries exist for distance context only -- confirm at least
    // one of each type is present so that distinction is actually exercised
    // somewhere in the catalog, not an empty category.
    {
        bool hasStar = false, hasBlackHole = false, hasGalaxy = false;
        for (const CatalogObject& obj : catalog) {
            hasStar |= obj.type == ObjectType::Star;
            hasBlackHole |= obj.type == ObjectType::BlackHoleObj;
            hasGalaxy |= obj.type == ObjectType::Galaxy;
        }
        ExpectTrue(hasStar && hasBlackHole && hasGalaxy, "Catalog contains all three object types (Star, BlackHoleObj, Galaxy)");
    }

    // OrbitalPeriodDays (Kepler's third law) must reproduce each listed
    // exoplanet's real published orbital period from just its semi-major
    // axis and its host star's catalog mass -- a cross-check that the
    // catalog's star masses and planet semi-major axes are mutually
    // consistent with real physics, not independently-sourced numbers that
    // happen to look plausible next to each other.
    {
        const std::vector<ExoplanetEntry> exoplanets = BuildExoplanetCatalog();
        ExpectTrue(!exoplanets.empty(), "Exoplanet catalog is non-empty");

        auto FindStarMass = [&](const char* starName) -> double {
            for (const CatalogObject& obj : catalog) {
                if (std::string(obj.name) == starName) return obj.massSolar;
            }
            return -1.0;
        };

        // {host star, planet, real published period in days}
        struct Known { const char* host; const char* planet; double realPeriodDays; };
        const Known knownPeriods[] = {
            {"Proxima Centauri", "Proxima Centauri b", 11.19},
            {"Ross 128", "Ross 128 b", 9.9},
            {"Epsilon Eridani", "Epsilon Eridani b", 2691.0}, // ~7.37 years
            {"Tau Ceti", "Tau Ceti e", 162.9},
            {"Tau Ceti", "Tau Ceti g", 20.0},
        };

        for (const Known& known : knownPeriods) {
            const double hostMass = FindStarMass(known.host);
            ExpectTrue(hostMass > 0.0, std::string("Host star found in catalog: ") + known.host);

            double semiMajorAxisAu = -1.0;
            for (const ExoplanetEntry& planet : exoplanets) {
                if (std::string(planet.name) == known.planet) semiMajorAxisAu = planet.semiMajorAxisAu;
            }
            ExpectTrue(semiMajorAxisAu > 0.0, std::string("Planet found in exoplanet catalog: ") + known.planet);

            const double computedPeriodDays = OrbitalPeriodDays(semiMajorAxisAu, hostMass);
            // Loose (10%) tolerance: the catalog's star masses are
            // themselves rounded published figures, not the exact masses
            // used in each planet's own discovery paper.
            ExpectNear(computedPeriodDays, known.realPeriodDays, known.realPeriodDays * 0.10,
                       std::string(known.planet) + "'s Kepler-derived period matches its real published period");
        }
    }
}
