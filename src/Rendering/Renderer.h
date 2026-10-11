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

    // Flamm's paraboloid, drawn as an actual surface of revolution (a
    // wireframe "funnel"/trumpet mesh -- concentric rings plus radial
    // lines, matching this renderer's existing wireframe-sphere style)
    // around a vertical axis through `center`, rather than a flat grid
    // with sagging lines. rho = planar distance from that axis; the
    // surface's height is y = -2*sqrt(rs*(rho-rs)) for rho in
    // [rs, outerRadius] -- the standard, textbook-exact 2D embedding of
    // the Schwarzschild equatorial spatial geometry (see docs/physics.md
    // for the derivation and its own caveat: this is an ANALOGY for
    // spacetime curvature, a 2D surface embedded in 3D space, not a
    // literal picture of 4D spacetime -- the caller is responsible for
    // labeling it that way in the UI, not this function). The throat
    // (rho=rs, where the real embedding has a vertical tangent) is
    // sampled more finely than the flaring outer region via a quadratic
    // ease-in on rho, so the narrow neck still reads smoothly rather
    // than as a sharp cone.
    void DrawFlammParaboloid(const glm::dvec3& center, double schwarzschildRadius, double outerRadius,
                              const glm::vec3& color, int radialSegments = 48, int ringSegments = 24) const;
    void DrawSphere(double radius, const glm::vec3& color, int rings = 24, int sectors = 32) const;
    void DrawSphere(const glm::dvec3& center, double radius, const glm::vec3& color, int rings = 24, int sectors = 32) const;

    // A solid sphere stretched into a prolate spheroid along one axis --
    // used to show a tidally-deforming body (e.g. Astronomy::PlanetBody
    // while still a single intact fragment) as an actual stretching solid
    // shape rather than a cloud of points that merely spreads out.
    // primaryAxisDirection need not be normalized; primarySemiAxis is the
    // half-length along that axis, lateralSemiAxis the half-length in the
    // perpendicular plane (equal in both perpendicular directions -- this
    // is a spheroid, not a fully general ellipsoid, which is enough to
    // read as "stretched toward/away from the black hole").
    void DrawEllipsoid(const glm::dvec3& center, const glm::dvec3& primaryAxisDirection, double primarySemiAxis,
                        double lateralSemiAxis, const glm::vec3& color, int rings = 10, int sectors = 16) const;

    // Lambertian-shaded solid sphere: same tessellation as DrawSphere, but
    // with per-vertex diffuse shading (an ambient floor plus a Lambertian
    // term against the sphere's own analytic normal) instead of one flat
    // color -- gives a round body a visible lit side and a dark side
    // rather than reading as a flat silhouette. lightDirection points FROM
    // the light TOWARD the body's center, same convention as
    // Jet::JetRenderer::Draw's lightDirection (and the same 0.25 ambient +
    // 0.75 diffuse split) -- the black hole is this scene's only light
    // source, so callers pass normalize(bodyPosition) when the hole sits
    // at the world origin. Plain DrawSphere is left untouched (still used
    // for the event horizon, which shouldn't look "lit").
    void DrawShadedSphere(const glm::dvec3& center, double radius, const glm::vec3& color,
                          const glm::dvec3& lightDirection, int rings = 10, int sectors = 16) const;

    // Same shading approach as DrawShadedSphere, applied to DrawEllipsoid's
    // stretched shape. The per-vertex normal used for shading is the
    // UNSTRETCHED unit-sphere normal rotated into world space -- exact
    // when primarySemiAxis == lateralSemiAxis (a sphere), an approximation
    // (not the true ellipsoid surface normal) once they diverge. Visually
    // convincing at the stretch ratios a tidally-deforming planet actually
    // reaches; documented here as an approximation rather than silently
    // treated as exact, same spirit as this file's other approximations.
    void DrawShadedEllipsoid(const glm::dvec3& center, const glm::dvec3& primaryAxisDirection, double primarySemiAxis,
                             double lateralSemiAxis, const glm::vec3& color, const glm::dvec3& lightDirection,
                             int rings = 10, int sectors = 16) const;
    void DrawWireSphere(double radius, const glm::vec3& color, int rings = 16, int sectors = 24) const;
    void DrawWireSphere(const glm::dvec3& center, double radius, const glm::vec3& color, int rings = 16,
                         int sectors = 24) const;
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
