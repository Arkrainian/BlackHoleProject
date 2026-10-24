#include "StarBody.h"

#include <random>

#include "Physics/BlackHole.h"

namespace Astronomy {

namespace {

// A uniformly-distributed point inside a sphere of the given radius,
// centered on the origin (rejection sampling -- simplest correct approach
// for a particle count this small, and this only ever runs once per star,
// not per frame).
glm::dvec3 RandomPointInSphere(std::mt19937& rng, double radius) {
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    glm::dvec3 p;
    do {
        p = glm::dvec3(uniform(rng), uniform(rng), uniform(rng));
    } while (glm::dot(p, p) > 1.0);
    return p * radius;
}

} // namespace

StarBody::StarBody(const glm::dvec3& centerPosition, const glm::dvec3& centerVelocity,
                    const StarBodySettings& settings)
    : m_settings(settings) {
    // A fresh engine per star (not shared/static) so spawning stars from
    // the UI thread is trivially safe and each star's seed pattern doesn't
    // depend on how many were spawned before it -- this runs once per star,
    // so the setup cost of a new engine is irrelevant.
    std::random_device seedSource;
    std::mt19937 rng(seedSource());

    m_particles.reserve(static_cast<size_t>(settings.particleCount));
    for (int i = 0; i < settings.particleCount; ++i) {
        StarParticle particle;
        particle.position = centerPosition + RandomPointInSphere(rng, settings.initialRadius);
        // No internal velocity dispersion -- every particle starts moving
        // with the star's own bulk velocity (self-gravity, which in a real
        // star would also bind the particles together, is deliberately not
        // modeled here; see StarBody.h). Any later separation is purely
        // tidal: a direct consequence of each particle's own radius from
        // the black hole, not an initial kick.
        particle.velocity = centerVelocity;
        particle.trail.push_back(particle.position);
        m_particles.push_back(std::move(particle));
    }
}

void StarBody::Update(double deltaTime, const Physics::BinarySystem* binary,
                       const std::vector<Physics::ExtraBlackHole>* extraBlackHoles,
                       const std::vector<Physics::GravitySource>* mutualGravitySources,
                       double mutualGravityStrength) {
    for (StarParticle& particle : m_particles) {
        if (particle.consumed) continue;

        const glm::dvec3 acceleration = Physics::CombinedGravityAccelSim(
            particle.position, binary, extraBlackHoles, mutualGravitySources, mutualGravityStrength);
        particle.velocity += acceleration * deltaTime;
        particle.position += particle.velocity * deltaTime;

        if (Physics::IsCapturedByAny(particle.position, binary, extraBlackHoles)) {
            particle.consumed = true;
            continue;
        }

        particle.trail.push_back(particle.position);
        if (static_cast<int>(particle.trail.size()) > m_settings.trailLength) {
            particle.trail.erase(particle.trail.begin());
        }
    }
}

bool StarBody::IsFullyConsumed() const {
    for (const StarParticle& particle : m_particles) {
        if (!particle.consumed) return false;
    }
    return true;
}

glm::dvec3 StarBody::CenterOfMass() const {
    glm::dvec3 sum(0.0);
    int liveCount = 0;
    for (const StarParticle& particle : m_particles) {
        if (particle.consumed) continue;
        sum += particle.position;
        ++liveCount;
    }
    return liveCount > 0 ? sum / static_cast<double>(liveCount) : glm::dvec3(0.0);
}

double StarBody::CurrentMassKg() const {
    if (m_particles.empty()) return 0.0;
    int liveCount = 0;
    for (const StarParticle& particle : m_particles) {
        if (!particle.consumed) ++liveCount;
    }
    return m_settings.totalMassKg * (static_cast<double>(liveCount) / static_cast<double>(m_particles.size()));
}

} // namespace Astronomy
