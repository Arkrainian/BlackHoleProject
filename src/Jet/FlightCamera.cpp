#include "FlightCamera.h"

namespace Jet {

FlightCamera::FlightCamera()
    : position(0.0f, 1.0f, 3.0f)
    , target(0.0f)
    , up(0.0f, 1.0f, 0.0f)
{}

void FlightCamera::Update(const glm::vec3& jetPos, const glm::quat& jetRot) {
    switch (mode) {
        case FlightCameraMode::Chase:
            UpdateChaseCamera(jetPos, jetRot);
            break;
        case FlightCameraMode::Cockpit:
            UpdateCockpitCamera(jetPos, jetRot);
            break;
    }
}

void FlightCamera::CycleMode() {
    mode = (mode == FlightCameraMode::Chase) ? FlightCameraMode::Cockpit : FlightCameraMode::Chase;
}

void FlightCamera::UpdateChaseCamera(const glm::vec3& jetPos, const glm::quat& jetRot) {
    // Rigidly attached to the ship -- no smoothing/lag of any kind. An
    // earlier version here interpolated toward this same offset with a
    // "dynamic rubber-banding" alpha that sped up the more the camera had
    // fallen behind (`distance(smoothPosition, targetPos)`); that distance
    // -- and so the camera's actual responsiveness -- was itself a
    // function of how fast the ship was moving (a slow or stationary ship
    // barely displaces frame to frame, so the camera sat in the SLOW
    // smoothing regime and visibly lagged/drifted away from the correct
    // chase offset, e.g. ending up noticeably above the ship instead of
    // snapped to its actual chase position) -- reported directly: "the
    // camera should stay with the Endurance regardless of the speed...
    // when the speed is too low it goes above the Endurance." Fixed by
    // removing the lag entirely rather than re-tuning it again: the
    // camera is simply placed at the ship's current chase offset every
    // single frame, so it holds its position relative to the ship
    // identically whether the ship is standing still, cruising, or
    // reversing.
    glm::vec3 forward = jetRot * glm::vec3(0, 0, -1);
    glm::vec3 jetUp = jetRot * glm::vec3(0, 1, 0);

    position = jetPos - (forward * chaseDistance) + (jetUp * chaseHeight);
    target = jetPos + (forward * 50.0f);
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
