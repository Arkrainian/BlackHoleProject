#include "JetMesh.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cfloat>
#include <iostream>

namespace Jet {

JetMesh::~JetMesh() {
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (EBO) glDeleteBuffers(1, &EBO);
}

void JetMesh::SetupMesh() {
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(JetVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(JetVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(JetVertex), (void*)offsetof(JetVertex, Normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(JetVertex), (void*)offsetof(JetVertex, TexCoords));

    glBindVertexArray(0);
}

void JetMesh::Draw(JetShader& shader) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, diffuseTexture);
    shader.SetInt("diffuseMap", 0);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void JetMesh::ComputeBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = glm::vec3(FLT_MAX);
    outMax = glm::vec3(-FLT_MAX);
    for (const JetVertex& v : vertices) {
        outMin = glm::min(outMin, v.Position);
        outMax = glm::max(outMax, v.Position);
    }
}

bool JetMesh::LoadGLB(const char* path, JetMesh& mesh, float scale) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success) {
        std::cerr << "Failed to parse GLB file: " << path << std::endl;
        return false;
    }

    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        std::cerr << "Failed to load GLB buffers: " << path << std::endl;
        cgltf_free(data);
        return false;
    }

    // Assumes a single mesh in the file (true for the standalone jet model
    // this loads) -- unlike the source project's LoadScene, this does not
    // walk the node graph for a multi-part city/scene.
    if (data->meshes_count > 0) {
        cgltf_mesh* gltfMesh = &data->meshes[0];
        for (size_t i = 0; i < gltfMesh->primitives_count; ++i) {
            cgltf_primitive* prim = &gltfMesh->primitives[i];

            cgltf_accessor* posAcc = nullptr;
            cgltf_accessor* normAcc = nullptr;
            cgltf_accessor* texAcc = nullptr;
            for (size_t k = 0; k < prim->attributes_count; ++k) {
                if (prim->attributes[k].type == cgltf_attribute_type_position) posAcc = prim->attributes[k].data;
                if (prim->attributes[k].type == cgltf_attribute_type_normal) normAcc = prim->attributes[k].data;
                if (prim->attributes[k].type == cgltf_attribute_type_texcoord) texAcc = prim->attributes[k].data;
            }

            size_t vertCount = posAcc ? posAcc->count : 0;
            std::vector<glm::vec3> tempPos(vertCount);
            std::vector<glm::vec3> tempNorm(vertCount);
            std::vector<glm::vec2> tempTex(vertCount);

            if (posAcc) {
                for (size_t k = 0; k < vertCount; ++k) {
                    cgltf_accessor_read_float(posAcc, k, &tempPos[k].x, 3);
                    tempPos[k] *= scale;
                }
            }
            if (normAcc) {
                for (size_t k = 0; k < vertCount; ++k) {
                    cgltf_accessor_read_float(normAcc, k, &tempNorm[k].x, 3);
                }
            }
            if (texAcc) {
                for (size_t k = 0; k < vertCount; ++k) {
                    cgltf_accessor_read_float(texAcc, k, &tempTex[k].x, 2);
                    // glTF UVs are top-left origin; textures loaded via
                    // stb_image with flip-on-load put V=0 at the bottom, so
                    // flip here once to keep the two conventions consistent.
                    tempTex[k].y = 1.0f - tempTex[k].y;
                }
            }

            size_t startIndex = mesh.vertices.size();
            for (size_t k = 0; k < vertCount; ++k) {
                mesh.vertices.push_back({tempPos[k], tempNorm[k], tempTex[k]});
            }

            if (prim->indices) {
                for (size_t k = 0; k < prim->indices->count; ++k) {
                    mesh.indices.push_back(static_cast<unsigned int>(startIndex + cgltf_accessor_read_index(prim->indices, k)));
                }
            } else {
                for (size_t k = 0; k < vertCount; ++k) {
                    mesh.indices.push_back(static_cast<unsigned int>(startIndex + k));
                }
            }
        }
    }

    cgltf_free(data);
    return true;
}

} // namespace Jet
