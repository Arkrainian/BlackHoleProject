#pragma once

// Physical constants (SI) and the geometrized-unit convention used
// internally by the simulator.
//
// Internally almost everything is expressed in "gravitational units" scaled
// to a specific black hole: we set G = c = 1 and additionally choose the
// length unit so that GM/c^2 = 1 for the black hole currently being
// simulated. In that convention the Schwarzschild radius is always exactly
// r_s = 2.0, the photon sphere is always exactly r_ph = 1.5, and the
// critical impact parameter is always exactly b_crit = 1.5*sqrt(3),
// regardless of whether M represents a stellar black hole or Sgr A*.
//
// SimulationUnits below carries the one number (metersPerUnit) needed to
// convert a dimensionless simulation distance back to physical meters, and
// UI code is responsible for ever showing SI numbers to the user.
namespace Physics::Constants {

// SI physical constants.
inline constexpr double G_SI = 6.67430e-11;      // m^3 kg^-1 s^-2
inline constexpr double C_SI = 2.99792458e8;     // m/s
inline constexpr double SOLAR_MASS_KG = 1.98892e30; // kg

// Convenience: gravitational radius GM/c^2 in meters for a mass in kg.
inline double GravitationalRadiusMeters(double massKg) {
    return G_SI * massKg / (C_SI * C_SI);
}

// Holds the meters-per-simulation-unit scale factor implied by a chosen
// black hole mass, so any sim-space distance can be reported in SI units
// for display without threading G/M/c through every call site.
struct SimulationUnits {
    double metersPerUnit = 1.0; // = GM/c^2 in meters, i.e. sim length 1.0 -> this many meters

    explicit SimulationUnits(double massKg = SOLAR_MASS_KG)
        : metersPerUnit(GravitationalRadiusMeters(massKg)) {}

    double ToMeters(double simDistance) const { return simDistance * metersPerUnit; }
    double ToSimUnits(double meters) const { return meters / metersPerUnit; }
};

} // namespace Physics::Constants
