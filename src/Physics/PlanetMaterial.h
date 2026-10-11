#pragma once

#include <glm/glm.hpp>

namespace Physics {

// What a planet is made of. These presets are reference points, not exact
// measurements of any real body -- they set the relative behavior that
// matters for tidal disruption: how dense it is (-> how strong self-gravity
// would be, even though this milestone doesn't simulate self-gravity
// directly -- see Astronomy/PlanetBody.h), how strongly its internal springs
// resist stretching, how much strain a spring can take before it snaps, and
// how quickly internal oscillations damp out. A rocky/iron planet should
// visibly hold together longer under the same tidal field than an icy one;
// a gas giant should deform first and fracture into many small, loosely
// bound pieces rather than a few large ones.
enum class PlanetComposition {
    Rocky,
    IronRich,
    Icy,
    GasGiant,
    Differentiated,
    Custom
};

struct PlanetMaterial {
    PlanetComposition composition = PlanetComposition::Rocky;

    // Reference bulk density in kg/m^3 -- informational/UI only in this
    // milestone (used for the mass/density consistency check and displayed
    // next to the mass/radius sliders); it does not feed back into the
    // spring simulation, which works in sim-unit lengths and a fraction of
    // the total mass per particle.
    double densityKgPerM3 = 3000.0;

    // Dimensionless strain fraction |deltaLength / restLength| a spring can
    // sustain before it permanently breaks. Lower = fractures earlier/more
    // easily. This is the ONE number per material that actually drives
    // when and how a planet comes apart -- see PlanetBody::CheckFractures.
    double materialStrength = 0.35;

    // Multiplies the base spring stiffness constant (PlanetBody.cpp) -- how
    // hard the material resists being stretched at all, independent of when
    // it breaks. A gas giant is both weak (breaks early) and soft (low
    // stiffness); iron is both strong and stiff.
    double stiffnessScale = 1.0;

    // Velocity-proportional damping along each spring's axis, as a fraction
    // of critical damping for that spring's stiffness/mass -- keeps the
    // lattice from ringing indefinitely once perturbed. Not a real material
    // property measurement, just enough to keep the explicit integrator
    // numerically calm (documented approximation, spec 28.5).
    double dampingFactor = 0.35;

    glm::vec3 color = glm::vec3(0.6f, 0.55f, 0.5f);

    static PlanetMaterial Preset(PlanetComposition composition) {
        PlanetMaterial m;
        m.composition = composition;
        switch (composition) {
            case PlanetComposition::Rocky:
                m.densityKgPerM3 = 3300.0;
                m.materialStrength = 0.35;
                m.stiffnessScale = 1.0;
                m.dampingFactor = 0.35;
                m.color = glm::vec3(0.62f, 0.52f, 0.40f);
                break;
            case PlanetComposition::IronRich:
                m.densityKgPerM3 = 7000.0;
                m.materialStrength = 0.55; // toughest -- holds together longest
                m.stiffnessScale = 1.8;
                m.dampingFactor = 0.4;
                m.color = glm::vec3(0.55f, 0.45f, 0.45f);
                break;
            case PlanetComposition::Icy:
                m.densityKgPerM3 = 920.0;
                m.materialStrength = 0.18; // brittle -- shatters early
                m.stiffnessScale = 0.6;
                m.dampingFactor = 0.25;
                m.color = glm::vec3(0.75f, 0.88f, 0.95f);
                break;
            case PlanetComposition::GasGiant:
                m.densityKgPerM3 = 1300.0;
                m.materialStrength = 0.10; // barely cohesive -- disperses fastest
                m.stiffnessScale = 0.25;
                m.dampingFactor = 0.5;
                m.color = glm::vec3(0.85f, 0.7f, 0.5f);
                break;
            case PlanetComposition::Differentiated:
                // A single material-strength number can't represent a real
                // core/mantle/crust split (that's deferred -- see
                // PlanetBody.h); this preset approximates it as an
                // iron-like-average body, stronger than plain rock since a
                // metallic core contributes most of the cohesion in reality.
                m.densityKgPerM3 = 4800.0;
                m.materialStrength = 0.42;
                m.stiffnessScale = 1.3;
                m.dampingFactor = 0.35;
                m.color = glm::vec3(0.58f, 0.5f, 0.42f);
                break;
            case PlanetComposition::Custom:
            default:
                break;
        }
        return m;
    }
};

} // namespace Physics
