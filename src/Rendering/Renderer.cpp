#include "Renderer.h"

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
