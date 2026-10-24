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

    // Thrust: Shift = increase, Left/Right Ctrl = decrease (and past zero,
    // genuine reverse thrust -- see REVERSE_THRUST_FRACTION), W+Shift =
    // afterburner. Previously Space decreased thrust with a floor at zero
    // (no reverse at all); moved to Ctrl specifically so reverse has its
    // own dedicated key, not a repurposed "idle" key.
    bool shiftPressed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                          glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
    bool ctrlPressed = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                         glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
    bool wPressed = (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS);

    if (shiftPressed) {
        physics.thrust += 50000.0f * deltaTime;
        physics.afterburner = wPressed;
    } else {
        physics.afterburner = false;
    }

    if (ctrlPressed) {
        physics.thrust -= 50000.0f * deltaTime;
    }

    physics.thrust = glm::clamp(physics.thrust, -MAX_THRUST * REVERSE_THRUST_FRACTION, MAX_THRUST);
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
