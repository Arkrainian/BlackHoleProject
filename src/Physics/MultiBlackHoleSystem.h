#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "BinarySystem.h"
#include "BlackHole.h"
#include "GravitySource.h"

namespace Physics {

// Upper bound on how many bodies Physics::TracePhotonMultiBody (and the
// star/planet gravity it parallels) will ever consider at once: 1 primary
// + 1 binary-mode second body (if binary mode is also on) + up to 4
// user-placed ExtraBlackHoles. A fixed, small cap keeps every array below
// simple stack storage -- no dynamic allocation per photon/per step -- and
// matches the UI's own 4-extra-black-hole limit (see App.cpp).
constexpr int kMaxLensingBodies = 6;

// A black hole placed freely by the user -- NOT constrained to
// BinarySystem's circular-orbit formula the way its own second body is.
// A simple point mass: position, plus an optional constant-velocity
// drift. No gravity acts ON an ExtraBlackHole itself (it doesn't respond
// to the primary, to other extra black holes, or to anything else) --
// consistent with every other spawned object in this sim being a test
// particle with no mutual gravity (see docs/physics.md).
// gmRatio is this body's GM relative to the primary black hole's GM=1,
// the same convention BinarySystem::massRatio already uses for ITS
// second body.
struct ExtraBlackHole {
    double gmRatio = 1.0;
    glm::dvec3 position{0.0};
    glm::dvec3 velocity{0.0};

    double SchwarzschildRadiusSim() const { return 2.0 * gmRatio; }
    void Advance(double deltaTime) { position += velocity * deltaTime; }
};

// One body contributing to lensing/gravity in the generalized multi-body
// solver (Physics::TracePhotonMultiBody) and in star/planet gravity -- a
// flattened, uniform view over whichever of {the primary black hole,
// binary mode's second body, every ExtraBlackHole} are active in a given
// frame, so neither the solver nor StarBody/PlanetBody need to know where
// each one actually came from. captureRadius is captureMultiplier * rs,
// precomputed by whoever builds this list (mirrors how BinaryGeodesicSolver
// already precomputes captureR1/captureR2 once per trace rather than
// re-deriving it every step).
struct LensingBody {
    glm::dvec3 position{0.0};
    double gm = 1.0;
    double rs = 2.0;
    double captureRadius = 2.0;
};

// Shared by Astronomy::StarBody::Update and Astronomy::PlanetBody::Update
// so neither reimplements "sum gravity across whichever of binary/extras/
// mutual sources are active" separately. Starts from the primary's pull
// (binary-aware if `binary` is non-null, single-hole
// StaticObserverGravityAccelSim otherwise -- same convention StarBody/
// PlanetBody already used before ExtraBlackHoles existed), then adds every
// ExtraBlackHole's own pull, then (new) every mutualGravitySources entry's
// tuned mutual pull (see GravitySource.h for why this is a standalone
// force law, not real physics) -- binary, extras, and mutual sources are
// all independent and any combination can be active at once.
// mutualGravityStrength is ignored when mutualGravitySources is null.
inline glm::dvec3 CombinedGravityAccelSim(const glm::dvec3& position, const BinarySystem* binary,
                                           const std::vector<ExtraBlackHole>* extraBlackHoles,
                                           const std::vector<GravitySource>* mutualGravitySources = nullptr,
                                           double mutualGravityStrength = 1.0) {
    glm::dvec3 accel =
        binary ? binary->GravityAccelSim(position) : StaticObserverGravityAccelSim(position);
    if (extraBlackHoles) {
        for (const ExtraBlackHole& extra : *extraBlackHoles) {
            accel += PointMassGravityAccelSim(position - extra.position, extra.gmRatio, extra.SchwarzschildRadiusSim());
        }
    }
    if (mutualGravitySources) {
        accel += MutualGravityAccelSim(position, *mutualGravitySources, mutualGravityStrength);
    }
    return accel;
}

// Matching capture check: has `position` crossed the primary's horizon
// (binary-aware via BinarySystem::IsCaptured if `binary` is non-null) OR
// any ExtraBlackHole's own horizon.
inline bool IsCapturedByAny(const glm::dvec3& position, const BinarySystem* binary,
                             const std::vector<ExtraBlackHole>* extraBlackHoles) {
    const bool primaryCaptured =
        binary ? binary->IsCaptured(position) : glm::length(position) < BlackHole::SchwarzschildRadiusSim();
    if (primaryCaptured) return true;
    if (extraBlackHoles) {
        for (const ExtraBlackHole& extra : *extraBlackHoles) {
            if (glm::length(position - extra.position) < extra.SchwarzschildRadiusSim()) return true;
        }
    }
    return false;
}

} // namespace Physics
