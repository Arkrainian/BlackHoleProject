#pragma once

#include "Constants.h"

namespace Physics {

// A non-rotating (Schwarzschild) black hole, described purely by its mass.
//
// All derived quantities below are given in "sim units" where the
// gravitational radius GM/c^2 = 1, i.e. r_s = 2. This class also stores the
// SI mass so the UI can display r_s in kilometers/etc via
// Constants::SimulationUnits.
class BlackHole {
public:
    explicit BlackHole(double massSolarMasses = 10.0)
        : m_massKg(massSolarMasses * Constants::SOLAR_MASS_KG) {}

    void SetMassSolarMasses(double massSolarMasses) {
        m_massKg = massSolarMasses * Constants::SOLAR_MASS_KG;
    }

    double MassKg() const { return m_massKg; }
    double MassSolarMasses() const { return m_massKg / Constants::SOLAR_MASS_KG; }

    // Schwarzschild radius, r_s = 2GM/c^2. In sim units this is always 2.0;
    // exposed as a function (not a constant) so the physical/documentation
    // intent stays explicit at call sites.
    static constexpr double SchwarzschildRadiusSim() { return 2.0; }

    // Photon sphere: the unstable circular photon orbit at r = 3GM/c^2 = 1.5*r_s.
    static constexpr double PhotonSphereRadiusSim() { return 3.0; }

    // Critical impact parameter b_crit = 3*sqrt(3) * GM/c^2 = (3*sqrt(3)/2) r_s.
    // Rays with |b| < b_crit are captured; |b| > b_crit escape (in the limit
    // of a ray starting far away and aimed at the hole, ignoring the
    // orbiting/unresolved boundary case exactly at b_crit).
    static double CriticalImpactParameterSim() {
        return 3.0 * 1.7320508075688772; // 3*sqrt(3)
    }

    double SchwarzschildRadiusMeters() const {
        return SchwarzschildRadiusSim() * Constants::GravitationalRadiusMeters(m_massKg);
    }

private:
    double m_massKg;
};

} // namespace Physics
