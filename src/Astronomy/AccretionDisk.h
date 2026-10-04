#pragma once

#include <glm/glm.hpp>

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
};

class AccretionDisk {
public:
    explicit AccretionDisk(const AccretionDiskSettings& settings = {}) : m_settings(settings) {}

    void SetSettings(const AccretionDiskSettings& settings) { m_settings = settings; }
    const AccretionDiskSettings& Settings() const { return m_settings; }

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
    // with no separate "beaming" formula needed on top.
    glm::vec3 EmittedColor(double temperature) const;

private:
    AccretionDiskSettings m_settings;
};

} // namespace Astronomy
