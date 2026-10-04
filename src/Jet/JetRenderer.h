#pragma once
#include <memory>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "JetMesh.h"
#include "JetShader.h"

// Owns the jet's mesh/shader/texture and knows how to draw it -- the only
// piece of BlackHoleProject that touches modern (GLEW-loaded) OpenGL
// directly. Everything else in this codebase is either the CUDA/CPU ray
// tracer or Rendering::Renderer's legacy fixed-function debug drawing; both
// coexist fine with this in the same GL context (see App.cpp's glewInit
// call) because GLFW's default context here is compatibility profile, not
// core.
namespace Jet {

class JetRenderer {
public:
    // Loads the GLB model + diffuse texture from `modelPath`/`texturePath`
    // (call once, after a GL context + glewInit exist), compiles the
    // shader, and rescales the loaded mesh so its longest axis equals
    // `targetLength` simulation units -- avoids hand-tuning a raw scale
    // factor against a mesh authored in real-world meters. Returns false on
    // failure (e.g. missing files); the caller should just skip drawing.
    bool Load(const std::string& modelPath, const std::string& texturePath, float targetLength);

    // Draws the jet at `position`/`rotation` (simulation units, same
    // convention as Jet::FlightPhysics: rotation's forward is -Z) using the
    // given view/projection matrices -- pass the SAME matrices the ray
    // tracer's camera used this frame (Rendering::Camera::ViewMatrix /
    // ProjectionMatrix) so the jet lines up with the lensed background.
    // `lightDirection` should point FROM the light TOWARD the jet.
    void Draw(const glm::vec3& position, const glm::quat& rotation, const glm::mat4& view, const glm::mat4& projection,
              const glm::vec3& lightDirection) const;

    bool IsLoaded() const { return m_loaded; }

private:
    std::unique_ptr<JetMesh> m_mesh;
    std::unique_ptr<JetShader> m_shader;
    float m_modelScale = 1.0f; // uniform scale applied on top of the raw loaded mesh to hit targetLength
    bool m_loaded = false;
};

} // namespace Jet
