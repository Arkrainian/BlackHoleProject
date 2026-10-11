#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

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

    // Dimensionless Kerr spin parameter a* = Jc/(GM^2), in [-1, 1]. Zero
    // (the default) is the original Schwarzschild hole this class always
    // described; nonzero routes ray tracing through KerrGeodesicSolver
    // instead (see CPUTracer.cpp). Clamped to +-0.998 -- the standard
    // "Thorne limit" for a realistic accreting hole, and also where
    // KerrMetric.h's formulas stay numerically well-behaved (the inner and
    // outer horizons coincide exactly at |a*|=1).
    void SetSpin(double spinParameter) { m_spin = std::clamp(spinParameter, -0.998, 0.998); }
    double Spin() const { return m_spin; }
    bool IsRotating() const { return m_spin != 0.0; }

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
    double m_spin = 0.0;
};

// The exact proper acceleration a "static" observer must apply to hover at
// a fixed Schwarzschild radial coordinate r (sim units, GM=1): a(r) =
// (GM/r^2) / sqrt(1 - r_s/r). Shared by anything that needs real point-mass
// gravity toward the hole -- originally written for Jet::FlightPhysics (see
// its header for the physical justification: it reduces to ordinary
// Newtonian 1/r^2 far away but diverges at the horizon, correctly modeling
// that nothing can hover arbitrarily close to it) and reused as-is by
// Astronomy::StarBody so a star's particles free-fall under the SAME exact
// law the jet does -- tidal "spaghettification" then isn't a separate
// effect to simulate, it's just what emerges when particles starting a
// little apart each follow this exact, radius-dependent pull independently.
// Clamped to a finite ceiling so one oversized timestep at the horizon
// can't inject inf/NaN before the caller's own horizon-crossing check has a
// chance to react.
inline glm::dvec3 StaticObserverGravityAccelSim(const glm::dvec3& position) {
    const double r = glm::length(position);
    if (r < 1e-6) {
        return glm::dvec3(0.0);
    }
    const double rs = BlackHole::SchwarzschildRadiusSim();
    const double newtonTerm = 1.0 / (r * r); // GM = 1 in sim units
    const double relativisticFactor = 1.0 / std::sqrt(std::max(1e-4, 1.0 - rs / r));
    const double magnitude = std::min(newtonTerm * relativisticFactor, 5000.0);
    return -glm::normalize(position) * magnitude;
}

} // namespace Physics
