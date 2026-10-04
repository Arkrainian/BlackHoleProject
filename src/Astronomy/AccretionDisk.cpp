#include "AccretionDisk.h"

#include <algorithm>
#include <cmath>

namespace Astronomy {

using Physics::Constants::C_SI;
using Physics::Constants::G_SI;
using Physics::Constants::PROTON_MASS_KG;
using Physics::Constants::STEFAN_BOLTZMANN;
using Physics::Constants::THOMSON_CROSS_SECTION_M2;

namespace {

// The real peak Kelvin scale of a Shakura-Sunyaev disk, i.e. T_eff at the
// inner edge (where the (1 - sqrt(r_in/r)) shape term -> 1):
//
//   T_in^4 = 3 G M Mdot / (8 pi sigma r_in^3)
//
// with the accretion rate set by an assumed Eddington fraction f:
//
//   Mdot = f * L_Edd / (eta c^2),   L_Edd = 4 pi G M m_p c / sigma_T
//
// Substituting r_in = x_in * (GM/c^2) (sim units are already in multiples
// of GM/c^2, see Physics/Constants.h) and simplifying, the M dependence
// collapses to a single inverse power -- T_in ~ M^(-1/4) -- which is the
// textbook result that makes supermassive holes have COOLER disks than
// stellar-mass ones despite radiating far more total power: the emitting
// area grows faster (~M^2) than the luminosity (~M), so the flux per unit
// area, and hence T_eff, drops.
double ComputePeakKelvin(double massKg, double innerRadiusSim, double eddingtonFraction, double radiativeEfficiency) {
    // C = 3 * (4 pi G m_p c / sigma_T) * c^4 / (8 pi sigma G^2)
    //   = 3 m_p c^5 / (2 sigma_T sigma G)
    static const double kC = 3.0 * PROTON_MASS_KG * std::pow(C_SI, 5.0) /
                              (2.0 * THOMSON_CROSS_SECTION_M2 * STEFAN_BOLTZMANN * G_SI);
    const double xIn3 = innerRadiusSim * innerRadiusSim * innerRadiusSim;
    const double t4 = kC * eddingtonFraction / (radiativeEfficiency * xIn3 * massKg);
    return std::pow(t4, 0.25);
}

} // namespace

void AccretionDisk::RecomputePeakKelvin() {
    m_peakKelvin = ComputePeakKelvin(m_blackHoleMassKg, m_settings.innerRadius, m_settings.eddingtonFraction,
                                      m_settings.radiativeEfficiency);
}

double AccretionDisk::Temperature(double r) const {
    if (r < m_settings.innerRadius || r > m_settings.outerRadius) {
        return 0.0;
    }
    const double x = m_settings.innerRadius / r; // in (0, 1]
    const double shape = std::pow(x, 0.75) * std::sqrt(std::max(0.0, 1.0 - std::sqrt(x)));
    return m_settings.temperatureScale * shape;
}

namespace {

// A simplified, perceptual approximation of the blackbody color locus
// (NOT an exact spectral integration of the Planck function against CIE
// color-matching curves -- that would need real spectral rendering). Maps
// an effective Kelvin-ish temperature through a small set of hand-picked
// keypoints -- cool red/orange, through warm white, to blue-white hot --
// and linearly interpolates between them on a log scale. Good enough for
// visually distinguishing disk temperature without claiming spectral
// accuracy; documented here so the approximation is never mistaken for a
// physically exact color.
glm::vec3 ApproximateBlackbodyHue(double kelvin) {
    struct Keypoint { double k; glm::vec3 color; };
    static const Keypoint kPoints[] = {
        {1000.0, {1.00f, 0.20f, 0.02f}},
        {2500.0, {1.00f, 0.40f, 0.10f}},
        {4000.0, {1.00f, 0.65f, 0.35f}},
        {5500.0, {1.00f, 0.85f, 0.65f}},
        {6600.0, {1.00f, 1.00f, 1.00f}},
        {10000.0, {0.75f, 0.85f, 1.00f}},
        {20000.0, {0.60f, 0.72f, 1.00f}},
    };
    constexpr int n = sizeof(kPoints) / sizeof(kPoints[0]);

    kelvin = std::clamp(kelvin, kPoints[0].k, kPoints[n - 1].k);
    for (int i = 0; i + 1 < n; ++i) {
        if (kelvin <= kPoints[i + 1].k) {
            const double t = std::log(kelvin / kPoints[i].k) / std::log(kPoints[i + 1].k / kPoints[i].k);
            return glm::mix(kPoints[i].color, kPoints[i + 1].color, static_cast<float>(t));
        }
    }
    return kPoints[n - 1].color;
}

} // namespace

glm::vec3 AccretionDisk::EmittedColor(double temperature) const {
    if (temperature <= 0.0) {
        return glm::vec3(0.0f);
    }
    // temperature is Temperature()'s dimensionless Shakura-Sunyaev shape
    // (post-redshift), which by construction is the same shape that
    // multiplies m_peakKelvin in the real T_eff(r) formula -- so this
    // product IS the disk's actual local effective temperature in Kelvin
    // (see RecomputePeakKelvin()), not an arbitrary color-ramp fudge.
    const double effectiveKelvin = m_peakKelvin * temperature;
    const glm::vec3 hue = ApproximateBlackbodyHue(effectiveKelvin);

    // Stefan-Boltzmann: radiated intensity ~ T^4. This is also exactly what
    // makes redshift correctly dim/brighten the disk (relativistic beaming)
    // when the caller passes in a Doppler/gravitationally shifted
    // temperature -- see AccretionDisk.h. Temperature() peaks around ~0.32
    // (dimensionless) at its default settings, which T^4 alone would crush
    // to ~0.01 -- kVisualNormalization exists purely to bring the default
    // peak brightness back up to ~1.0 for on-screen display; it carries no
    // physical meaning (this whole model isn't flux-calibrated to begin
    // with, see docs/physics.md limitations).
    constexpr double kVisualNormalization = 100.0;
    const double intensity = std::pow(temperature, 4.0) * kVisualNormalization * m_settings.brightnessScale;
    return hue * static_cast<float>(intensity);
}

} // namespace Astronomy
