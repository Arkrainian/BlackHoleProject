#include "NewtonianIntegrator.h"

#include <cmath>

namespace Physics {

glm::dvec3 NewtonianAcceleration(const glm::dvec3& position) {
    const double r = glm::length(position);
    if (r < 1e-9) {
        return glm::dvec3(0.0); // avoid division blow-up if a particle reaches the origin
    }
    return -position / (r * r * r);
}

namespace {

void StepEuler(ParticleState& s, double dt) {
    const glm::dvec3 a = NewtonianAcceleration(s.position);
    s.position += s.velocity * dt;
    s.velocity += a * dt;
}

void StepVelocityVerlet(ParticleState& s, double dt) {
    const glm::dvec3 a0 = NewtonianAcceleration(s.position);
    s.position += s.velocity * dt + 0.5 * a0 * dt * dt;
    const glm::dvec3 a1 = NewtonianAcceleration(s.position);
    s.velocity += 0.5 * (a0 + a1) * dt;
}

void StepRK4(ParticleState& s, double dt) {
    struct Derivative {
        glm::dvec3 dPos;
        glm::dvec3 dVel;
    };

    auto eval = [](const glm::dvec3& pos, const glm::dvec3& vel) -> Derivative {
        return {vel, NewtonianAcceleration(pos)};
    };

    const Derivative k1 = eval(s.position, s.velocity);
    const Derivative k2 = eval(s.position + 0.5 * dt * k1.dPos, s.velocity + 0.5 * dt * k1.dVel);
    const Derivative k3 = eval(s.position + 0.5 * dt * k2.dPos, s.velocity + 0.5 * dt * k2.dVel);
    const Derivative k4 = eval(s.position + dt * k3.dPos, s.velocity + dt * k3.dVel);

    s.position += (dt / 6.0) * (k1.dPos + 2.0 * k2.dPos + 2.0 * k3.dPos + k4.dPos);
    s.velocity += (dt / 6.0) * (k1.dVel + 2.0 * k2.dVel + 2.0 * k3.dVel + k4.dVel);
}

} // namespace

void IntegrateStep(ParticleState& state, double dt, IntegratorMethod method) {
    switch (method) {
        case IntegratorMethod::Euler: StepEuler(state, dt); return;
        case IntegratorMethod::VelocityVerlet: StepVelocityVerlet(state, dt); return;
        case IntegratorMethod::RK4: StepRK4(state, dt); return;
    }
}

} // namespace Physics
