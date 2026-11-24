#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// Scene Editor save/load (see App.cpp's DrawSceneEditorPanelContents):
// captures the subset of scene state the Scene Editor panel exposes --
// Endurance placement/behavior, the light, the accretion disk's color/
// glare, the black hole's mass/spin, and spawned stars'/planets' position/
// color -- so a hand-tuned scene can be written to disk and reloaded later.
// Not a full save-game: physics state (velocities, particle-level detail,
// camera pose, etc.) is intentionally out of scope, matching what the Scene
// Editor panel itself lets you edit.
namespace Application {

struct SceneObjectState {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
};

struct SceneState {
    // Endurance -- shipMode mirrors App.cpp's ShipSceneMode enum
    // (0 = Piloted, 1 = Parked, 2 = Orbiting) without depending on it
    // directly, so this header doesn't need to include App.cpp's state.
    int shipMode = 0;
    glm::vec3 shipParkedPosition{0.0f};
    glm::vec3 shipParkedEulerDegrees{0.0f};
    double shipOrbitRadius = 40.0;
    double shipOrbitSpeedRadPerSec = 0.15;
    double shipOrbitInclinationDegrees = 15.0;
    bool shipOrbitAnimated = false;
    double shipOrbitPhase = 0.0;

    // Light -- lightMode mirrors App.cpp's SceneLightMode (0 = Auto, 1 = Custom)
    int lightMode = 0;
    float lightAzimuthDegrees = 0.0f;
    float lightElevationDegrees = 30.0f;
    glm::vec3 lightColor{1.0f};
    float lightIntensity = 1.0f;

    // Accretion disk
    double diskTemperatureScale = 1.0;
    double diskBrightnessScale = 1.0;
    bool diskMovieAccurateColors = false;
    float diskGlareIntensity = 0.0f;
    float diskGlareRadius = 0.05f;

    // Black hole
    double blackHoleMassSolar = 10.0;
    double blackHoleSpin = 0.0;

    std::vector<SceneObjectState> stars;
    std::vector<SceneObjectState> planets;
};

// Writes `state` to `path` as human-readable JSON, creating any missing
// parent directory first. Returns false on failure (e.g. an unwritable
// path), leaving any existing file at `path` untouched.
bool SaveScene(const std::string& path, const SceneState& state);

// Reads a scene previously written by SaveScene. Returns std::nullopt on
// any failure (missing file, malformed JSON) rather than throwing --
// callers should leave the current scene untouched when this fails.
std::optional<SceneState> LoadScene(const std::string& path);

} // namespace Application
