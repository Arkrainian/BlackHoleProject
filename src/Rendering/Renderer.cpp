#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// Legacy fixed-function GL entry points (glBegin/glVertex/glMatrixMode/...)
// are exported directly by opengl32.lib on Windows, so no extension loader
// is needed for this debug-view renderer.
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {
constexpr double kPi = glm::pi<double>();
}

// Windows' bundled <GL/gl.h> only declares OpenGL 1.1, which predates this
// constant (added in 1.2); no extension loader is in use for this
// fixed-function debug/blit renderer, so it's defined directly with its
// standard registry value rather than pulling in glext.h.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Rendering {

void Renderer::BeginFrame(const Camera& camera, float aspectRatio, int viewportWidth, int viewportHeight) {
    glViewport(0, 0, viewportWidth, viewportHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 proj = camera.ProjectionMatrix(aspectRatio);
    const glm::mat4 view = camera.ViewMatrix();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(glm::value_ptr(proj));
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(glm::value_ptr(view));
}

void Renderer::BeginOverlayPass(const Camera& camera, float aspectRatio) const {
    glEnable(GL_DEPTH_TEST);

    const glm::mat4 proj = camera.ProjectionMatrix(aspectRatio);
    const glm::mat4 view = camera.ViewMatrix();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(glm::value_ptr(proj));
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(glm::value_ptr(view));
}

void Renderer::DrawGrid(double halfExtent, int divisions, const glm::vec3& color) const {
    glColor3f(color.r, color.g, color.b);
    glBegin(GL_LINES);
    for (int i = 0; i <= divisions; ++i) {
        const double t = -halfExtent + (2.0 * halfExtent * i) / divisions;
        glVertex3d(t, 0.0, -halfExtent);
        glVertex3d(t, 0.0, halfExtent);
        glVertex3d(-halfExtent, 0.0, t);
        glVertex3d(halfExtent, 0.0, t);
    }
    glEnd();
}

void Renderer::DrawFlammParaboloid(const glm::dvec3& center, double schwarzschildRadius, double outerRadius,
                                    const glm::vec3& color, int radialSegments, int ringSegments) const {
    const double rs = schwarzschildRadius;

    // rho(ring index): quadratic ease-in from rs to outerRadius, so rings
    // bunch up near the throat (rho=rs) where the real surface's slope
    // diverges (vertical tangent) and thin out in the nearly-flat outer
    // region, rather than wasting uniform density where the curvature is
    // mild.
    const auto Rho = [&](int ring) {
        const double t = static_cast<double>(ring) / ringSegments;
        return rs + (outerRadius - rs) * t * t;
    };
    const auto Height = [&](double rho) { return -2.0 * std::sqrt(rs * std::max(0.0, rho - rs)); };

    glColor3f(color.r, color.g, color.b);

    // Rings (circles of latitude) at each sampled rho.
    for (int i = 0; i <= ringSegments; ++i) {
        const double rho = Rho(i);
        const double y = Height(rho);
        glBegin(GL_LINE_LOOP);
        for (int s = 0; s < radialSegments; ++s) {
            const double theta = 2.0 * kPi * s / radialSegments;
            glVertex3d(center.x + rho * std::cos(theta), center.y + y, center.z + rho * std::sin(theta));
        }
        glEnd();
    }
    // Radial lines (lines of longitude) from the throat out to the rim.
    for (int s = 0; s < radialSegments; ++s) {
        const double theta = 2.0 * kPi * s / radialSegments;
        const double cosT = std::cos(theta), sinT = std::sin(theta);
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= ringSegments; ++i) {
            const double rho = Rho(i);
            glVertex3d(center.x + rho * cosT, center.y + Height(rho), center.z + rho * sinT);
        }
        glEnd();
    }
}

void Renderer::DrawSphere(double radius, const glm::vec3& color, int rings, int sectors) const {
    glColor3f(color.r, color.g, color.b);
    for (int r = 0; r < rings; ++r) {
        const double lat0 = kPi * (-0.5 + static_cast<double>(r) / rings);
        const double lat1 = kPi * (-0.5 + static_cast<double>(r + 1) / rings);
        const double y0 = std::sin(lat0), yr0 = std::cos(lat0);
        const double y1 = std::sin(lat1), yr1 = std::cos(lat1);

        glBegin(GL_QUAD_STRIP);
        for (int s = 0; s <= sectors; ++s) {
            const double lng = 2.0 * kPi * static_cast<double>(s) / sectors;
            const double x = std::cos(lng), z = std::sin(lng);
            glVertex3d(radius * x * yr0, radius * y0, radius * z * yr0);
            glVertex3d(radius * x * yr1, radius * y1, radius * z * yr1);
        }
        glEnd();
    }
}

void Renderer::DrawSphere(const glm::dvec3& center, double radius, const glm::vec3& color, int rings, int sectors) const {
    glPushMatrix();
    glTranslated(center.x, center.y, center.z);
    DrawSphere(radius, color, rings, sectors);
    glPopMatrix();
}

void Renderer::DrawTexturedSphere(const glm::dvec3& center, double radius, unsigned int texture, int rings,
                                   int sectors) const {
    glPushMatrix();
    glTranslated(center.x, center.y, center.z);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor3f(1.0f, 1.0f, 1.0f); // no tint -- the texture's own colors are the final color, same as DrawFullscreenTexture

    for (int r = 0; r < rings; ++r) {
        const double lat0 = kPi * (-0.5 + static_cast<double>(r) / rings);
        const double lat1 = kPi * (-0.5 + static_cast<double>(r + 1) / rings);
        const double y0 = std::sin(lat0), yr0 = std::cos(lat0);
        const double y1 = std::sin(lat1), yr1 = std::cos(lat1);
        const double v0 = static_cast<double>(r) / rings;
        const double v1 = static_cast<double>(r + 1) / rings;

        glBegin(GL_QUAD_STRIP);
        for (int s = 0; s <= sectors; ++s) {
            const double lng = 2.0 * kPi * static_cast<double>(s) / sectors;
            const double x = std::cos(lng), z = std::sin(lng);
            const double u = static_cast<double>(s) / sectors;
            glTexCoord2d(u, v0);
            glVertex3d(radius * x * yr0, radius * y0, radius * z * yr0);
            glTexCoord2d(u, v1);
            glVertex3d(radius * x * yr1, radius * y1, radius * z * yr1);
        }
        glEnd();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();
}

void Renderer::DrawEllipsoid(const glm::dvec3& center, const glm::dvec3& primaryAxisDirection,
                              double primarySemiAxis, double lateralSemiAxis, const glm::vec3& color, int rings,
                              int sectors) const {
    const double len = glm::length(primaryAxisDirection);
    const glm::dvec3 yAxis = len > 1e-9 ? primaryAxisDirection / len : glm::dvec3(0.0, 1.0, 0.0);

    // Build an orthonormal right-handed basis with yAxis as the "pole"
    // axis -- DrawSphere's unit sphere varies latitude (its `rings`
    // argument) along +-Y and longitude (`sectors`) around the XZ plane,
    // so aligning Y with the desired stretch direction and then scaling
    // Y by primarySemiAxis / X,Z by lateralSemiAxis turns it into a
    // spheroid stretched the right way without needing a different mesh.
    glm::dvec3 worldUpHint = std::abs(yAxis.y) < 0.99 ? glm::dvec3(0.0, 1.0, 0.0) : glm::dvec3(1.0, 0.0, 0.0);
    const glm::dvec3 xAxis = glm::normalize(glm::cross(worldUpHint, yAxis));
    const glm::dvec3 zAxis = glm::cross(xAxis, yAxis);

    const float basis[16] = {
        static_cast<float>(xAxis.x), static_cast<float>(xAxis.y), static_cast<float>(xAxis.z), 0.0f,
        static_cast<float>(yAxis.x), static_cast<float>(yAxis.y), static_cast<float>(yAxis.z), 0.0f,
        static_cast<float>(zAxis.x), static_cast<float>(zAxis.y), static_cast<float>(zAxis.z), 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    glPushMatrix();
    glTranslated(center.x, center.y, center.z);
    glMultMatrixf(basis);
    glScaled(lateralSemiAxis, primarySemiAxis, lateralSemiAxis);
    DrawSphere(1.0, color, rings, sectors);
    glPopMatrix();
}

void Renderer::DrawShadedSphere(const glm::dvec3& center, double radius, const glm::vec3& color,
                                 const glm::dvec3& lightDirection, int rings, int sectors) const {
    const glm::dvec3 lightDir =
        glm::length(lightDirection) > 1e-9 ? glm::normalize(lightDirection) : glm::dvec3(0.0, 0.0, 1.0);
    constexpr double kAmbient = 0.25; // matches Jet::JetRenderer's shader exactly (ambient floor + Lambertian term)

    for (int r = 0; r < rings; ++r) {
        const double lat0 = kPi * (-0.5 + static_cast<double>(r) / rings);
        const double lat1 = kPi * (-0.5 + static_cast<double>(r + 1) / rings);
        const double y0 = std::sin(lat0), yr0 = std::cos(lat0);
        const double y1 = std::sin(lat1), yr1 = std::cos(lat1);

        glBegin(GL_QUAD_STRIP);
        for (int s = 0; s <= sectors; ++s) {
            const double lng = 2.0 * kPi * static_cast<double>(s) / sectors;
            const double x = std::cos(lng), z = std::sin(lng);

            const glm::dvec3 n0(x * yr0, y0, z * yr0); // unit-sphere normal == unit-radius local position
            const double shade0 = kAmbient + (1.0 - kAmbient) * std::max(0.0, glm::dot(n0, -lightDir));
            glColor3f(static_cast<float>(color.r * shade0), static_cast<float>(color.g * shade0),
                      static_cast<float>(color.b * shade0));
            glVertex3d(center.x + radius * n0.x, center.y + radius * n0.y, center.z + radius * n0.z);

            const glm::dvec3 n1(x * yr1, y1, z * yr1);
            const double shade1 = kAmbient + (1.0 - kAmbient) * std::max(0.0, glm::dot(n1, -lightDir));
            glColor3f(static_cast<float>(color.r * shade1), static_cast<float>(color.g * shade1),
                      static_cast<float>(color.b * shade1));
            glVertex3d(center.x + radius * n1.x, center.y + radius * n1.y, center.z + radius * n1.z);
        }
        glEnd();
    }
}

void Renderer::DrawShadedEllipsoid(const glm::dvec3& center, const glm::dvec3& primaryAxisDirection,
                                    double primarySemiAxis, double lateralSemiAxis, const glm::vec3& color,
                                    const glm::dvec3& lightDirection, int rings, int sectors) const {
    const double len = glm::length(primaryAxisDirection);
    const glm::dvec3 yAxis = len > 1e-9 ? primaryAxisDirection / len : glm::dvec3(0.0, 1.0, 0.0);
    const glm::dvec3 worldUpHint = std::abs(yAxis.y) < 0.99 ? glm::dvec3(0.0, 1.0, 0.0) : glm::dvec3(1.0, 0.0, 0.0);
    const glm::dvec3 xAxis = glm::normalize(glm::cross(worldUpHint, yAxis));
    const glm::dvec3 zAxis = glm::cross(xAxis, yAxis);

    const glm::dvec3 lightDir =
        glm::length(lightDirection) > 1e-9 ? glm::normalize(lightDirection) : glm::dvec3(0.0, 0.0, 1.0);
    constexpr double kAmbient = 0.25;

    const auto EmitVertex = [&](double x, double y, double z) {
        const glm::dvec3 localUnit(x, y, z); // unit-sphere local coords, pre-stretch
        const glm::dvec3 worldPos = center + xAxis * (x * lateralSemiAxis) + yAxis * (y * primarySemiAxis) +
                                     zAxis * (z * lateralSemiAxis);
        // Approximate normal: the UNSTRETCHED local normal rotated into
        // world space (see this method's header comment for why this
        // isn't the exact ellipsoid normal once the two semi-axes diverge).
        const glm::dvec3 worldNormal = glm::normalize(xAxis * localUnit.x + yAxis * localUnit.y + zAxis * localUnit.z);
        const double shade = kAmbient + (1.0 - kAmbient) * std::max(0.0, glm::dot(worldNormal, -lightDir));
        glColor3f(static_cast<float>(color.r * shade), static_cast<float>(color.g * shade),
                  static_cast<float>(color.b * shade));
        glVertex3d(worldPos.x, worldPos.y, worldPos.z);
    };

    for (int r = 0; r < rings; ++r) {
        const double lat0 = kPi * (-0.5 + static_cast<double>(r) / rings);
        const double lat1 = kPi * (-0.5 + static_cast<double>(r + 1) / rings);
        const double y0 = std::sin(lat0), yr0 = std::cos(lat0);
        const double y1 = std::sin(lat1), yr1 = std::cos(lat1);

        glBegin(GL_QUAD_STRIP);
        for (int s = 0; s <= sectors; ++s) {
            const double lng = 2.0 * kPi * static_cast<double>(s) / sectors;
            const double x = std::cos(lng), z = std::sin(lng);
            EmitVertex(x * yr0, y0, z * yr0);
            EmitVertex(x * yr1, y1, z * yr1);
        }
        glEnd();
    }
}

void Renderer::DrawWireSphere(double radius, const glm::vec3& color, int rings, int sectors) const {
    glColor3f(color.r, color.g, color.b);
    for (int r = 1; r < rings; ++r) {
        const double lat = kPi * (-0.5 + static_cast<double>(r) / rings);
        const double y = std::sin(lat), yr = std::cos(lat);
        glBegin(GL_LINE_LOOP);
        for (int s = 0; s < sectors; ++s) {
            const double lng = 2.0 * kPi * static_cast<double>(s) / sectors;
            glVertex3d(radius * std::cos(lng) * yr, radius * y, radius * std::sin(lng) * yr);
        }
        glEnd();
    }
    for (int s = 0; s < sectors; ++s) {
        const double lng = 2.0 * kPi * static_cast<double>(s) / sectors;
        glBegin(GL_LINE_STRIP);
        for (int r = 0; r <= rings; ++r) {
            const double lat = kPi * (-0.5 + static_cast<double>(r) / rings);
            const double y = std::sin(lat), yr = std::cos(lat);
            glVertex3d(radius * std::cos(lng) * yr, radius * y, radius * std::sin(lng) * yr);
        }
        glEnd();
    }
}

void Renderer::DrawWireSphere(const glm::dvec3& center, double radius, const glm::vec3& color, int rings,
                               int sectors) const {
    glPushMatrix();
    glTranslated(center.x, center.y, center.z);
    DrawWireSphere(radius, color, rings, sectors);
    glPopMatrix();
}

unsigned int Renderer::LoadTexture(const std::string& path) const {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true); // GL expects row 0 at the bottom; image files store row 0 at the top
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 3);
    if (!data) {
        std::fprintf(stderr, "Failed to load texture '%s': %s\n", path.c_str(), stbi_failure_reason());
        return 0;
    }

    // No mipmaps here: mipmap generation (glGenerateMipmap, GL 3.0+) isn't
    // exported by opengl32.lib's Windows import library and this renderer
    // deliberately has no extension loader (see the GL_CLAMP_TO_EDGE comment
    // above) -- plain bilinear filtering is a fine trade-off for a handful
    // of decorative background planets.
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return texture;
}

void Renderer::DrawPath(const std::vector<glm::dvec3>& path, const glm::vec3& color, float lineWidth) const {
    if (path.size() < 2) return;
    glLineWidth(lineWidth);
    glColor3f(color.r, color.g, color.b);
    glBegin(GL_LINE_STRIP);
    for (const glm::dvec3& p : path) {
        glVertex3d(p.x, p.y, p.z);
    }
    glEnd();
}

void Renderer::DrawPoint(const glm::dvec3& position, const glm::vec3& color, float size) const {
    glPointSize(size);
    glColor3f(color.r, color.g, color.b);
    glBegin(GL_POINTS);
    glVertex3d(position.x, position.y, position.z);
    glEnd();
}

unsigned int Renderer::UploadImage(const std::vector<glm::vec3>& pixels, int width, int height, unsigned int existingTexture) const {
    GLuint texture = existingTexture;
    const bool isNewTexture = (texture == 0);
    if (isNewTexture) {
        glGenTextures(1, &texture);
    }
    glBindTexture(GL_TEXTURE_2D, texture);

    const bool sameSizeAsLastUpload =
        !isNewTexture && texture == m_uploadedTexture && width == m_uploadedWidth && height == m_uploadedHeight;
    if (sameSizeAsLastUpload) {
        // Overwrite in place -- the texture's GPU-side storage already has
        // the right dimensions/format, so there's no need to reallocate it
        // (glTexImage2D) just to hand it the same-shaped pixel data again.
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_FLOAT, pixels.data());
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_FLOAT, pixels.data());
        m_uploadedTexture = texture;
        m_uploadedWidth = width;
        m_uploadedHeight = height;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void Renderer::DrawFullscreenTexture(unsigned int texture) const {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor3f(1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

} // namespace Rendering
