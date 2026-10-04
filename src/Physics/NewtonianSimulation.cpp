#include "NewtonianSimulation.h"

#include <algorithm>
#include <cmath>

namespace Physics {

void NewtonianSimulation::Reset(const std::vector<ParticleSpawnConfig>& spawns,
                                 const NewtonianSimulationConfig& config) {
    m_config = config;
    m_particles.clear();
    m_particles.reserve(spawns.size());

    for (const ParticleSpawnConfig& spawn : spawns) {
        const ParticleState& initial = spawn.state;
        TrackedParticle p;
        p.state = initial;
        p.method = spawn.method;
        p.analytic = ComputeKeplerElements(initial.position, initial.velocity);

        const glm::dvec3 angularMomentum = glm::cross(initial.position, initial.velocity);
        const double speed = glm::length(initial.velocity);
        p.impactParameter = speed > 1e-9 ? glm::length(angularMomentum) / speed : 0.0;

        p.path.push_back(initial.position);
        m_particles.push_back(std::move(p));
    }
}

void NewtonianSimulation::ClassifyFate(TrackedParticle& p) {
    const double r = glm::length(p.state.position);
    const double radialVelocity = glm::dot(p.state.position, p.state.velocity) / r;

    if (r <= BlackHole::SchwarzschildRadiusSim()) {
        p.fate = ParticleFate::CAPTURED;
    } else if (r >= m_config.outerBoundary && radialVelocity > 0.0) {
        p.fate = ParticleFate::ESCAPED;
    } else if (p.stepCount >= m_config.maxSteps) {
        p.fate = ParticleFate::UNRESOLVED;
    }
}

void NewtonianSimulation::Step() {
    for (TrackedParticle& p : m_particles) {
        if (p.fate != ParticleFate::ACTIVE) {
            continue;
        }

        IntegrateStep(p.state, m_config.timeStep, p.method);
        ++p.stepCount;

        const double rNumeric = glm::length(p.state.position);
        const double theta = KeplerTrueAnomalyOf(p.analytic, p.state.position);
        const double rAnalytic = KeplerRadiusAtTrueAnomaly(p.analytic, theta);
        if (rAnalytic > 0.0) {
            p.maxRadialError = std::max(p.maxRadialError, std::abs(rNumeric - rAnalytic));
        }

        // Decimate stored path: once over budget, keep every other point
        // going forward so long-running orbits don't grow memory/draw cost
        // unboundedly while still showing the overall shape.
        if (static_cast<int>(p.path.size()) < m_config.maxPathPoints || (p.stepCount % 2 == 0)) {
            p.path.push_back(p.state.position);
        }

        ClassifyFate(p);
    }
}

bool NewtonianSimulation::AllResolved() const {
    for (const TrackedParticle& p : m_particles) {
        if (p.fate == ParticleFate::ACTIVE) {
            return false;
        }
    }
    return true;
}

} // namespace Physics
