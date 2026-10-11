#pragma once

namespace Astronomy {

// A simplified "tidal disruption flare": captured planetary debris
// temporarily brightens/heats the accretion disk, decaying back to
// baseline over time. This stands in for the real fallback-accretion
// light curve a tidal disruption event actually produces -- debris that
// doesn't escape rains back down onto the black hole over an extended
// period, following (in the classic Rees 1988 picture) a roughly t^-5/3
// decline -- NOT reproduced here. This class instead accumulates captured
// mass into a single scalar and decays it with a fixed exponential time
// constant, which is simpler, bounded, and good enough to make captured
// debris visibly (if approximately) affect the disk, without claiming to
// model real fallback-accretion physics (spec 28.13 explicitly allows
// this: "if a simplified luminosity curve is used, identify its
// assumptions" -- this comment is that identification).
class TidalDisruptionFlare {
public:
    // Call once per live planet per frame with that planet's
    // PlanetBody::MassCapturedLastStep() (zero most frames -- cheap to
    // call unconditionally). Adds to the decaying accumulator; does not
    // itself advance time (see Update).
    void RegisterCapturedMass(double massKg);

    // Advances the exponential decay by deltaTime (seconds, sim-unit
    // time -- same convention as PlanetBody::Update's deltaTime). Call
    // once per frame, after all of that frame's RegisterCapturedMass calls.
    void Update(double deltaTime);

    // Additional AccretionDiskSettings::eddingtonFraction to add to
    // whatever the user's own slider is set to -- zero when nothing has
    // been captured recently. Clamped (see TidalDisruptionFlare.cpp) so a
    // single huge capture event can't send the disk's settings into a
    // nonsensical/overflowing visual range.
    double EddingtonFractionBoost() const;

    // True when EddingtonFractionBoost() is above a negligible threshold
    // -- for a UI readout ("flare active" vs. "no recent captures").
    bool IsActive() const;

private:
    double m_accumulatorKg = 0.0;
};

} // namespace Astronomy
