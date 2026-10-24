#pragma once

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

// A small, real-data catalog of stars, black holes, and galaxies, used by
// the "Universe / Star Map" panel (App.cpp) to let the app's single
// black-hole-centered scene be re-pointed at a different real astronomical
// object ("travel") instead of only ever showing one hand-picked default
// (TON 618).
//
// Scope, deliberately limited (see HANDOFF.md's "Universe / Star Map"
// session for the full reasoning):
//   - Positions/distances/masses/temperatures below are real published
//     values (standard catalog/textbook figures), not procedurally
//     generated -- this is "real catalog + travel", not a procedural
//     universe generator.
//   - "Travel" is an instantaneous narrative jump, not simulated
//     faster-than-light flight: the app has no mechanism for continuously
//     flying across light-year distances (the jet/free-fly camera operates
//     in black-hole-relative sim units, see Physics/Constants.h), so
//     arriving re-points the existing scene at the target instead. The real
//     light-travel time implied by the jump distance is always shown
//     alongside it so this is never silently misrepresented as FTL travel
//     actually happening.
//   - Only Star and BlackHoleObj entries are "visitable" (see App.cpp's
//     TravelToUniverseIndex) -- a Galaxy here is a single point too far
//     away and too extended to meaningfully render as a destination with
//     this app's single-point-source renderer; those entries exist for
//     real-scale distance context only (how far Andromeda actually is,
//     etc.), not as travel targets.
namespace Universe {

enum class ObjectType { Star, BlackHoleObj, Galaxy };

// Sentinel for CatalogObject::spin meaning "not reliably measured" --
// distinct from a real spin of 0 (a genuinely non-rotating/Schwarzschild
// hole), which the near-extremal black holes below are not.
inline constexpr double kUnknownSpin = -2.0;

struct CatalogObject {
    const char* name;
    ObjectType type;

    // Equatorial coordinates (J2000-ish, standard catalog values) + distance,
    // used only to place this object in a shared heliocentric Cartesian
    // frame via CartesianLy() below -- not used for anything else.
    double raHours;
    double decDegrees;
    double distanceLy;

    // Star: the star's own mass. BlackHoleObj: the hole's mass (fed straight
    // into Physics::BlackHole::SetMassSolarMasses on arrival). Galaxy: 0.0,
    // not meaningfully a point mass at this scale.
    double massSolar;

    // Star: approximate effective surface temperature (Kelvin), used with
    // Astronomy::ApproximateBlackbodyHue for its rendered color. Not
    // applicable to BlackHoleObj/Galaxy (0.0).
    double temperatureK;

    // BlackHoleObj only: dimensionless Kerr a* where independently measured/
    // estimated in the literature; kUnknownSpin otherwise (left at whatever
    // spin the UI already had rather than asserting a specific, unverified
    // number).
    double spin;

    // Star: spectral classification (e.g. "G2V"). BlackHoleObj/Galaxy: a
    // short descriptive note. Purely informational, shown in the Star Map
    // panel.
    const char* notes;

    // This object's position in a shared heliocentric equatorial Cartesian
    // frame, in light-years -- NOT simulation units (see Physics/
    // Constants.h; sim units are relative to whichever black hole is
    // currently loaded and have no meaning at interstellar scale). Used only
    // to compute real distances between catalog entries (including from
    // "Sol", the implicit origin/home entry every session starts at).
    glm::dvec3 CartesianLy() const {
        constexpr double kPi = 3.14159265358979323846;
        const double raRadians = raHours * (kPi / 12.0);
        const double decRadians = decDegrees * (kPi / 180.0);
        const double cosDec = std::cos(decRadians);
        return glm::dvec3(
            distanceLy * cosDec * std::cos(raRadians),
            distanceLy * cosDec * std::sin(raRadians),
            distanceLy * std::sin(decRadians));
    }
};

// Real stars, black holes, and galaxies, roughly ordered by distance within
// each type. Figures are standard published values (Hipparcos-era distances
// for the nearby stars, widely-cited literature masses for the black holes)
// -- precise to the 2-3 significant figures that matter for "how far/how
// massive is this, roughly", not research-grade astrometry. A few notes on
// specific entries:
//   - Sirius, Procyon, Pollux, Capella are given as their primary
//     component's figures (each is actually a multiple-star system).
//   - Betelgeuse/Rigel/Deneb/Antares distances carry real, large published
//     uncertainties (these are supergiants too far for reliable parallax);
//     the values here are the commonly-cited central estimates.
//   - Sagittarius A*'s spin is genuinely not well constrained observationally
//     (unlike M87*'s and Cygnus X-1's, both from direct EHT/X-ray
//     measurements) -- left as kUnknownSpin rather than guessing.
inline std::vector<CatalogObject> BuildCatalog() {
    using T = ObjectType;
    return {
        // --- Nearby stars (within ~15 ly) ---
        {"Alpha Centauri A", T::Star, 14.6598, -60.8339, 4.37, 1.10, 5790.0, kUnknownSpin, "G2V"},
        {"Alpha Centauri B", T::Star, 14.6599, -60.8392, 4.37, 0.91, 5260.0, kUnknownSpin, "K1V"},
        {"Proxima Centauri", T::Star, 14.4954, -62.6795, 4.25, 0.12, 3042.0, kUnknownSpin, "M5.5Ve -- nearest known star to the Sun"},
        {"Barnard's Star", T::Star, 17.9566, 4.6933, 5.96, 0.14, 3134.0, kUnknownSpin, "M4V"},
        {"Wolf 359", T::Star, 10.7268, 7.0153, 7.86, 0.09, 2800.0, kUnknownSpin, "M6V"},
        {"Sirius A", T::Star, 6.7525, -16.7161, 8.60, 2.02, 9940.0, kUnknownSpin, "A1V -- brightest star in Earth's night sky"},
        {"Luyten 726-8 (UV Ceti)", T::Star, 1.5890, -17.9500, 8.73, 0.10, 2670.0, kUnknownSpin, "M5.5V"},
        {"Ross 154", T::Star, 18.8578, -23.8402, 9.68, 0.17, 3340.0, kUnknownSpin, "M3.5V"},
        {"Epsilon Eridani", T::Star, 3.5486, -9.4582, 10.50, 0.82, 5084.0, kUnknownSpin, "K2V"},
        {"Lacaille 9352", T::Star, 23.1057, -35.8531, 10.74, 0.49, 3626.0, kUnknownSpin, "M0.5V"},
        {"Ross 128", T::Star, 11.7975, 0.7986, 11.03, 0.17, 3192.0, kUnknownSpin, "M4V"},
        {"Procyon A", T::Star, 7.6550, 5.2250, 11.46, 1.50, 6530.0, kUnknownSpin, "F5IV-V"},
        {"61 Cygni A", T::Star, 21.0667, 38.7497, 11.40, 0.70, 4526.0, kUnknownSpin, "K5V"},
        {"Epsilon Indi", T::Star, 22.0866, -56.7868, 11.87, 0.75, 4649.0, kUnknownSpin, "K5V"},
        {"Tau Ceti", T::Star, 1.7443, -15.9375, 11.90, 0.78, 5344.0, kUnknownSpin, "G8.5V"},

        // --- Bright/well-known naked-eye stars ---
        {"Altair", T::Star, 19.8464, 8.8683, 16.73, 1.79, 7760.0, kUnknownSpin, "A7V"},
        {"Vega", T::Star, 18.6156, 38.7837, 25.04, 2.14, 9602.0, kUnknownSpin, "A0V"},
        {"Fomalhaut", T::Star, 22.9608, -29.6222, 25.13, 1.92, 8590.0, kUnknownSpin, "A3V"},
        {"Pollux", T::Star, 7.7553, 28.0262, 33.78, 1.91, 4666.0, kUnknownSpin, "K0III"},
        {"Arcturus", T::Star, 14.2611, 19.1824, 36.70, 1.08, 4286.0, kUnknownSpin, "K1.5III"},
        {"Capella", T::Star, 5.2782, 45.9980, 42.90, 2.57, 4970.0, kUnknownSpin, "G3III"},
        {"Aldebaran", T::Star, 4.5987, 16.5093, 65.30, 1.16, 3910.0, kUnknownSpin, "K5III"},
        {"Polaris", T::Star, 2.5303, 89.2641, 433.0, 5.40, 6015.0, kUnknownSpin, "F7Ib -- the North Star"},
        {"Regulus", T::Star, 10.1395, 11.9672, 79.3, 3.80, 12460.0, kUnknownSpin, "B8IVn"},

        // --- Supergiants ---
        {"Betelgeuse", T::Star, 5.9195, 7.4071, 548.0, 16.5, 3600.0, kUnknownSpin, "M1-M2Ia -- red supergiant, will one day go supernova"},
        {"Rigel", T::Star, 5.2423, -8.2016, 860.0, 21.0, 12100.0, kUnknownSpin, "B8Ia -- blue supergiant"},
        {"Antares", T::Star, 16.4901, -26.4320, 550.0, 12.0, 3660.0, kUnknownSpin, "M1.5Iab -- red supergiant"},
        {"Deneb", T::Star, 20.6905, 45.2803, 2615.0, 19.0, 8515.0, kUnknownSpin, "A2Ia -- blue-white supergiant"},

        // --- Black holes ---
        {"Cygnus X-1", T::BlackHoleObj, 19.9733, 35.2016, 7200.0, 21.2, 0.0, 0.998,
         "Stellar-mass black hole in an X-ray binary -- one of the first strong black hole candidates identified; spin near-extremal, well measured via X-ray continuum/reflection fitting"},
        {"V404 Cygni", T::BlackHoleObj, 20.3581, 33.8675, 7800.0, 9.0, 0.0, kUnknownSpin,
         "Stellar-mass black hole, X-ray transient"},
        {"Sagittarius A*", T::BlackHoleObj, 17.7611, -29.0078, 26700.0, 4.3e6, 0.0, kUnknownSpin,
         "The Milky Way's own central supermassive black hole; imaged by the Event Horizon Telescope (2022)"},
        {"M87*", T::BlackHoleObj, 12.5137, 12.3911, 5.349e7, 6.5e9, 0.0, 0.9,
         "First black hole ever directly imaged (Event Horizon Telescope, 2019), at the center of the galaxy M87"},
        {"TON 618", T::BlackHoleObj, 12.0538, 29.8117, 1.04e10, 6.6e10, 0.0, kUnknownSpin,
         "One of the most massive black holes known -- an ultramassive quasar engine; this app's original default scene"},

        // --- Galaxies (distance context only -- not travel targets, see
        //     header comment above) ---
        {"Andromeda Galaxy (M31)", T::Galaxy, 0.7122, 41.2690, 2.537e6, 0.0, 0.0, kUnknownSpin,
         "Nearest large spiral galaxy to the Milky Way; on a collision course with it in ~4-5 billion years"},
        {"Triangulum Galaxy (M33)", T::Galaxy, 1.5642, 30.6600, 2.73e6, 0.0, 0.0, kUnknownSpin,
         "Third-largest galaxy in the Local Group"},
        {"Large Magellanic Cloud", T::Galaxy, 5.4000, -69.0, 1.62e5, 0.0, 0.0, kUnknownSpin,
         "Milky Way satellite dwarf galaxy, visible to the naked eye from the Southern Hemisphere"},
        {"Small Magellanic Cloud", T::Galaxy, 0.8667, -72.8, 2.00e5, 0.0, 0.0, kUnknownSpin,
         "Milky Way satellite dwarf galaxy"},
        {"Whirlpool Galaxy (M51)", T::Galaxy, 13.4979, 47.1952, 2.3e7, 0.0, 0.0, kUnknownSpin,
         "Classic interacting spiral galaxy pair"},
        {"Centaurus A", T::Galaxy, 13.4251, -43.0192, 1.37e7, 0.0, 0.0, kUnknownSpin,
         "Nearby active galaxy with a prominent relativistic jet"},
    };
}

} // namespace Universe
