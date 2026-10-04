#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Camera.h"

namespace Rendering {

// Minimal fixed-function (legacy immediate-mode) OpenGL renderer for Stage
// 1's debug visualization: a reference grid, the black hole as a solid
// sphere, and trajectory polylines. This is deliberately not the final
// rendering path — Stage 3+ needs a fragment-shader/CUDA-buffer based
// full-screen ray tracer for actual lensing images, added later without
// touching this debug-view code.
class Renderer {
public:
    void BeginFrame(const Camera& camera, float aspectRatio, int viewportWidth, int viewportHeight);

    // Re-applies the camera's projection/view matrices and re-enables depth
    // testing WITHOUT clearing the color buffer or touching the viewport --
    // used to draw ordinary rasterized geometry (e.g. the jet mesh) on top
    // of an already-drawn full-screen ray-traced image in the same frame,
    // so both layers appear together.
    void BeginOverlayPass(const Camera& camera, float aspectRatio) const;

    void DrawGrid(double halfExtent, int divisions, const glm::vec3& color) const;
    void DrawSphere(double radius, const glm::vec3& color, int rings = 24, int sectors = 32) const;
    void DrawSphere(const glm::dvec3& center, double radius, const glm::vec3& color, int rings = 24, int sectors = 32) const;
    void DrawWireSphere(double radius, const glm::vec3& color, int rings = 16, int sectors = 24) const;
    void DrawPath(const std::vector<glm::dvec3>& path, const glm::vec3& color, float lineWidth = 2.0f) const;
    void DrawPoint(const glm::dvec3& position, const glm::vec3& color, float size = 6.0f) const;

    // Loads an image file from disk into a GL texture (via stb_image) and
    // returns its id, or 0 on failure. Intended to be called once per unique
    // file and cached by the caller -- this does hit the filesystem/decode a
    // JPEG every call, so it is NOT meant to be called every frame.
    unsigned int LoadTexture(const std::string& path) const;

    // Stage 3: uploads an RGB float image (row-major, top-to-bottom, as
    // produced by RayTracing::RenderImage) to a GL texture and returns its
    // id, creating the texture on first use and reusing/resizing it on
    // subsequent calls so repeated renders don't leak texture objects. When
    // `existingTexture` already has this exact width/height (the common
    // case in live rendering -- the resolution only changes when the window
    // resizes or the resolution slider moves), this updates its pixels
    // in-place (glTexSubImage2D) instead of re-specifying/reallocating the
    // texture's GPU-side storage from scratch every frame (glTexImage2D).
    unsigned int UploadImage(const std::vector<glm::vec3>& pixels, int width, int height, unsigned int existingTexture = 0) const;

    // Draws `texture` as a full-viewport quad, ignoring the 3D camera
    // entirely -- used for the Stage 3 lensed-render view.
    void DrawFullscreenTexture(unsigned int texture) const;

private:
    // Tracks the last size UploadImage specified for a given texture object,
    // so it can tell whether a same-size call can use the cheaper
    // glTexSubImage2D path. Only ever one texture is live this way in
    // practice (the lensed render), but keyed by id to stay correct if that
    // changes.
    mutable unsigned int m_uploadedTexture = 0;
    mutable int m_uploadedWidth = 0;
    mutable int m_uploadedHeight = 0;
};

} // namespace Rendering
