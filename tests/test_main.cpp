#include <cstdio>

#include "TestFramework.h"

void RunSchwarzschildTests();
void RunIntegratorTests();
void RunGeodesicTests();
void RunDeflectionTests();
void RunShadowTests();
void RunDiskTests();
void RunKerrTests();
void RunBinaryTests();
void RunMultiBodyTests();
void RunMutualGravityTests();
void RunPlanetTests();
void RunTidalFlareTests();
void RunUniverseTests();
void RunMultiStarTests();
#ifdef BH_CUDA_AVAILABLE
void RunGpuRenderTests();
void RunKerrGpuRenderTests();
void RunBinaryGpuRenderTests();
#endif
#ifdef BH_PLANET_CUDA_AVAILABLE
void RunPlanetGpuTests();
#endif

int main() {
    std::printf("=== Schwarzschild radii / critical impact parameter ===\n");
    RunSchwarzschildTests();

    std::printf("\n=== Newtonian integrator comparison (Stage 1) ===\n");
    RunIntegratorTests();

    std::printf("\n=== Photon geodesic capture/escape classification (Stage 2) ===\n");
    RunGeodesicTests();

    std::printf("\n=== Weak-field deflection vs analytical formula (Stage 2) ===\n");
    RunDeflectionTests();

    std::printf("\n=== Event horizon vs black-hole shadow (Stage 5) ===\n");
    RunShadowTests();

    std::printf("\n=== Accretion disk: orbital velocity, redshift, temperature (Stage 6) ===\n");
    RunDiskTests();

    std::printf("\n=== Kerr (rotating) black hole: horizons, ISCO, photon orbits, geodesics ===\n");
    RunKerrTests();

    std::printf("\n=== Binary black holes (approximate superposed lensing) ===\n");
    RunBinaryTests();

    std::printf("\n=== Multi-body (freely-placed extra black holes) lensing ===\n");
    RunMultiBodyTests();

    std::printf("\n=== Mutual gravity (object-level pull between spawned stars/planets) ===\n");
    RunMutualGravityTests();

    std::printf("\n=== Planetary destruction: materials, tidal/Roche diagnostics, fracture ===\n");
    RunPlanetTests();

    std::printf("\n=== Tidal disruption flare (debris -> accretion disk coupling) ===\n");
    RunTidalFlareTests();

    std::printf("\n=== Universe / Star Map: real catalog data and coordinate conversion ===\n");
    RunUniverseTests();

    std::printf("\n=== Multi-star systems: 2/3/N-body catalog and mutual-gravity sanity ===\n");
    RunMultiStarTests();

#ifdef BH_CUDA_AVAILABLE
    std::printf("\n=== CPU vs GPU render comparison (Stage 7) ===\n");
    RunGpuRenderTests();

    std::printf("\n=== Kerr CPU vs GPU render comparison ===\n");
    RunKerrGpuRenderTests();

    std::printf("\n=== Binary CPU vs GPU render comparison ===\n");
    RunBinaryGpuRenderTests();
#endif
#ifdef BH_PLANET_CUDA_AVAILABLE
    std::printf("\n=== Planetary destruction: CPU vs GPU step agreement ===\n");
    RunPlanetGpuTests();
#endif

    const int failures = TestFramework::FailureCount();
    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
