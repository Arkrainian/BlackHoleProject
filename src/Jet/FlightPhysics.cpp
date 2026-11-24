#include "FlightPhysics.h"

#include <algorithm>
#include <cmath>

#include "Physics/BlackHole.h"
#include "Physics/Constants.h"

namespace Jet {

// Starting cruise speed: exactly 0 -- the ship spawns stationary.
// Explicitly requested ("make sure the endurance doesnt have speed on
// spawn, it should have 0 speed"); previously defaulted to a nonzero
// value (150, originally 300) so the ship was already moving the instant
// it spawned/respawned, which is no longer the desired behavior. Shift
// raises it from here with no ceiling of its own.
constexpr float kDefaultCruiseSpeed = 0.0f;

FlightPhysics::FlightPhysics(const glm::vec3& spawnPosition)
    : position(spawnPosition)
    , velocity(0.0f, 0.0f, 0.0f)
    , rotation(glm::identity<glm::quat>())
    , angularVelocity(0.0f)
    , cruiseSpeedSimUnitsPerSec(kDefaultCruiseSpeed)
    , currentGForce(1.0f)
    , peakGForce(1.0f)
    , pitchInput(0.0f)
    , rollInput(0.0f)
    , yawInput(0.0f)
    , afterburner(false)
    , capturedByBlackHole(false)
{}

void FlightPhysics::Reset(const glm::vec3& spawnPosition) {
    position = spawnPosition;
    velocity = glm::vec3(0.0f);
    rotation = glm::identity<glm::quat>();
    angularVelocity = glm::vec3(0.0f);
    cruiseSpeedSimUnitsPerSec = kDefaultCruiseSpeed;
    currentGForce = 1.0f;
    peakGForce = 1.0f;
    capturedByBlackHole = false;
}

void FlightPhysics::Update(float deltaTime, double metersPerSimUnit) {
    // See FlightPhysics.h point 3: convert the real speed cap into however
    // many sim-units/s that is for the CURRENTLY loaded black hole. Smaller
    // holes (tiny metersPerSimUnit) get a huge cap in sim-units/s, so the
    // original arcade thrust/drag equilibrium is what limits speed, same as
    // before this was added; a hole as large as TON 618 gets a small enough
    // cap to actually bind.
    // See MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC in FlightPhysics.h for why the
    // floor exists: without it, this cap goes to ~6e-7 sim-units/s at a
    // hole as massive as TON 618, which is not "slow," it's frozen.
    const double relativisticMaxSpeedSim = (static_cast<double>(maxSpeedFractionOfC) * Physics::Constants::C_SI) / metersPerSimUnit;
    const double maxSpeedSim = std::max(relativisticMaxSpeedSim, static_cast<double>(MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC));
    UpdateForces(deltaTime, static_cast<float>(maxSpeedSim));
    UpdateRotation(deltaTime);

    // Event horizon "crash": falling inside r_s means nothing escapes,
    // including the jet -- flag it and let the caller (App.cpp) decide what
    // that means for the flight (reset), rather than this class knowing
    // about game-over/reset UX itself.
    const double r = glm::length(glm::dvec3(position));
    if (r < Physics::BlackHole::SchwarzschildRadiusSim()) {
        capturedByBlackHole = true;
    }
}

void FlightPhysics::UpdateForces(float deltaTime, float maxSpeedSimUnitsPerSec) {
    const glm::vec3 velocityBeforeThisStep = velocity;
    glm::vec3 forward = GetForward();

    // Gravity: NOT plain Newtonian 1/r^2 -- that turned out to be
    // completely imperceptible everywhere outside the horizon. Instead
    // this uses the EXACT general-relativistic proper acceleration
    // required for a "static" observer to hover at fixed Schwarzschild
    // radius r:
    //
    //   a(r) = (GM/r^2) / sqrt(1 - r_s/r)
    //
    // (see e.g. MTW "Gravitation" or Hartle's "Gravity", the standard
    // static-observer result for the Schwarzschild metric). Still
    // genuinely integrated into `velocity` every frame, same as always --
    // see FlightPhysics.h's class comment and `cruiseSpeedSimUnitsPerSec`'s
    // own comment for exactly what effect this retains (bending the
    // flight path) versus what the cruise-control redesign below gives up
    // (gravity no longer forces overall SPEED up along a nose-aligned
    // dive -- that component gets directly overwritten by the pilot's set
    // speed a few lines down). Shared with Astronomy::StarBody (see
    // Physics/BlackHole.h) so a falling star still feels the identical law.
    const glm::vec3 gravityAccel(Physics::StaticObserverGravityAccelSim(glm::dvec3(position)));
    velocity += gravityAccel * deltaTime;

    // Cruise control: the pilot's directly-set speed
    // (`cruiseSpeedSimUnitsPerSec`, Shift/Ctrl via
    // InputHandler::ProcessKeyboard, no ceiling of its own) becomes the
    // ship's velocity component ALONG its own nose direction, every
    // frame, immediately -- not a force integrated against drag toward
    // some equilibrium. Decomposing into along-nose and perpendicular
    // parts (rather than just setting `velocity = forward * speed`
    // outright) keeps whatever perpendicular drift gravity just added
    // above intact instead of silently discarding it -- that's the part
    // of "the black hole bends your path" that survives this redesign.
    // Afterburner still multiplies the effective speed by 50%. A negative
    // cruiseSpeedSimUnitsPerSec means reverse -- flying backward along the
    // nose, not just "less forward."
    const float afterburnerMultiplier = afterburner ? 1.5f : 1.0f;
    const float targetAlongNoseSpeed = cruiseSpeedSimUnitsPerSec * afterburnerMultiplier;
    const float currentAlongNoseSpeed = glm::dot(velocity, forward);
    const glm::vec3 perpendicularVelocity = velocity - forward * currentAlongNoseSpeed;
    velocity = forward * targetAlongNoseSpeed + perpendicularVelocity;

    CalculateGForce((velocity - velocityBeforeThisStep) / std::max(deltaTime, 1e-6f));

    // Hard relativistic speed limit -- see FlightPhysics.h's
    // MAX_SPEED_FRACTION_OF_C/MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC. Whatever
    // the pilot's set speed above would otherwise produce, velocity never
    // exceeds this (mass-dependent) cap -- the one bound that ISN'T
    // "infinite" by design, since it's standing in for an actual physical
    // limit (the speed of light), not an arbitrary engine rating.
    const float speed = glm::length(velocity);
    if (speed > maxSpeedSimUnitsPerSec && speed > 1e-6f) {
        velocity *= maxSpeedSimUnitsPerSec / speed;
    }

    position += velocity * deltaTime;
}

void FlightPhysics::UpdateRotation(float deltaTime) {
    float airSpeed = GetSpeed();

    // --- ARCADE FLIGHT MODEL (direct rate control) ---
    // "Ace Combat" style: input directly controls rotation speed. No
    // inertia, no torque, no overshoot -- pure response.
    float authority = 1.0f;

    // COBRA LOGIC: at low speed with hard pitch input, allow full authority
    // (post-stall maneuvering).
    bool isCobra = (airSpeed < 250.0f) && (std::abs(pitchInput) > 0.8f);

    if (!isCobra && airSpeed < 40.0f) {
        authority = std::max(0.0f, (airSpeed - 10.0f) / 30.0f); // linear dropoff below 40 units/s
    }

    float pitchSpeed = 2.5f; // ~140 deg/s
    if (isCobra) pitchSpeed = 4.0f; // ~230 deg/s snap pitch
    float rollSpeed = 5.0f;  // ~280 deg/s
    float yawSpeed = 1.0f;   // ~60 deg/s

    glm::vec3 targetVelocity(0.0f);
    targetVelocity.x = pitchInput * pitchSpeed * authority;
    targetVelocity.y = yawInput * yawSpeed * authority;
    targetVelocity.z = rollInput * rollSpeed * authority;

    // Move (quickly, not instantly) towards the target angular velocity.
    float handling = 10.0f;
    angularVelocity.x = glm::mix(angularVelocity.x, targetVelocity.x, std::min(handling * deltaTime, 1.0f));
    angularVelocity.y = glm::mix(angularVelocity.y, targetVelocity.y, std::min(handling * deltaTime, 1.0f));
    angularVelocity.z = glm::mix(angularVelocity.z, targetVelocity.z, std::min(handling * 1.5f * deltaTime, 1.0f));

    if (glm::length(angularVelocity) < 0.001f) angularVelocity = glm::vec3(0.0f);

    glm::vec3 axis = angularVelocity;
    float angle = glm::length(axis) * deltaTime;
    if (angle > 0.00001f) {
        axis = glm::normalize(axis);
        glm::quat deltaRot = glm::angleAxis(angle, axis);
        rotation = rotation * deltaRot;
        rotation = glm::normalize(rotation);
    }
}

void FlightPhysics::CalculateGForce(const glm::vec3& acceleration) {
    glm::vec3 totalAccel = acceleration + glm::vec3(0.0f, G_REFERENCE, 0.0f);
    currentGForce = glm::length(totalAccel) / G_REFERENCE;
    if (currentGForce > peakGForce) {
        peakGForce = currentGForce;
    }
}

float FlightPhysics::GetSpeed() const {
    return glm::length(velocity);
}

glm::vec3 FlightPhysics::GetForward() const {
    return rotation * glm::vec3(0, 0, -1);
}

glm::vec3 FlightPhysics::GetUp() const {
    return rotation * glm::vec3(0, 1, 0);
}

glm::vec3 FlightPhysics::GetRight() const {
    return rotation * glm::vec3(1, 0, 0);
}

} // namespace Jet
