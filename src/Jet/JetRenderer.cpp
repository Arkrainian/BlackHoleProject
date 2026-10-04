#include "JetRenderer.h"

#include <algorithm>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

#include <stb_image.h>

namespace Jet {

namespace {

// Compact diffuse+Lambertian shader -- a deliberate replacement for the
// source project's much larger PBR shader (metallic/roughness/AO/normal
// mapping), which needs tangent vectors this codebase's Vertex layout
// doesn't carry. One diffuse texture plus a simple directional light reads
// perfectly well for a small ship seen against a black hole.
const char* kVertexShader = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    // Uniform scale + rotation only (no shear), so transforming the normal
    // by mat3(model) directly is correct -- no inverse-transpose needed.
    vNormal = normalize(mat3(model) * aNormal);
    vTexCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)glsl";

const char* kFragmentShader = R"glsl(
#version 330 core
in vec3 vNormal;
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D diffuseMap;
uniform vec3 lightDirection; // points FROM the light TOWARD the surface

void main() {
    vec3 normal = normalize(vNormal);
    float diffuse = max(dot(normal, -lightDirection), 0.0);
    float lighting = 0.25 + 0.75 * diffuse; // ambient floor + Lambertian term
    vec3 albedo = texture(diffuseMap, vTexCoord).rgb;
    FragColor = vec4(albedo * lighting, 1.0);
}
)glsl";

GLuint LoadDiffuseTexture(const std::string& path) {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 3);
    if (!data) {
        std::fprintf(stderr, "Jet::JetRenderer: failed to load texture '%s': %s\n", path.c_str(), stbi_failure_reason());
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D); // GLEW makes this available, unlike Rendering::Renderer's legacy-only path
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return texture;
}

} // namespace

bool JetRenderer::Load(const std::string& modelPath, const std::string& texturePath, float targetLength) {
    m_mesh = std::make_unique<JetMesh>();
    if (!JetMesh::LoadGLB(modelPath.c_str(), *m_mesh, 1.0f) || m_mesh->vertices.empty()) {
        std::fprintf(stderr, "Jet::JetRenderer: failed to load model '%s'\n", modelPath.c_str());
        m_mesh.reset();
        return false;
    }

    glm::vec3 boundsMin, boundsMax;
    m_mesh->ComputeBounds(boundsMin, boundsMax);
    const glm::vec3 extent = boundsMax - boundsMin;
    const float longestAxis = std::max({extent.x, extent.y, extent.z});
    m_modelScale = (longestAxis > 1e-6f) ? (targetLength / longestAxis) : 1.0f;

    const GLuint texture = LoadDiffuseTexture(texturePath);
    if (texture == 0) {
        m_mesh.reset();
        return false;
    }
    m_mesh->diffuseTexture = texture;
    m_mesh->SetupMesh();

    m_shader = std::make_unique<JetShader>(kVertexShader, kFragmentShader);

    m_loaded = true;
    return true;
}

void JetRenderer::Draw(const glm::vec3& position, const glm::quat& rotation, const glm::mat4& view,
                       const glm::mat4& projection, const glm::vec3& lightDirection) const {
    if (!m_loaded) return;

    // Same fixup rotation the source project applies at draw time to align
    // this specific GLB's authored axes with FlightPhysics's forward = -Z
    // convention (GetForward() = rotation * (0,0,-1)) -- see Jet system
    // new's Nahidwin.cpp jetModel construction.
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    model *= glm::mat4_cast(rotation);
    model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
    model = glm::rotate(model, glm::radians(270.0f), glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(m_modelScale));

    m_shader->Activate();
    m_shader->SetMat4("model", model);
    m_shader->SetMat4("view", view);
    m_shader->SetMat4("projection", projection);
    m_shader->SetVec3("lightDirection", glm::normalize(lightDirection));

    // The jet's thin single-sided panels (flaps, canopy frame, pylons) look
    // wrong with back-face culling on, same reasoning as the source
    // project's jet draw calls.
    glDisable(GL_CULL_FACE);
    m_mesh->Draw(*m_shader);

    // Un-bind our shader program and this mesh's texture unit -- while any
    // GLSL program is active, it overrides the ENTIRE fixed-function
    // pipeline, including Rendering::Renderer's legacy glBegin/glTexCoord2f
    // calls (the fullscreen ray-traced quad, the debug grid/spheres). Left
    // bound, the frame drawn immediately after this one silently ran the
    // background blit through this jet shader instead of fixed-function
    // texturing -- wrong vertex attributes entirely, which is why the black
    // hole/disk appeared to vanish the moment the jet was first drawn.
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace Jet
