#pragma once

#include <cmath>
#include <random>
#include <string>
#include <vector>

// Multi-star systems for the Universe/Star Map panel (App.cpp), on top of
// StarCatalog.h's single-point destinations. Two real options (Alpha
// Centauri's inner binary, and the same system including its real, very
// distant third member Proxima Centauri -- an actual three-body system)
// plus a procedural N-body generator for anything beyond that, since
// precisely-measured real systems of 4+ comparably-separated, mutually
// interacting stars are rare and not something to present as verified
// catalog data from memory. See StarCatalog.h's own header comment for the
// same real-data-vs-procedural distinction this file follows.
namespace Universe {

struct StarMember {
    std::string name;
    double massSolar;
    double temperatureK;
};

enum class MultiStarKind { TwoBody, ThreeBody, NBody };

struct MultiStarSystemEntry {
    std::string name;
    double raHours = 0.0;
    double decDegrees = 0.0;
    double distanceLy = 0.0; // 0.0 for a procedurally-generated system -- see isRealSystem
    std::vector<StarMember> members;
    MultiStarKind kind = MultiStarKind::TwoBody;

    // true: a real catalogued system (Alpha Centauri) with real member
    // masses/temperatures/approximate separation -- see the builders
    // below. false: procedurally generated (GenerateProceduralSystem) --
    // masses are real-ish (drawn from a plausible range) but this is not a
    // specific system that exists, and has no real sky position
    // (raHours/decDegrees/distanceLy are left at 0 and travel to one
    // doesn't quote a real light-travel distance).
    bool isRealSystem = true;

    std::string notes;
};

// The real inner binary of the nearest star system to the Sun -- real
// masses, and (unlike 3+ body systems) a genuine closed-form two-body
// orbit exists, so this one is numerically well-behaved: App.cpp
// integrates it with the same real-time Newtonian N-body step every
// multi-star system uses (not a special-cased closed-form path), but two
// bodies integrated this way reduce to an ordinary (effectively Keplerian)
// bound orbit rather than anything chaotic.
inline MultiStarSystemEntry BuildAlphaCentauriBinary() {
    MultiStarSystemEntry entry;
    entry.name = "Alpha Centauri AB (binary)";
    entry.raHours = 14.6598;
    entry.decDegrees = -60.8339;
    entry.distanceLy = 4.37;
    entry.members = {{"Alpha Centauri A", 1.10, 5790.0}, {"Alpha Centauri B", 0.907, 5260.0}};
    entry.kind = MultiStarKind::TwoBody;
    entry.isRealSystem = true;
    entry.notes =
        "The real inner pair of the nearest star system to the Sun -- average real separation ~23.5 AU, real "
        "orbital period ~79.9 years (highly eccentric: 11.2-35.6 AU). Two mutually gravitating bodies DO have a "
        "closed-form orbit (the ordinary two-body/Kepler problem) -- this view still integrates it step by step "
        "like every multi-star system here, it just settles into an ordinary bound orbit rather than anything "
        "chaotic.";
    return entry;
}

// The same real system, now including its real third member: Proxima
// Centauri, bound but extraordinarily distant (current best estimates put
// it roughly 13,000 AU from the A-B pair, with an orbital period on the
// order of 500,000+ years and real, substantial uncertainty in its exact
// orbit -- among the most eccentric/least-constrained orbits of any known
// bound star). A genuine three-body system: unlike the binary above, three
// mutually gravitating bodies have NO general closed-form solution (this
// is literally "the three-body problem") -- App.cpp's real-time Newtonian
// integration is the only way to show this system's motion at all, not a
// simplification of an available exact answer.
inline MultiStarSystemEntry BuildAlphaCentauriTriple() {
    MultiStarSystemEntry entry;
    entry.name = "Alpha Centauri System (triple)";
    entry.raHours = 14.6598;
    entry.decDegrees = -60.8339;
    entry.distanceLy = 4.37;
    entry.members = {{"Alpha Centauri A", 1.10, 5790.0},
                      {"Alpha Centauri B", 0.907, 5260.0},
                      {"Proxima Centauri", 0.122, 3042.0}};
    entry.kind = MultiStarKind::ThreeBody;
    entry.isRealSystem = true;
    entry.notes =
        "The same system with its real third member, Proxima Centauri, bound but extraordinarily distant (~13,000 "
        "AU from the A-B pair, period on the order of 500,000+ years, with real substantial orbital uncertainty). "
        "A genuine three-body system -- no general closed-form solution exists for 3+ mutually gravitating "
        "bodies, so this view's real-time Newtonian integration is the only way to show it moving at all.";
    return entry;
}

// Procedurally generates an N-star system (N >= 2), masses drawn from a
// simplified plausible real range and temperatures from an approximate
// real mass-temperature power law for main-sequence stars (T ~ T_sun *
// (M/M_sun)^0.54 -- a simplified empirical fit, not a full stellar
// structure/evolution model). NOT a specific real catalogued system --
// precisely-measured real bound systems of 4+ comparably-separated stars
// are rare and not something to fabricate specific numbers for from
// memory, so this is labeled procedural throughout (isRealSystem=false,
// no real sky position) rather than dressed up as a real discovery.
inline MultiStarSystemEntry GenerateProceduralSystem(int starCount, unsigned int seed) {
    starCount = std::max(2, starCount);
    MultiStarSystemEntry entry;
    entry.name = "Procedural " + std::to_string(starCount) + "-Star System";
    entry.kind = starCount == 2 ? MultiStarKind::TwoBody : (starCount == 3 ? MultiStarKind::ThreeBody : MultiStarKind::NBody);
    entry.isRealSystem = false;
    entry.notes =
        std::to_string(starCount) +
        " mutually gravitating stars, masses drawn from a plausible real range (not a specific real system). " +
        (starCount >= 3
             ? "3+ mutually gravitating bodies have no general closed-form orbit -- this system's evolution is "
               "genuinely chaotic, the same unsolved N-body problem real astrophysics deals with, not scripted "
               "motion."
             : "Two mutually gravitating bodies reduce to an ordinary bound two-body orbit.");

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> massDist(0.15, 2.5); // solar masses -- a simplified range, not a real initial-mass-function sample
    for (int i = 0; i < starCount; ++i) {
        const double mass = massDist(rng);
        const double kelvin = 5778.0 * std::pow(mass, 0.54);
        entry.members.push_back({"Star " + std::to_string(i + 1), mass, kelvin});
    }
    return entry;
}

} // namespace Universe
