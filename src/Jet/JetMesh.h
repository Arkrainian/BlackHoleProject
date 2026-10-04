#pragma once
#include <GL/glew.h>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

#include "JetShader.h"

// Trimmed port of the "jet system new" project's Mesh class: static
// (non-instanced, non-scene-graph) geometry with a single diffuse texture,
// which is all a standalone jet model needs. Instancing, LoadScene (a whole
// city's worth of textured parts), and the primitive-generator helpers from
// the source project aren't needed here and were dropped.
namespace Jet {

struct JetVertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
};

class JetMesh {
public:
    std::vector<JetVertex> vertices;
    std::vector<unsigned int> indices;
    GLuint diffuseTexture = 0; // set by the caller after LoadGLB, see JetRenderer
    GLuint VAO = 0, VBO = 0, EBO = 0;

    JetMesh() = default;
    ~JetMesh();

    void SetupMesh();
    void Draw(JetShader& shader);

    // Parses the first mesh found in a .glb file (this codebase only ever
    // loads single-model files, not multi-part scenes) into `mesh`,
    // uniformly scaled by `scale`. Returns false on parse/read failure.
    static bool LoadGLB(const char* path, JetMesh& mesh, float scale = 1.0f);

    // Computes the axis-aligned bounding box of `vertices`, before
    // SetupMesh() uploads them -- used by the caller to normalize the
    // model's size into simulation units without hand-tuning a scale.
    void ComputeBounds(glm::vec3& outMin, glm::vec3& outMax) const;
};

} // namespace Jet
