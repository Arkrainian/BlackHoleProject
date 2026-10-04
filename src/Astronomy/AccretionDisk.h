#pragma once

#include <glm/glm.hpp>

#include "Physics/Constants.h"

namespace Astronomy {

// A geometrically thin, optically thick equatorial accretion disk, modeled
// after the classic Shakura & Sunyaev (1973) alpha-disk radial temperature
// profile. This is NOT a full radiative-transfer or MHD simulation (Stage 6
// explicitly does not attempt that -- see docs/physics.md limitations); it
// gives each disk radius a physically-motivated relative temperature and
// color, which GeodesicSolver's disk-intersection logic then modulates by
// the exact gravitational + Doppler redshift at the photon's crossing point.
struct AccretionDiskSettings {
    // Physically, stable circular orbits only exist for r >= ISCO = 3*r_s
    // (6 GM/c^2) in Schwarzschild spacetime; the UI clamps the slider there
    // by default, though the underlying velocity formula stays valid (for
    // exploration) down to the photon sphere at 1.5*r_s.
    double innerRadius = 6.0; // sim units; default = ISCO = 3*r_s
    double outerRadius = 26.0;
    double temperatureScale = 1.0; // visual tuning multiplier, not a physical Kelvin scale
    double brightnessScale = 1.0;
    bool clockwise = true; // rotation sense as seen from +z (purely a visualization choice)
    bool relativisticEffects = true; // toggle combined redshift+beaming vs. a flat "static" disk, for comparison

    // Together with the black hole's actual mass (see AccretionDisk::
    // SetBlackHoleMassKg), these two pick the REAL Kelvin scale of the disk
    // via the Shakura-Sunyaev effective temperature, rather than an
    // artist-picked color ramp -- see AccretionDisk.cpp's PeakKelvin(). This
    // is what makes a supermassive hole like TON 618 (M ~ 6.6e10 Msun) show
    // a cooler, UV/blue-white "big blue bump" quasar disk (tens of
    // thousands of K) instead of the X-ray-hot (millions of K. visually
    // identical to the quasar case once run through the same clamped
    // blackbody ramp) disk a stellar-mass hole would have at the same
    // dimensionless radius -- T_eff scales as M^-1/4 for a fixed Eddington
    // fraction.
    double eddingtonFraction = 0.3;   // L / L_Edd; ~0.3 is typical for a luminous quasar like TON 618
    double radiativeEfficiency = 0.1; // eta = L / (Mdot c^2); standard thin-disk value
};

class AccretionDisk {
public:
    explicit AccretionDisk(const AccretionDiskSettings& settings = {},
                            double blackHoleMassKg = 10.0 * Physics::Constants::SOLAR_MASS_KG)
        : m_settings(settings), m_blackHoleMassKg(blackHoleMassKg) {
        RecomputePeakKelvin();
    }

    void SetSettings(const AccretionDiskSettings& settings) {
        m_settings = settings;
        RecomputePeakKelvin();
    }
    void SetBlackHoleMassKg(double massKg) {
        m_blackHoleMassKg = massKg;
        RecomputePeakKelvin();
    }
    const AccretionDiskSettings& Settings() const { return m_settings; }

    // The real Kelvin scale at the disk's inner edge (see RecomputePeakKelvin
    // in the .cpp) -- exposed so the UI can show the actual physical
    // temperature this mass/Eddington-fraction/efficiency combination
    // implies, not just the dimensionless shape.
    double PeakKelvin() const { return m_peakKelvin; }

    // Shakura-Sunyaev radial temperature shape (arbitrary relative units,
    // not literal Kelvin -- the profile's SHAPE is the physically-cited
    // part): T(r) ~ (r_in/r)^(3/4) * sqrt(1 - sqrt(r_in/r)), zero outside
    // [innerRadius, outerRadius]. Peaks a little outside the inner edge and
    // falls off as r^(-3/4) further out, matching the standard thin-disk
    // result (e.g. Shakura & Sunyaev 1973; Frank, King & Raine "Accretion
    // Power in Astrophysics" ch.5).
    double Temperature(double r) const;

    // Maps a dimensionless temperature (as returned by Temperature(), after
    // any redshift has already been applied by the caller) to a color.
    // Brightness scales as temperature^4 (Stefan-Boltzmann), which is what
    // makes gravitational+Doppler redshift correctly produce relativistic
    // beaming here too: redshifting T also dims/brightens the T^4 term,
    // with no separate "beaming" formula needed on top. The HUE, separately,
    // comes from multiplying this same dimensionless value by the real
    // Kelvin scale (PeakKelvin(), recomputed from the black hole's actual
    // mass) -- see AccretionDisk.cpp.
    glm::vec3 EmittedColor(double temperature) const;

private:
    void RecomputePeakKelvin();

    AccretionDiskSettings m_settings;
    double m_blackHoleMassKg;
    double m_peakKelvin = 10000.0; // recomputed by RecomputePeakKelvin(); never used before that runs
};

} // namespace Astronomy
