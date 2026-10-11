#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Physics/BlackHole.h"
#include "Physics/PlanetMaterial.h"

namespace Astronomy {

// One edge of a planet's internal spring lattice, connecting two particle
// indices. Springs model material cohesion -- the thing StarBody
// deliberately omits (see StarBody.h) -- as an explicit, breakable force,
// so a planet's deformation and fracture are a direct consequence of
// stress in this lattice exceeding its material's strength, not a cosmetic
// stretch or a Roche-limit distance check.
struct PlanetSpring {
    int a = -1;
    int b = -1;
    double restLength = 0.0;
    bool broken = false;
};

struct PlanetParticle {
    glm::dvec3 position;
    glm::dvec3 velocity;

    // Equal share of the planet's total mass across all particles (live or
    // not) -- simplest physically-consistent split for a uniform-density
    // approximation; see PlanetBody.h class comment for what a real
    // differentiated interior would need instead.
    double mass = 0.0;

    std::vector<glm::dvec3> trail;

    // True once this particle crossed the (single, Schwarzschild) event
    // horizon -- same convention and same limitation as StarParticle::consumed.
    bool consumed = false;

    // Recomputed every frame a spring broke since the last rebuild (see
    // PlanetBody::RebuildFragments) -- which connected cluster of
    // still-linked particles this one currently belongs to. -1 before the
    // first rebuild only; every live particle belongs to exactly one
    // fragment once the lattice exists, even if nothing has broken yet (a
    // fully intact planet is "fragment 0").
    int fragmentId = -1;
};

// A group of particles still connected to each other (directly or through a
// chain of unbroken springs), tracked as a unit so the UI/renderer can
// report fragment count, capture/escape classification, and a distinct
// color per piece once a planet starts coming apart. Purely a bookkeeping
// view over the particle array -- a fragment owns no independent state and
// is fully derived from which springs are still intact (PlanetBody::RebuildFragments).
struct PlanetFragment {
    int id = -1;
    std::vector<int> particleIndices;
    glm::dvec3 comPosition{0.0};
    glm::dvec3 comVelocity{0.0};
    double mass = 0.0;
    bool captured = false; // every particle in it has crossed the horizon
    bool escaped = false;  // heuristic: moving outward, well beyond the spawn-scale distance
};

struct PlanetBodySettings {
    // Sim units -- same exaggeration-for-visibility rationale as
    // StarBodySettings::initialRadius.
    double initialRadius = 1.5;

    // SI kilograms. Only used for the Roche-limit/tidal-acceleration
    // diagnostics (Physics/TidalForceModel.h) and for splitting mass across
    // particles/fragments -- never fed into the per-particle force law,
    // which (like StarBody) treats every particle as a massless test
    // particle under the black hole's field. The planet's OWN gravity on
    // itself (self-gravity) is explicitly not modeled this milestone; see
    // the class comment below.
    double totalMassKg = 5.972e24; // Earth mass, as a default

    // Moderate by design (spec 28.6: avoid thousands of expensive,
    // individually managed objects) -- enough particles for the spring
    // lattice to visibly deform and fracture into several fragments without
    // the O(N^2) neighbor-connection step (one-time, at construction) or the
    // per-frame union-find becoming a bottleneck.
    int particleCount = 90;

    Physics::PlanetMaterial material;
};

// A planet, modeled as a cluster of point-mass particles connected by a
// breakable spring lattice, free-falling through the black hole's field.
//
// This extends StarBody's "cluster of independent test particles" approach
// (see StarBody.h) with an explicit cohesion force: each particle feels the
// SAME exact point-mass gravity law every other body in this sim uses
// (Physics::StaticObserverGravityAccelSim) PLUS a spring force toward its
// lattice neighbors. Deformation is therefore a real consequence of two
// competing forces -- differential gravity trying to pull the lattice apart,
// springs trying to hold it together -- rather than StarBody's
// already-unbound swarm or a cosmetic stretch. Once a spring's strain
// exceeds its material's PlanetMaterial::materialStrength it breaks
// permanently; connected components of the remaining lattice are tracked as
// Fragments.
//
// Explicitly NOT modeled this milestone (see HANDOFF.md for the
// continuation plan):
//   - Planetary self-gravity: fragments/particles do not attract each
//     other. In reality self-gravity is what the springs here stand in
//     for approximately -- real self-gravity would also let large
//     fragments gravitationally reaccumulate, which this lattice cannot
//     reproduce once broken.
//   - Differentiated internal layers (core/mantle/crust behaving
//     differently): PlanetComposition::Differentiated is approximated as a
//     single averaged material, not a layered structure.
//   - Kerr/binary-hole-aware capture: like StarBody, the capture check uses
//     only the single-hole Schwarzschild radius.
class PlanetBody {
public:
    PlanetBody(const glm::dvec3& centerPosition, const glm::dvec3& centerVelocity,
               const PlanetBodySettings& settings = {});

    // Integrates every non-consumed particle one step (gravity + spring
    // forces, semi-implicit Euler -- same integration style as
    // StarBody/Jet::FlightPhysics), checks for new fractures, and rebuilds
    // the fragment list if the lattice topology changed.
    void Update(double deltaTime, const Physics::BlackHole& blackHole);

    // Same physics, same result (within float-vs-double rounding -- see
    // cuda/PlanetKernel.h), but the gravity/spring/integration/fracture math
    // runs on the GPU via cuda/planet_kernel.cu (one thread per particle or
    // per spring) instead of this process's CPU loop. Returns false (this
    // instance's state is left completely untouched -- the caller should
    // fall back to Update() for this frame) when the project wasn't built
    // with BH_ENABLE_CUDA, or no CUDA device is available, or the GPU call
    // itself failed. There is no "force GPU" mode and no separate "force
    // CPU" toggle -- same automatic-fallback-only philosophy the ray tracer
    // already uses (see RayTracing::CudaAvailable's call sites in App.cpp).
    bool UpdateGPU(double deltaTime, const Physics::BlackHole& blackHole);

    // True if this build was compiled with BH_ENABLE_CUDA AND a CUDA device
    // was found at runtime -- i.e. whether UpdateGPU() can be expected to
    // succeed. The UI uses this for a status readout; UpdateGPU()'s own
    // return value is still the authoritative per-call success/failure
    // signal (this can be true while a specific call still fails, e.g. a
    // transient allocation failure).
    static bool GpuAvailable();

    // True once every particle has crossed the horizon -- same semantics as
    // StarBody::IsFullyConsumed. A planet on a stable orbit that never falls
    // in stays alive indefinitely, same as a star would; the UI's "Clear
    // Planets" button is the manual escape hatch for that case.
    bool IsFullyConsumedOrDispersed() const;

    const std::vector<PlanetParticle>& Particles() const { return m_particles; }
    const std::vector<PlanetSpring>& Springs() const { return m_springs; }
    const std::vector<PlanetFragment>& Fragments() const { return m_fragments; }
    const glm::vec3& BaseColor() const { return m_settings.material.color; }
    const PlanetBodySettings& Settings() const { return m_settings; }

    // Last values computed by Update (zero before the first Update call) --
    // the UI reads these directly rather than recomputing them, so the
    // displayed number always matches exactly what this instance last used.
    double LastTidalAccelerationSim() const { return m_lastTidalAccel; }
    double LastRocheLimitSim() const { return m_lastRocheLimit; }

private:
    void BuildSpringLattice();
    void ApplySpringForces(std::vector<glm::dvec3>& accel) const;
    void CheckFractures();
    void RebuildFragments();

    // Shared tail of a physics step, used by both Update() and UpdateGPU()
    // after each has done its own force/integration/fracture work (on the
    // CPU or GPU respectively): pushes a trail point for every still-live
    // particle, rebuilds fragments if the lattice topology changed, and
    // recomputes the tidal-acceleration/Roche-limit diagnostics.
    void FinishStep(const Physics::BlackHole& blackHole);

    std::vector<PlanetParticle> m_particles;
    std::vector<PlanetSpring> m_springs;
    std::vector<PlanetFragment> m_fragments;
    PlanetBodySettings m_settings;

    double m_lastTidalAccel = 0.0;
    double m_lastRocheLimit = 0.0;
    bool m_fragmentsDirty = true;
};

} // namespace Astronomy
