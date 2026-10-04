#include "FlightCamera.h"

#include <algorithm>
#include <cmath>

namespace Jet {

FlightCamera::FlightCamera()
    : position(0.0f, 1.0f, 3.0f)
    , target(0.0f)
    , up(0.0f, 1.0f, 0.0f)
    , smoothPosition(0.0f, 1.0f, 3.0f)
    , smoothTarget(0.0f)
{}

void FlightCamera::Update(const glm::vec3& jetPos, const glm::quat& jetRot, float deltaTime) {
    switch (mode) {
        case FlightCameraMode::Chase:
            UpdateChaseCamera(jetPos, jetRot, deltaTime);
            break;
        case FlightCameraMode::Cockpit:
            UpdateCockpitCamera(jetPos, jetRot);
            break;
    }
}

void FlightCamera::CycleMode() {
    mode = (mode == FlightCameraMode::Chase) ? FlightCameraMode::Cockpit : FlightCameraMode::Chase;
}

void FlightCamera::UpdateChaseCamera(const glm::vec3& jetPos, const glm::quat& jetRot, float deltaTime) {
    glm::vec3 forward = jetRot * glm::vec3(0, 0, -1);
    glm::vec3 jetUp = jetRot * glm::vec3(0, 1, 0);

    glm::vec3 targetPos = jetPos - (forward * chaseDistance) + (jetUp * chaseHeight);
    glm::vec3 targetLookAt = jetPos + (forward * 50.0f);

    // Dynamic rubber-banding: interpolation speed increases with lag instead
    // of a hard clamp, so the camera keeps up during hard maneuvers without
    // feeling twitchy in normal flight.
    float distToTarget = glm::distance(smoothPosition, targetPos);
    float baseAlpha = 1.0f - std::pow(smoothness, deltaTime * 60.0f);
    float dynamicAlpha = baseAlpha;
    if (distToTarget > 10.0f) {
        float lagFactor = (distToTarget - 10.0f) / 50.0f;
        dynamicAlpha = glm::mix(baseAlpha, 1.0f, glm::clamp(lagFactor, 0.0f, 1.0f));
    }

    smoothPosition = glm::mix(smoothPosition, targetPos, dynamicAlpha);
    smoothTarget = glm::mix(smoothTarget, targetLookAt, dynamicAlpha);

    position = smoothPosition;
    target = smoothTarget;
    up = jetUp; // camera rolls with the jet
}

void FlightCamera::UpdateCockpitCamera(const glm::vec3& jetPos, const glm::quat& jetRot) {
    glm::vec3 forward = jetRot * glm::vec3(0, 0, -1);
    glm::vec3 jetUp = jetRot * glm::vec3(0, 1, 0);

    position = jetPos + jetUp * 0.6f + forward * 0.8f;
    target = position + forward * 100.0f;
    up = jetUp;
}

} // namespace Jet
