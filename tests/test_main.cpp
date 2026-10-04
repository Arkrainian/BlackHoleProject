#include <cstdio>

#include "TestFramework.h"

void RunSchwarzschildTests();
void RunIntegratorTests();
void RunGeodesicTests();
void RunDeflectionTests();
void RunShadowTests();
void RunDiskTests();
#ifdef BH_CUDA_AVAILABLE
void RunGpuRenderTests();
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

#ifdef BH_CUDA_AVAILABLE
    std::printf("\n=== CPU vs GPU render comparison (Stage 7) ===\n");
    RunGpuRenderTests();
#endif

    const int failures = TestFramework::FailureCount();
    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
