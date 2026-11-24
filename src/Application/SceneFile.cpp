#include "SceneFile.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace Application {

namespace {

nlohmann::json Vec3ToJson(const glm::vec3& v) {
    return {v.x, v.y, v.z};
}

glm::vec3 Vec3FromJson(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() != 3) return fallback;
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

nlohmann::json ObjectToJson(const SceneObjectState& obj) {
    return {{"position", Vec3ToJson(obj.position)}, {"color", Vec3ToJson(obj.color)}};
}

SceneObjectState ObjectFromJson(const nlohmann::json& j) {
    SceneObjectState obj;
    if (j.contains("position")) obj.position = Vec3FromJson(j["position"], obj.position);
    if (j.contains("color")) obj.color = Vec3FromJson(j["color"], obj.color);
    return obj;
}

} // namespace

bool SaveScene(const std::string& path, const SceneState& state) {
    nlohmann::json j;
    j["shipMode"] = state.shipMode;
    j["shipParkedPosition"] = Vec3ToJson(state.shipParkedPosition);
    j["shipParkedEulerDegrees"] = Vec3ToJson(state.shipParkedEulerDegrees);
    j["shipOrbitRadius"] = state.shipOrbitRadius;
    j["shipOrbitSpeedRadPerSec"] = state.shipOrbitSpeedRadPerSec;
    j["shipOrbitInclinationDegrees"] = state.shipOrbitInclinationDegrees;
    j["shipOrbitAnimated"] = state.shipOrbitAnimated;
    j["shipOrbitPhase"] = state.shipOrbitPhase;

    j["lightMode"] = state.lightMode;
    j["lightAzimuthDegrees"] = state.lightAzimuthDegrees;
    j["lightElevationDegrees"] = state.lightElevationDegrees;
    j["lightColor"] = Vec3ToJson(state.lightColor);
    j["lightIntensity"] = state.lightIntensity;

    j["diskTemperatureScale"] = state.diskTemperatureScale;
    j["diskBrightnessScale"] = state.diskBrightnessScale;
    j["diskMovieAccurateColors"] = state.diskMovieAccurateColors;
    j["diskGlareIntensity"] = state.diskGlareIntensity;
    j["diskGlareRadius"] = state.diskGlareRadius;

    j["blackHoleMassSolar"] = state.blackHoleMassSolar;
    j["blackHoleSpin"] = state.blackHoleSpin;

    nlohmann::json stars = nlohmann::json::array();
    for (const SceneObjectState& star : state.stars) stars.push_back(ObjectToJson(star));
    j["stars"] = stars;

    nlohmann::json planets = nlohmann::json::array();
    for (const SceneObjectState& planet : state.planets) planets.push_back(ObjectToJson(planet));
    j["planets"] = planets;

    const std::filesystem::path filePath(path);
    std::error_code ec;
    if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path(), ec);
    }

    std::ofstream out(filePath);
    if (!out) return false;
    out << j.dump(2);
    return out.good();
}

std::optional<SceneState> LoadScene(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;

    nlohmann::json j;
    try {
        in >> j;
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }

    SceneState state;
    try {
        state.shipMode = j.value("shipMode", state.shipMode);
        if (j.contains("shipParkedPosition")) state.shipParkedPosition = Vec3FromJson(j["shipParkedPosition"], state.shipParkedPosition);
        if (j.contains("shipParkedEulerDegrees")) state.shipParkedEulerDegrees = Vec3FromJson(j["shipParkedEulerDegrees"], state.shipParkedEulerDegrees);
        state.shipOrbitRadius = j.value("shipOrbitRadius", state.shipOrbitRadius);
        state.shipOrbitSpeedRadPerSec = j.value("shipOrbitSpeedRadPerSec", state.shipOrbitSpeedRadPerSec);
        state.shipOrbitInclinationDegrees = j.value("shipOrbitInclinationDegrees", state.shipOrbitInclinationDegrees);
        state.shipOrbitAnimated = j.value("shipOrbitAnimated", state.shipOrbitAnimated);
        state.shipOrbitPhase = j.value("shipOrbitPhase", state.shipOrbitPhase);

        state.lightMode = j.value("lightMode", state.lightMode);
        state.lightAzimuthDegrees = j.value("lightAzimuthDegrees", state.lightAzimuthDegrees);
        state.lightElevationDegrees = j.value("lightElevationDegrees", state.lightElevationDegrees);
        if (j.contains("lightColor")) state.lightColor = Vec3FromJson(j["lightColor"], state.lightColor);
        state.lightIntensity = j.value("lightIntensity", state.lightIntensity);

        state.diskTemperatureScale = j.value("diskTemperatureScale", state.diskTemperatureScale);
        state.diskBrightnessScale = j.value("diskBrightnessScale", state.diskBrightnessScale);
        state.diskMovieAccurateColors = j.value("diskMovieAccurateColors", state.diskMovieAccurateColors);
        state.diskGlareIntensity = j.value("diskGlareIntensity", state.diskGlareIntensity);
        state.diskGlareRadius = j.value("diskGlareRadius", state.diskGlareRadius);

        state.blackHoleMassSolar = j.value("blackHoleMassSolar", state.blackHoleMassSolar);
        state.blackHoleSpin = j.value("blackHoleSpin", state.blackHoleSpin);

        if (j.contains("stars")) {
            for (const auto& starJson : j["stars"]) state.stars.push_back(ObjectFromJson(starJson));
        }
        if (j.contains("planets")) {
            for (const auto& planetJson : j["planets"]) state.planets.push_back(ObjectFromJson(planetJson));
        }
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }

    return state;
}

} // namespace Application
