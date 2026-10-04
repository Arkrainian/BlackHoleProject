#pragma once

#include <vector>

#include "BlackHole.h"
#include "KeplerOrbit.h"
#include "NewtonianIntegrator.h"
#include "Particle.h"

namespace Physics {

// One test particle's full record for Stage 1: its evolving state, the path
// history for rendering, its analytic Kepler reference (ground truth for a
// pure Newtonian 1/r^2 field), and the running numerical error against that
// reference — this is the "measure error and stability" comparison the
// integrators are validated against.
struct TrackedParticle {
    ParticleState state;
    IntegratorMethod method = IntegratorMethod::RK4;
    ParticleFate fate = ParticleFate::ACTIVE;
    KeplerElements analytic;
    std::vector<glm::dvec3> path;

    double impactParameter = 0.0;  // |L|/|v_infinity|, for labeling/UI
    double maxRadialError = 0.0;   // largest |r_numeric - r_analytic| seen so far
    int stepCount = 0;
};

// A particle to spawn, paired with the integrator method it should be
// advanced with. Spawning the same initial state under all three methods
// is how Stage 1 visualizes their divergence/error directly.
struct ParticleSpawnConfig {
    ParticleState state;
    IntegratorMethod method = IntegratorMethod::RK4;
};

struct NewtonianSimulationConfig {
    double timeStep = 0.01;       // sim time units
    double outerBoundary = 50.0;  // sim distance: ESCAPED once crossed while moving outward
    int maxSteps = 200000;
    int maxPathPoints = 4000;     // decimate stored path so rendering stays cheap on long runs
};

// Owns and steps a batch of independent Newtonian test-particle trajectories
// around a single black hole. This is the Stage 1 control experiment:
// straight 1/r^2 gravity, no relativistic bending, used later as the
// baseline that Stage 2's Schwarzschild photon geodesics are contrasted
// against.
class NewtonianSimulation {
public:
    void Reset(const std::vector<ParticleSpawnConfig>& spawns, const NewtonianSimulationConfig& config);

    // Advances every still-ACTIVE particle by one integrator step and
    // updates fate/error bookkeeping. Safe to call every frame.
    void Step();

    const std::vector<TrackedParticle>& Particles() const { return m_particles; }
    const NewtonianSimulationConfig& Config() const { return m_config; }
    bool AllResolved() const;

private:
    void ClassifyFate(TrackedParticle& p);

    std::vector<TrackedParticle> m_particles;
    NewtonianSimulationConfig m_config;
};

} // namespace Physics
