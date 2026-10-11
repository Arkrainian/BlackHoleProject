#include "TidalDisruptionFlare.h"

#include <algorithm>
#include <cmath>

namespace Astronomy {

namespace {

// Exponential decay time constant (seconds -- PlanetBody::Update's
// deltaTime is the app's real wall-clock frame delta, so this reads
// directly as "roughly how many seconds a flare stays visible"). Not a
// measured fallback-accretion timescale -- see this class's header
// comment for why a real t^-5/3 curve isn't implemented. Picked so a
// capture event stays visible for a noticeable stretch (tens of seconds)
// without lingering indefinitely.
constexpr double kDecayTimeConstantSeconds = 12.0;

// Converts the accumulated captured-mass scalar (kg) into an additional
// AccretionDiskSettings::eddingtonFraction. Tuned so capturing roughly an
// Earth's worth of mass in total produces a clearly visible (not
// overwhelming) brightness bump, given the UI's eddingtonFraction slider
// range is [0.01, 1.0] -- see kMaxEddingtonFractionBoost for the ceiling
// this is clamped to regardless of how much mass is captured at once.
constexpr double kEddingtonBoostPerKg = 5.0e-26;

// Hard ceiling on the boost, regardless of how much mass was captured --
// keeps a single huge capture event from sending the disk's effective
// eddingtonFraction into a nonsensical/visually-overflowing range.
constexpr double kMaxEddingtonFractionBoost = 0.6;

// Below this, IsActive() reports false and the boost is treated as
// negligible for UI purposes (it's still applied to the disk settings,
// just imperceptibly).
constexpr double kActiveThreshold = 0.002;

} // namespace

void TidalDisruptionFlare::RegisterCapturedMass(double massKg) {
    if (massKg > 0.0) {
        m_accumulatorKg += massKg;
    }
}

void TidalDisruptionFlare::Update(double deltaTime) {
    if (deltaTime <= 0.0 || m_accumulatorKg <= 0.0) return;
    m_accumulatorKg *= std::exp(-deltaTime / kDecayTimeConstantSeconds);
    if (m_accumulatorKg < 1.0) {
        m_accumulatorKg = 0.0; // avoid an eternally-shrinking-but-nonzero tail
    }
}

double TidalDisruptionFlare::EddingtonFractionBoost() const {
    return std::min(kMaxEddingtonFractionBoost, m_accumulatorKg * kEddingtonBoostPerKg);
}

bool TidalDisruptionFlare::IsActive() const { return EddingtonFractionBoost() > kActiveThreshold; }

} // namespace Astronomy
