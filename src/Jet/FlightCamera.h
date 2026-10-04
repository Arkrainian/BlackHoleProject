#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Ported from the "jet system new" project's Camera class (chase/cockpit
// smoothing logic, unchanged) -- renamed to avoid colliding with
// Rendering::Camera, which this feeds into (see App.cpp: each frame this
// class's position/target/up are copied into Rendering::Camera's Jet mode
// fields, so the ray tracer and debug renderer see the jet's view through
// the exact same Camera/CameraFrame path every other mode already uses).
namespace Jet {

enum class FlightCameraMode {
    Chase,   // third-person chase camera
    Cockpit, // first-person cockpit view
};

class FlightCamera {
public:
    FlightCameraMode mode = FlightCameraMode::Chase;
    glm::vec3 position;
    glm::vec3 target;
    glm::vec3 up;

    float chaseDistance = 3.0f;
    float chaseHeight = 1.0f;
    float smoothness = 0.9f;

    FlightCamera();

    void Update(const glm::vec3& jetPos, const glm::quat& jetRot, float deltaTime);
    void CycleMode();

private:
    void UpdateChaseCamera(const glm::vec3& jetPos, const glm::quat& jetRot, float deltaTime);
    void UpdateCockpitCamera(const glm::vec3& jetPos, const glm::quat& jetRot);

    glm::vec3 smoothPosition;
    glm::vec3 smoothTarget;
};

} // namespace Jet
