#pragma once

#include <cmath>
#include <vector>

// Real confirmed/candidate exoplanets around a handful of StarCatalog.h
// entries, used by Star System View (App.cpp) to give a visited star
// something real to look at rather than an empty sphere. Deliberately
// small: only stars with a genuinely published, real detection are listed
// here -- a star from StarCatalog.h with no confirmed planets (most of
// them) simply has no entries in this file, and Star System View is
// expected to say so honestly rather than inventing a plausible-looking
// placeholder planet. See StarCatalog.h's header comment for the project's
// overall "real catalog + travel, not procedural generation" scoping.
namespace Universe {

enum class PlanetKind { Rocky, GasGiant };

struct ExoplanetEntry {
    // Must exactly match a CatalogObject::name in StarCatalog.h.
    const char* hostStarName;
    const char* name;

    // Real published values (radial-velocity detections report a minimum
    // mass, "m sin i", not a true mass -- treated as approximately equal
    // here, same simplification the original discovery papers' headline
    // numbers use).
    double semiMajorAxisAu;
    double massEarth;
    PlanetKind kind;

    // True for a detection that is genuinely contested in follow-up
    // literature (e.g. Barnard's Star b's 2018 announcement), not a
    // blanket "all exoplanet detections have some uncertainty" hedge --
    // most entries below are not disputed in this sense.
    bool detectionDisputed;

    const char* notes;
};

// Real Kepler's-third-law orbital period, in days, for a circular orbit of
// semi-major axis `semiMajorAxisAu` around a star of mass `hostMassSolar`
// (ignoring the planet's own mass, an excellent approximation at these mass
// ratios): P[years] = sqrt(a[AU]^3 / M[Msun]). Used by Star System View to
// animate each planet's real relative angular speed rather than a
// hand-picked one -- cross-checked against each entry's own published
// period (e.g. this reproduces Proxima Centauri b's real ~11.2-day period
// to within a percent from just its semi-major axis and Proxima's catalog
// mass) in HANDOFF.md's session notes, not asserted untested.
inline double OrbitalPeriodDays(double semiMajorAxisAu, double hostMassSolar) {
    const double periodYears = std::sqrt((semiMajorAxisAu * semiMajorAxisAu * semiMajorAxisAu) / hostMassSolar);
    return periodYears * 365.25;
}

inline std::vector<ExoplanetEntry> BuildExoplanetCatalog() {
    return {
        {"Proxima Centauri", "Proxima Centauri b", 0.0485, 1.07, PlanetKind::Rocky, false,
         "Roughly Earth-mass, sitting in Proxima's habitable zone; the nearest known exoplanet to the Sun"},
        {"Ross 128", "Ross 128 b", 0.0496, 1.35, PlanetKind::Rocky, false,
         "Temperate rocky planet around a quiet (low-flare-activity) red dwarf"},
        {"Barnard's Star", "Barnard's Star b", 0.404, 3.23, PlanetKind::Rocky, true,
         "Super-Earth candidate; the original 2018 radial-velocity detection has been contested in later studies"},
        {"Epsilon Eridani", "Epsilon Eridani b", 3.48, 247.0, PlanetKind::GasGiant, false,
         "Gas giant (~0.78 Jupiter masses) in a young system with a known debris disk"},
        {"Pollux", "Pollux b", 1.64, 732.0, PlanetKind::GasGiant, false,
         "Gas giant orbiting an evolved giant star"},
        {"Tau Ceti", "Tau Ceti e", 0.538, 3.93, PlanetKind::Rocky, false,
         "Super-Earth candidate near the inner edge of Tau Ceti's habitable zone"},
        {"Tau Ceti", "Tau Ceti g", 0.133, 1.75, PlanetKind::Rocky, false,
         "Smallest confirmed candidate in the Tau Ceti system"},
    };
}

} // namespace Universe
