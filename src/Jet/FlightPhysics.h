#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Ported (and adapted for orbiting a black hole instead of flying over an
// Earth-like atmosphere) from the "jet system new" project's Physics class.
// It's a hybrid "semi-realistic force sim + arcade rate-control" flight
// model: real thrust/drag forces integrated into velocity, but rotation is
// direct rate-control (input -> target angular velocity), not torque/moment
// of inertia -- see UpdateRotation. Two changes from the original, both
// deliberate:
//   1. Gravity now pulls toward the black hole at the origin, using the
//      EXACT general-relativistic Schwarzschild static-observer acceleration
//      (GM/r^2)/sqrt(1-r_s/r) -- see UpdateForces -- instead of a constant
//      -9.81 in a fixed "down", or even plain Newtonian 1/r^2 (which turned
//      out too weak, relative to this jet's thrust, to ever be felt). It
//      reduces to negligible far away, same as Newtonian gravity, but
//      diverges near the horizon: no amount of thrust can hold station
//      arbitrarily close to r_s, so the black hole becomes a real,
//      escalating threat exactly where a black hole's gravity should be one.
//   2. Air density/altitude/ocean/ground mechanics are removed entirely --
//      there's no atmosphere or sea level in orbit around a black hole. Air
//      density is nominally 1.0 (sea-level-equivalent) everywhere, which
//      keeps the arcade thrust/drag/stall feel exactly as tuned in the
//      source project without an atmosphere to justify it physically. This
//      is a deliberate "starfighter" arcade control scheme, not a claim of
//      physical accuracy in vacuum.
namespace Jet {

const float JET_MASS = 15000.0f;           // kg (Su-57 approx) -- only used to convert forces to accelerations
const float MAX_THRUST = 2000000.0f;       // Newtons (afterburner-capable)
const float STALL_SPEED = 70.0f;           // sim units/s
const float G_REFERENCE = 9.81f;           // m/s^2, used only to normalize G-force display, not as world gravity
const float MACH_1 = 343.0f;               // sim units/s, purely for the arcade transonic-drag-rise flavor

class FlightPhysics {
public:
    // State
    glm::vec3 position;
    glm::vec3 velocity;
    glm::quat rotation;
    glm::vec3 angularVelocity;  // rad/s in local space

    float thrust;
    float currentGForce;
    float peakGForce;

    // Control inputs
    float pitchInput;
    float rollInput;
    float yawInput;
    bool afterburner;

    // Set to true for one frame when the jet crosses the event horizon
    // (see Update) -- App.cpp resets the flight on this instead of Physics
    // doing anything scene-specific itself.
    bool capturedByBlackHole;

    explicit FlightPhysics(const glm::vec3& spawnPosition);

    void Update(float deltaTime);
    void Reset(const glm::vec3& spawnPosition);

    // Getters
    float GetSpeed() const;
    float GetMachNumber() const;
    glm::vec3 GetForward() const;
    glm::vec3 GetUp() const;
    glm::vec3 GetRight() const;

private:
    void UpdateForces(float deltaTime);
    void UpdateRotation(float deltaTime);
    void CalculateGForce(const glm::vec3& acceleration);
    float GetDragCoefficient(float mach) const;
};

} // namespace Jet
