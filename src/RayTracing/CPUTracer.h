#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "Physics/BinaryGeodesicSolver.h"
#include "Physics/GeodesicSolver.h"
#include "Physics/KerrGeodesicSolver.h"

namespace RayTracing {

// An orthonormal camera basis in simulation space (position + look
// direction, already resolved from whatever UI controls -- orbit angles,
// FOV, etc. -- produced them), plus the projection parameters needed to
// turn a pixel into a ray direction.
struct CameraFrame {
    glm::dvec3 position{0.0};
    glm::dvec3 forward{0.0, 0.0, -1.0};
    glm::dvec3 right{1.0, 0.0, 0.0};
    glm::dvec3 up{0.0, 1.0, 0.0};
    double tanHalfFovY = 0.4142; // tan(45 deg / 2)... i.e. actual tan(fovY/2)
    double aspect = 16.0 / 9.0;  // width / height
};

struct RenderSettings {
    int width = 320;
    int height = 180;
    Physics::GeodesicConfig geodesicConfig;

    // When spin != 0, RenderImage routes every pixel through
    // KerrGeodesicSolver::TracePhotonKerr instead of the Schwarzschild
    // TracePhoton. Both CPU (here) and GPU (RenderImageCUDA ->
    // TraceKerrPhotonsKernel) branch on this same field.
    double spin = 0.0;
    Physics::KerrGeodesicConfig kerrConfig;

    // When set, RenderImage routes every pixel through
    // BinaryGeodesicSolver::TracePhotonBinary instead -- an explicitly
    // approximate scheme for two orbiting masses (see
    // Physics/BinaryGeodesicSolver.h for why "approximate" isn't a
    // shortcut here, there is no exact closed-form two-body metric to
    // fall back to). Mutually exclusive with `spin`: binary mode takes
    // priority if both are set, since a rotating member of a binary
    // isn't modeled. Both CPU (here) and GPU (RenderImageCUDA ->
    // TraceBinaryPhotonsKernel) branch on this same field.
    bool binaryEnabled = false;
    Physics::BinarySystem binarySystem;
    Physics::BinaryGeodesicConfig binaryConfig;
};

using BackgroundSampler = std::function<glm::vec3(const glm::dvec3& direction)>;

// Stage 6: called instead of the background sampler when a ray's first
// obstruction is the accretion disk (settings.geodesicConfig.checkDisk),
// given that ray's full trace result (disk hit radius/position, and the
// combined gravitational+Doppler redshift factor at the crossing point).
using DiskSampler = std::function<glm::vec3(const Physics::PhotonTraceResult&)>;

// Renders one image, row-major, by backward-tracing one photon geodesic per
// pixel from the camera through curved Schwarzschild spacetime and sampling
// `background` wherever a ray escapes to infinity, or `disk` wherever a ray
// hits the accretion disk first (black where captured/unresolved without a
// disk -- i.e. the black hole's shadow). This is real gravitational
// lensing: the distortion is a direct consequence of GeodesicSolver's
// physics, not a screen-space warp applied after the fact. Parallelized
// across rows since every pixel's trace is fully independent.
std::vector<glm::vec3> RenderImage(const CameraFrame& camera, const RenderSettings& settings,
                                    const BackgroundSampler& background, const DiskSampler& disk = nullptr);

} // namespace RayTracing
