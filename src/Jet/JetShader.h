#pragma once
#include <GL/glew.h>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

// Trimmed port of the "jet system new" project's Shader class (only the
// uniform setters the jet's simple diffuse+Lambertian shader actually
// needs -- see JetRenderer.cpp for the GLSL source, a compact replacement
// for that project's much larger PBR shader, which needs tangent-space
// normal mapping this codebase's Vertex layout doesn't provide anyway).
namespace Jet {

class JetShader {
public:
    GLuint ID = 0;

    JetShader(const char* vertexSource, const char* fragmentSource);
    ~JetShader();

    void Activate();

    void SetInt(const std::string& name, int value);
    void SetFloat(const std::string& name, float value);
    void SetVec3(const std::string& name, const glm::vec3& value);
    void SetMat4(const std::string& name, const glm::mat4& value);

private:
    void CheckCompileErrors(GLuint shader, const std::string& type);
};

} // namespace Jet
