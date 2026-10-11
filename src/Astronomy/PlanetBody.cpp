#include "PlanetBody.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

#include "Physics/TidalForceModel.h"

#ifdef BH_PLANET_CUDA_AVAILABLE
#include "../../cuda/PlanetKernel.h" // plain header, no CUDA syntax -- see its own comment
#endif

namespace Astronomy {

namespace {

// Same rejection-sampling approach as StarBody::RandomPointInSphere -- only
// runs once per planet, not per frame, so simplicity wins over speed.
glm::dvec3 RandomPointInSphere(std::mt19937& rng, double radius) {
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    glm::dvec3 p;
    do {
        p = glm::dvec3(uniform(rng), uniform(rng), uniform(rng));
    } while (glm::dot(p, p) > 1.0);
    return p * radius;
}

// Base spring stiffness constant (sim units: acceleration per unit strain
// per unit mass), before PlanetMaterial::stiffnessScale. Chosen empirically
// so that, combined with the particle spacing BuildSpringLattice produces at
// the default particleCount/initialRadius, a rocky planet's natural
// oscillation period is comfortably shorter than the timescale of a close
// tidal encounter -- i.e. the lattice has time to actually resist before
// the planet has already fallen past the hole. Not a measured material
// constant; documented approximation per spec 28.5.
constexpr double kBaseSpringStiffness = 400.0;

// Number of nearest neighbors each particle connects to when the lattice is
// built. Enough to resist bending/shearing, not so many that the one-time
// O(N^2 log N) neighbor search or the per-frame spring-force loop becomes
// expensive at particleCount ~ 100-200.
constexpr int kNeighborsPerParticle = 6;

} // namespace

PlanetBody::PlanetBody(const glm::dvec3& centerPosition, const glm::dvec3& centerVelocity,
                        const PlanetBodySettings& settings)
    : m_settings(settings) {
    std::random_device seedSource;
    std::mt19937 rng(seedSource());

    const int count = std::max(1, settings.particleCount);
    const double perParticleMass = settings.totalMassKg / static_cast<double>(count);

    m_particles.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        PlanetParticle particle;
        particle.position = centerPosition + RandomPointInSphere(rng, settings.initialRadius);
        // No internal velocity dispersion, same rationale as StarBody: any
        // separation that follows is attributable to gravity/spring forces,
        // not an initial kick.
        particle.velocity = centerVelocity;
        particle.mass = perParticleMass;
        particle.trail.push_back(particle.position);
        m_particles.push_back(std::move(particle));
    }

    BuildSpringLattice();
    RebuildFragments();
}

void PlanetBody::BuildSpringLattice() {
    const int n = static_cast<int>(m_particles.size());
    m_springs.clear();

    // For each particle, connect to its kNeighborsPerParticle nearest other
    // particles that it isn't already connected to. O(N^2) distance
    // computation is fine at this particle count and only runs once.
    std::vector<std::pair<double, int>> distances; // (distanceSq, index)
    for (int i = 0; i < n; ++i) {
        distances.clear();
        distances.reserve(static_cast<size_t>(n));
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const glm::dvec3 diff = m_particles[j].position - m_particles[i].position;
            distances.emplace_back(glm::dot(diff, diff), j);
        }
        const int neighbors = std::min(kNeighborsPerParticle, static_cast<int>(distances.size()));
        std::partial_sort(distances.begin(), distances.begin() + neighbors, distances.end());

        for (int k = 0; k < neighbors; ++k) {
            const int j = distances[static_cast<size_t>(k)].second;
            // Avoid duplicate springs (i-j and j-i) by only adding when i < j;
            // this means some mutual-nearest-neighbor pairs end up connected
            // from only one side's search, which is fine -- the goal is a
            // well-connected lattice, not an exact symmetric graph.
            if (i >= j) continue;
            bool alreadyConnected = false;
            for (const PlanetSpring& spring : m_springs) {
                if ((spring.a == i && spring.b == j) || (spring.a == j && spring.b == i)) {
                    alreadyConnected = true;
                    break;
                }
            }
            if (alreadyConnected) continue;

            PlanetSpring spring;
            spring.a = i;
            spring.b = j;
            spring.restLength = glm::length(m_particles[j].position - m_particles[i].position);
            m_springs.push_back(spring);
        }
    }
}

void PlanetBody::ApplySpringForces(std::vector<glm::dvec3>& accel) const {
    const double stiffness = kBaseSpringStiffness * m_settings.material.stiffnessScale;
    const double damping = m_settings.material.dampingFactor;

    for (const PlanetSpring& spring : m_springs) {
        if (spring.broken) continue;
        const PlanetParticle& pa = m_particles[static_cast<size_t>(spring.a)];
        const PlanetParticle& pb = m_particles[static_cast<size_t>(spring.b)];
        if (pa.consumed || pb.consumed) continue;

        const glm::dvec3 delta = pb.position - pa.position;
        const double length = glm::length(delta);
        if (length < 1e-9) continue;
        const glm::dvec3 direction = delta / length;
        const double strain = length - spring.restLength;

        // Hooke's law force, plus damping proportional to the relative
        // velocity along the spring's own axis (keeps the explicit
        // integrator from ringing indefinitely -- see PlanetMaterial::dampingFactor).
        const double relativeVelocityAlongAxis = glm::dot(pb.velocity - pa.velocity, direction);
        const double forceMagnitude = stiffness * strain + damping * stiffness * relativeVelocityAlongAxis;
        const glm::dvec3 force = direction * forceMagnitude;

        // Equal and opposite -- no momentum is created by a spring, only
        // exchanged between its two ends (this is what the momentum-
        // conservation test checks).
        accel[static_cast<size_t>(spring.a)] += force / pa.mass;
        accel[static_cast<size_t>(spring.b)] -= force / pb.mass;
    }
}

void PlanetBody::CheckFractures() {
    const double strength = m_settings.material.materialStrength;
    for (PlanetSpring& spring : m_springs) {
        if (spring.broken) continue;
        if (spring.restLength < 1e-9) continue;
        const PlanetParticle& pa = m_particles[static_cast<size_t>(spring.a)];
        const PlanetParticle& pb = m_particles[static_cast<size_t>(spring.b)];
        const double length = glm::length(pb.position - pa.position);
        const double strain = std::abs(length - spring.restLength) / spring.restLength;
        if (strain > strength) {
            spring.broken = true;
            m_fragmentsDirty = true;
        }
    }
}

void PlanetBody::RebuildFragments() {
    const int n = static_cast<int>(m_particles.size());
    std::vector<int> parent(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) parent[static_cast<size_t>(i)] = i;

    // Classic union-find, iterative path-walk (n is small, no need for
    // path compression/union-by-rank machinery here).
    std::function<int(int)> find = [&](int x) {
        while (parent[static_cast<size_t>(x)] != x) x = parent[static_cast<size_t>(x)];
        return x;
    };
    for (const PlanetSpring& spring : m_springs) {
        if (spring.broken) continue;
        const int ra = find(spring.a);
        const int rb = find(spring.b);
        if (ra != rb) parent[static_cast<size_t>(ra)] = rb;
    }

    std::vector<int> rootToFragment(static_cast<size_t>(n), -1);
    m_fragments.clear();
    for (int i = 0; i < n; ++i) {
        const int root = find(i);
        int fragmentIndex = rootToFragment[static_cast<size_t>(root)];
        if (fragmentIndex < 0) {
            PlanetFragment fragment;
            fragment.id = static_cast<int>(m_fragments.size());
            m_fragments.push_back(fragment);
            fragmentIndex = fragment.id;
            rootToFragment[static_cast<size_t>(root)] = fragmentIndex;
        }
        m_particles[static_cast<size_t>(i)].fragmentId = fragmentIndex;
        m_fragments[static_cast<size_t>(fragmentIndex)].particleIndices.push_back(i);
    }

    for (PlanetFragment& fragment : m_fragments) {
        glm::dvec3 weightedPosition(0.0), weightedVelocity(0.0);
        double mass = 0.0;
        bool allConsumed = !fragment.particleIndices.empty();
        for (int index : fragment.particleIndices) {
            const PlanetParticle& particle = m_particles[static_cast<size_t>(index)];
            weightedPosition += particle.position * particle.mass;
            weightedVelocity += particle.velocity * particle.mass;
            mass += particle.mass;
            if (!particle.consumed) allConsumed = false;
        }
        fragment.mass = mass;
        if (mass > 0.0) {
            fragment.comPosition = weightedPosition / mass;
            fragment.comVelocity = weightedVelocity / mass;
        }
        fragment.captured = allConsumed;
        // Heuristic "escaped" classification (spec 28.11): moving outward
        // with enough speed that, under the dominant 1/r^2 term alone, it
        // would never fall back -- i.e. local escape velocity exceeded.
        // This ignores the spring forces still acting on bound fragments,
        // which is an acceptable looseness for a UI readout, not a physical
        // claim about the fragment's long-term fate.
        const double r = glm::length(fragment.comPosition);
        const double speed = glm::length(fragment.comVelocity);
        const double radialSpeed = r > 1e-9 ? glm::dot(fragment.comVelocity, fragment.comPosition) / r : 0.0;
        const double escapeSpeed = r > 1e-9 ? std::sqrt(2.0 / r) : 0.0; // GM=1 sim units
        fragment.escaped = !fragment.captured && radialSpeed > 0.0 && speed > escapeSpeed;
    }

    m_fragmentsDirty = false;
}

void PlanetBody::Update(double deltaTime, const Physics::BlackHole& blackHole) {
    const double rs = Physics::BlackHole::SchwarzschildRadiusSim();
    const int n = static_cast<int>(m_particles.size());

    std::vector<glm::dvec3> accel(static_cast<size_t>(n), glm::dvec3(0.0));
    for (int i = 0; i < n; ++i) {
        if (m_particles[static_cast<size_t>(i)].consumed) continue;
        accel[static_cast<size_t>(i)] = Physics::StaticObserverGravityAccelSim(m_particles[static_cast<size_t>(i)].position);
    }
    ApplySpringForces(accel);

    for (int i = 0; i < n; ++i) {
        PlanetParticle& particle = m_particles[static_cast<size_t>(i)];
        if (particle.consumed) continue;

        particle.velocity += accel[static_cast<size_t>(i)] * deltaTime;
        particle.position += particle.velocity * deltaTime;

        if (glm::length(particle.position) < rs) {
            particle.consumed = true;
            m_fragmentsDirty = true;
        }
    }

    CheckFractures();
    FinishStep(blackHole);
}

void PlanetBody::FinishStep(const Physics::BlackHole& blackHole) {
    constexpr size_t kTrailLength = 50;
    for (PlanetParticle& particle : m_particles) {
        if (particle.consumed) continue;
        particle.trail.push_back(particle.position);
        if (particle.trail.size() > kTrailLength) {
            particle.trail.erase(particle.trail.begin());
        }
    }

    if (m_fragmentsDirty) {
        RebuildFragments();
    }

    // Diagnostics only -- see Physics/TidalForceModel.h. Evaluated at the
    // planet's current center of mass (fragment 0 before any fracture, or
    // the whole cluster's mass-weighted position otherwise).
    glm::dvec3 comPosition(0.0);
    double totalMass = 0.0;
    for (const PlanetParticle& particle : m_particles) {
        if (particle.consumed) continue;
        comPosition += particle.position * particle.mass;
        totalMass += particle.mass;
    }
    if (totalMass > 0.0) {
        comPosition /= totalMass;
        const double r = glm::length(comPosition);
        m_lastTidalAccel = Physics::NewtonianTidalAccelerationSim(blackHole.MassKg(), m_settings.initialRadius, r);
        m_lastRocheLimit = Physics::RocheLimitSim(m_settings.initialRadius, blackHole.MassKg(), m_settings.totalMassKg);
    }
}

#ifdef BH_PLANET_CUDA_AVAILABLE
bool PlanetBody::UpdateGPU(double deltaTime, const Physics::BlackHole& blackHole) {
    const int n = static_cast<int>(m_particles.size());
    if (n == 0) return true;

    // Marshal this planet's state into flat float/uchar arrays -- the GPU
    // kernel works entirely in float (see cuda/PlanetKernel.h for why),
    // same precision tradeoff the Kerr/binary ray-tracing kernels already
    // accept. Nothing in m_particles/m_springs is touched yet, so if the
    // GPU call below fails, this instance is left completely unmodified
    // and the caller can safely fall back to Update() for this frame.
    std::vector<float> positions(static_cast<size_t>(n) * 3);
    std::vector<float> velocities(static_cast<size_t>(n) * 3);
    std::vector<float> masses(static_cast<size_t>(n));
    std::vector<unsigned char> consumed(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const PlanetParticle& p = m_particles[static_cast<size_t>(i)];
        positions[static_cast<size_t>(i) * 3 + 0] = static_cast<float>(p.position.x);
        positions[static_cast<size_t>(i) * 3 + 1] = static_cast<float>(p.position.y);
        positions[static_cast<size_t>(i) * 3 + 2] = static_cast<float>(p.position.z);
        velocities[static_cast<size_t>(i) * 3 + 0] = static_cast<float>(p.velocity.x);
        velocities[static_cast<size_t>(i) * 3 + 1] = static_cast<float>(p.velocity.y);
        velocities[static_cast<size_t>(i) * 3 + 2] = static_cast<float>(p.velocity.z);
        masses[static_cast<size_t>(i)] = static_cast<float>(p.mass);
        consumed[static_cast<size_t>(i)] = p.consumed ? 1 : 0;
    }

    const int springCount = static_cast<int>(m_springs.size());
    std::vector<int> springA(static_cast<size_t>(springCount));
    std::vector<int> springB(static_cast<size_t>(springCount));
    std::vector<float> springRestLengths(static_cast<size_t>(springCount));
    std::vector<unsigned char> springBroken(static_cast<size_t>(springCount));
    for (int i = 0; i < springCount; ++i) {
        const PlanetSpring& s = m_springs[static_cast<size_t>(i)];
        springA[static_cast<size_t>(i)] = s.a;
        springB[static_cast<size_t>(i)] = s.b;
        springRestLengths[static_cast<size_t>(i)] = static_cast<float>(s.restLength);
        springBroken[static_cast<size_t>(i)] = s.broken ? 1 : 0;
    }

    const double stiffness = kBaseSpringStiffness * m_settings.material.stiffnessScale;
    const bool ok = CudaPlanetSim::RunPlanetStepCUDA(
        positions.data(), velocities.data(), masses.data(), consumed.data(), n,
        springCount > 0 ? springA.data() : nullptr, springCount > 0 ? springB.data() : nullptr,
        springCount > 0 ? springRestLengths.data() : nullptr, springCount > 0 ? springBroken.data() : nullptr,
        springCount, static_cast<float>(Physics::BlackHole::SchwarzschildRadiusSim()),
        static_cast<float>(stiffness), static_cast<float>(m_settings.material.dampingFactor),
        static_cast<float>(m_settings.material.materialStrength), static_cast<float>(deltaTime));
    if (!ok) return false;

    // Precision note: a particle already consumed before this call round-
    // trips through float and back -- lossless from here on (float->double
    // is exact), but its position permanently loses the double-precision
    // detail it had right at the moment of capture. Acceptable: nothing
    // reads a consumed particle's exact position again except the
    // (already-approximate) fragment bookkeeping below.
    for (int i = 0; i < n; ++i) {
        PlanetParticle& p = m_particles[static_cast<size_t>(i)];
        p.position = glm::dvec3(positions[static_cast<size_t>(i) * 3 + 0], positions[static_cast<size_t>(i) * 3 + 1],
                                 positions[static_cast<size_t>(i) * 3 + 2]);
        p.velocity = glm::dvec3(velocities[static_cast<size_t>(i) * 3 + 0], velocities[static_cast<size_t>(i) * 3 + 1],
                                 velocities[static_cast<size_t>(i) * 3 + 2]);
        p.consumed = consumed[static_cast<size_t>(i)] != 0;
    }
    for (int i = 0; i < springCount; ++i) {
        if (springBroken[static_cast<size_t>(i)]) {
            m_springs[static_cast<size_t>(i)].broken = true;
        }
    }

    // The GPU kernel already did fracture detection, so CheckFractures()
    // (the CPU version of that same check) is skipped here -- but whether
    // a spring broke isn't cheaply known from this function's return value
    // alone, so this unconditionally asks FinishStep to rebuild fragments
    // rather than tracking a separate "did anything change" flag. Rebuild
    // is O(particles+springs) union-find, cheap at the sizes this is
    // actually used at.
    m_fragmentsDirty = true;
    FinishStep(blackHole);
    return true;
}
bool PlanetBody::GpuAvailable() { return CudaPlanetSim::PlanetCudaDeviceAvailable(); }
#else
bool PlanetBody::UpdateGPU(double /*deltaTime*/, const Physics::BlackHole& /*blackHole*/) { return false; }
bool PlanetBody::GpuAvailable() { return false; }
#endif

bool PlanetBody::IsFullyConsumedOrDispersed() const {
    for (const PlanetParticle& particle : m_particles) {
        if (!particle.consumed) return false;
    }
    return true;
}

} // namespace Astronomy
