#include "TestFramework.h"

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "Astronomy/PlanetBody.h"
#include "Physics/BlackHole.h"
#include "Physics/PlanetMaterial.h"

using namespace TestFramework;
using namespace Physics;

// Validates Astronomy::PlanetBody::UpdateGPU against the CPU Update() path
// it mirrors (cuda/planet_kernel.cu) -- same spirit as RunGpuRenderTests/
// RunKerrGpuRenderTests/RunBinaryGpuRenderTests (test_gpu_render.cpp): a
// tolerance-based comparison, not exact equality, since the GPU path runs
// in float (see cuda/PlanetKernel.h) while the CPU path stays double. Only
// compiled/run when BH_PLANET_CUDA_AVAILABLE is defined (BH_ENABLE_CUDA).
void RunPlanetGpuTests() {
    BlackHole blackHole(10.0);

    Astronomy::PlanetBodySettings settings;
    settings.initialRadius = 1.5;
    settings.particleCount = 60;
    settings.totalMassKg = 5.972e24;
    settings.material = PlanetMaterial::Preset(PlanetComposition::Rocky);

    const double spawnRadius = 14.0;
    const glm::dvec3 position(spawnRadius, 0.0, 0.0);
    const double circularSpeed = std::sqrt(1.0 / spawnRadius);
    const glm::dvec3 velocity(0.0, 0.0, circularSpeed * 0.7);

    // One planet built once, then COPIED (PlanetBody's implicit copy
    // constructor) into a CPU instance and a GPU instance -- both start
    // from bit-identical particle positions/velocities/spring lattice.
    // Building two SEPARATE PlanetBody instances from the same settings
    // would not be comparable: particle placement is randomized per
    // construction (see PlanetBody.cpp's RandomPointInSphere), so each
    // instance would have a different lattice entirely.
    Astronomy::PlanetBody seed(position, velocity, settings);
    Astronomy::PlanetBody cpuPlanet = seed;
    Astronomy::PlanetBody gpuPlanet = seed;

    const double dt = 0.02;
    const int steps = 20; // short enough that float-vs-double rounding hasn't had time to compound into a different fracture outcome
    bool gpuOk = true;
    for (int step = 0; step < steps && gpuOk; ++step) {
        cpuPlanet.Update(dt, blackHole);
        gpuOk = gpuPlanet.UpdateGPU(dt, blackHole);
    }
    ExpectTrue(gpuOk, "UpdateGPU succeeds every step (a CUDA device is expected to be present in this build)");

    if (!gpuOk) return;

    const auto& cpuParticles = cpuPlanet.Particles();
    const auto& gpuParticles = gpuPlanet.Particles();
    ExpectTrue(cpuParticles.size() == gpuParticles.size(), "CPU and GPU planets keep the same particle count");

    double maxPositionDiff = 0.0, maxVelocityDiff = 0.0;
    int consumedMismatches = 0;
    for (size_t i = 0; i < cpuParticles.size() && i < gpuParticles.size(); ++i) {
        maxPositionDiff = std::max(maxPositionDiff, glm::length(cpuParticles[i].position - gpuParticles[i].position));
        maxVelocityDiff = std::max(maxVelocityDiff, glm::length(cpuParticles[i].velocity - gpuParticles[i].velocity));
        if (cpuParticles[i].consumed != gpuParticles[i].consumed) ++consumedMismatches;
    }
    std::printf("  CPU vs GPU planet step (%d steps): max position diff = %.6g, max velocity diff = %.6g\n", steps,
                maxPositionDiff, maxVelocityDiff);

    // Loose-ish tolerances: float vs double rounding over 20 steps at sim
    // scale ~10 units, not a bit-exact match.
    ExpectTrue(maxPositionDiff < 1.0e-2, "CPU and GPU particle positions agree within float-precision tolerance");
    ExpectTrue(maxVelocityDiff < 1.0e-2, "CPU and GPU particle velocities agree within float-precision tolerance");
    ExpectTrue(consumedMismatches == 0, "CPU and GPU agree on which particles (if any) have been consumed");

    int cpuBroken = 0, gpuBroken = 0;
    const auto& cpuSprings = cpuPlanet.Springs();
    const auto& gpuSprings = gpuPlanet.Springs();
    for (const auto& s : cpuSprings) if (s.broken) ++cpuBroken;
    for (const auto& s : gpuSprings) if (s.broken) ++gpuBroken;
    ExpectTrue(cpuBroken == gpuBroken, "CPU and GPU agree on how many springs have broken after a short run");
}
