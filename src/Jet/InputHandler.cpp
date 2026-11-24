#include "InputHandler.h"

#include <algorithm>
#include <cmath>

namespace Jet {

InputHandler::InputHandler()
    : mouseSensitivity(0.03f)
    , keyboardSensitivity(1.0f)
    , inputSmoothing(0.92f)
    , smoothedPitch(0.0f)
    , smoothedRoll(0.0f)
    , smoothedYaw(0.0f)
    , mousePitchAccumulator(0.0f)
    , mouseRollAccumulator(0.0f)
{}

void InputHandler::ProcessKeyboard(GLFWwindow* window, FlightPhysics& physics, float deltaTime) {
    float keyPitch = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) keyPitch = 1.0f;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) keyPitch = -1.0f;

    float keyRoll = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) keyRoll = 1.0f;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) keyRoll = -1.0f;

    float keyYaw = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) keyYaw = 1.0f;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) keyYaw = -1.0f;

    keyPitch *= keyboardSensitivity;
    keyRoll *= keyboardSensitivity;
    keyYaw *= keyboardSensitivity;

    // Decay mouse accumulators (auto-center virtual joystick).
    float decaySpeed = 2.0f;
    float decayFactor = 1.0f - std::min(decaySpeed * deltaTime, 1.0f);
    mousePitchAccumulator *= decayFactor;
    mouseRollAccumulator *= decayFactor;
    if (std::abs(mousePitchAccumulator) < 0.001f) mousePitchAccumulator = 0.0f;
    if (std::abs(mouseRollAccumulator) < 0.001f) mouseRollAccumulator = 0.0f;

    float targetPitch = std::clamp(keyPitch + mousePitchAccumulator, -1.0f, 1.0f);
    float targetRoll = std::clamp(keyRoll + mouseRollAccumulator, -1.0f, 1.0f);
    float targetYaw = keyYaw;

    smoothedPitch = SmoothInput(smoothedPitch, targetPitch, inputSmoothing, deltaTime);
    smoothedRoll = SmoothInput(smoothedRoll, targetRoll, inputSmoothing, deltaTime);
    smoothedYaw = SmoothInput(smoothedYaw, targetYaw, inputSmoothing, deltaTime);

    physics.pitchInput = smoothedPitch;
    physics.rollInput = smoothedRoll;
    physics.yawInput = smoothedYaw;

    // Cruise speed: Shift raises it, Ctrl lowers it, W+Shift adds the
    // afterburner multiplier. No ceiling on the forward side -- holding
    // Shift keeps raising it for as long as it's held, with no
    // engine-rating limit to plateau against (explicitly requested:
    // "shift or ctrl infinitely reduces/increases the speed/throttle").
    // The forward side's only bound on the ship's actual resulting
    // velocity is the honest relativistic cap applied in
    // FlightPhysics::Update, not anything here. Ctrl, however, IS floored
    // at exactly 0 -- reverse flight is deliberately disabled (explicitly
    // requested: "make it so i can not reverse"). Floored here, not in
    // FlightPhysics, so Ctrl simply decelerates to a stop and holds there
    // rather than overshooting into a negative value that would fly the
    // ship backward.
    bool shiftPressed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                          glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
    bool ctrlPressed = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                         glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
    bool wPressed = (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS);

    if (shiftPressed) {
        physics.cruiseSpeedSimUnitsPerSec += CRUISE_SPEED_RAMP_RATE * deltaTime;
        physics.afterburner = wPressed;
    } else {
        physics.afterburner = false;
    }

    if (ctrlPressed) {
        physics.cruiseSpeedSimUnitsPerSec -= CRUISE_SPEED_RAMP_RATE * deltaTime;
    }

    physics.cruiseSpeedSimUnitsPerSec = std::max(physics.cruiseSpeedSimUnitsPerSec, 0.0f);
}

void InputHandler::ProcessMouse(float xoffset, float yoffset, FlightPhysics& physics, float deltaTime) {
    (void)physics;
    (void)deltaTime;
    float sens = mouseSensitivity;

    // Mouse down -> pitch down (matches the source project's convention).
    mousePitchAccumulator -= yoffset * sens;
    mouseRollAccumulator += xoffset * sens;

    mousePitchAccumulator = std::clamp(mousePitchAccumulator, -1.5f, 1.5f);
    mouseRollAccumulator = std::clamp(mouseRollAccumulator, -1.5f, 1.5f);
}

float InputHandler::SmoothInput(float current, float target, float smoothing, float deltaTime) {
    float alpha = 1.0f - std::pow(smoothing, deltaTime * 60.0f); // frame-rate independent
    return current + (target - current) * alpha;
}

} // namespace Jet
