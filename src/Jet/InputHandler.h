#pragma once
#include <GLFW/glfw3.h>

#include "FlightPhysics.h"

// Ported verbatim (self-contained, no engine coupling beyond GLFW key
// polling and FlightPhysics) from the "jet system new" project's
// InputHandler. Combines keyboard (WASD pitch/roll, Q/E yaw) with a
// decaying mouse "virtual joystick" for pitch/roll, smoothed for a
// frame-rate-independent, non-twitchy feel.
namespace Jet {

class InputHandler {
public:
    float mouseSensitivity;
    float keyboardSensitivity;
    float inputSmoothing; // 0.0 = instant, 1.0 = very smooth

    InputHandler();

    void ProcessKeyboard(GLFWwindow* window, FlightPhysics& physics, float deltaTime);
    void ProcessMouse(float xoffset, float yoffset, FlightPhysics& physics, float deltaTime);

private:
    float smoothedPitch;
    float smoothedRoll;
    float smoothedYaw;

    float mousePitchAccumulator;
    float mouseRollAccumulator;

    float SmoothInput(float current, float target, float smoothing, float deltaTime);
};

} // namespace Jet
