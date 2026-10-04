#include "FlightPhysics.h"

#include <algorithm>
#include <cmath>

#include "Physics/BlackHole.h"

namespace Jet {

FlightPhysics::FlightPhysics(const glm::vec3& spawnPosition)
    : position(spawnPosition)
    , velocity(0.0f, 0.0f, 0.0f)
    , rotation(glm::identity<glm::quat>())
    , angularVelocity(0.0f)
    , thrust(MAX_THRUST * 0.5f)
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
    thrust = MAX_THRUST * 0.5f;
    currentGForce = 1.0f;
    peakGForce = 1.0f;
    capturedByBlackHole = false;
}

void FlightPhysics::Update(float deltaTime) {
    UpdateForces(deltaTime);
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

void FlightPhysics::UpdateForces(float deltaTime) {
    float airSpeed = glm::length(velocity);

    // No atmosphere in orbit around a black hole -- nominal sea-level-
    // equivalent density everywhere, purely to keep the arcade thrust/drag
    // feel from the source project unchanged (see FlightPhysics.h header).
    const float airDensity = 1.225f;

    glm::vec3 forward = GetForward();
    glm::vec3 velocityDir = (airSpeed > 0.1f) ? glm::normalize(velocity) : forward;
    float aoaDot = glm::dot(forward, velocityDir);
    float aoaFactor = glm::clamp(aoaDot, 0.0f, 1.0f);

    // --- FORCES (Newtons, except gravity which is applied as a direct ---
    // acceleration -- see below) ---

    // 1. Gravity: NOT plain Newtonian 1/r^2 -- that turned out to be
    // completely imperceptible everywhere outside the horizon (at r=2*r_s,
    // GM/r^2 is only ~0.06 sim units/s^2, versus ~130 units/s^2 of thrust
    // acceleration, a 2000x mismatch that meant the black hole could never
    // actually threaten the jet no matter how close it got). Instead this
    // uses the EXACT general-relativistic proper acceleration required for
    // a "static" observer to hover at fixed Schwarzschild radius r:
    //
    //   a(r) = (GM/r^2) / sqrt(1 - r_s/r)
    //
    // (see e.g. MTW "Gravitation" or Hartle's "Gravity", the standard
    // static-observer result for the Schwarzschild metric). It reduces to
    // ordinary Newtonian gravity far away (the 1/sqrt(1-r_s/r) factor -> 1),
    // matching the GM=1 convention used everywhere else in this sim, but
    // DIVERGES as r -> r_s -- correctly modeling that no amount of thrust
    // can hold station arbitrarily close to the horizon. In practice this
    // stays negligible while cruising (r ~ 100s of units) and only becomes
    // a real, escalating threat once the jet dives in toward the accretion
    // disk/photon sphere -- exactly where it should.
    glm::vec3 gravityAccel(0.0f);
    {
        const glm::dvec3 dpos(position);
        const double r = glm::length(dpos);
        const double rs = Physics::BlackHole::SchwarzschildRadiusSim();
        if (r > 1e-6) {
            const double newtonTerm = 1.0 / (r * r); // GM=1
            // Clamped, not left to blow up to infinity, purely so one large
            // timestep right at the horizon can't inject NaN/inf into
            // velocity before FlightPhysics::Update's capture check (next
            // frame) has a chance to reset the flight.
            const double relativisticFactor = 1.0 / std::sqrt(std::max(1e-4, 1.0 - rs / r));
            const double magnitude = std::min(newtonTerm * relativisticFactor, 5000.0);
            gravityAccel = glm::vec3(-glm::normalize(dpos) * magnitude);
        }
    }

    // 2. Thrust. Afterburner adds 50% more.
    float thrustMultiplier = afterburner ? 1.5f : 1.0f;
    glm::vec3 F_thrust = forward * thrust * thrustMultiplier;

    // 3. Drag (with compressibility effects).
    float mach = GetMachNumber();
    float dragCoeff = GetDragCoefficient(mach);
    float inducedDragCoeff = 0.5f * (1.0f - aoaFactor);

    // ARCADE: heavy drag when turning hard ("air brake" / cobra effect).
    float turnDrag = glm::length(angularVelocity) * 2.0f;

    float totalDrag = 0.5f * airDensity * (airSpeed * airSpeed) * (dragCoeff + inducedDragCoeff + turnDrag);
    glm::vec3 F_drag = -velocityDir * totalDrag;

    // 4. Lift: intentionally omitted (arcade mode) -- see the "Turn"
    // correction below, which aligns velocity with the nose instead.

    // --- INTEGRATION (F = ma, gravity added directly as an acceleration) ---
    glm::vec3 totalForce = F_thrust + F_drag;
    glm::vec3 acceleration = totalForce / JET_MASS + gravityAccel;

    CalculateGForce(acceleration);

    velocity += acceleration * deltaTime;

    // Aerodynamic "turn" correction -- velocity aligns with the nose.
    if (airSpeed > 10.0f) {
        float turnAgility = 2.0f * deltaTime;
        glm::vec3 desiredVel = forward * airSpeed;
        velocity = glm::mix(velocity, desiredVel, turnAgility);
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

float FlightPhysics::GetMachNumber() const {
    return GetSpeed() / MACH_1;
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

float FlightPhysics::GetDragCoefficient(float mach) const {
    float baseDrag = 0.02f;
    if (mach > 0.8f && mach < 1.2f) {
        float transonicFactor = 1.0f + 5.0f * std::pow(mach - 0.8f, 2.0f);
        return baseDrag * transonicFactor;
    } else if (mach >= 1.2f) {
        return baseDrag * 3.0f;
    }
    return baseDrag;
}

} // namespace Jet
