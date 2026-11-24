#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// GLEW must come before any GL header (including the one GLFW pulls in) --
// it's what exposes the modern shader/FBO entry points DrawFullscreenTextureWithGlare
// uses below, on top of the legacy fixed-function ones
// (glBegin/glVertex/glMatrixMode/...) that opengl32.lib already exports
// directly on Windows and the rest of this debug-view renderer still uses.
#include <GL/glew.h>
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

namespace {

// GPU bloom pipeline backing DrawFullscreenTextureWithGlare -- replaces the
// old CPU ApplyGlare (bright-pass + box blur over every pixel on the host)
// with the same idea done as GPU shader passes instead, since even a
// multithreaded CPU blur over a full HDR framebuffer every time the camera
// moves is real, visible lag that a GPU eats for free. Lazily created (a GL
// context must already exist) and resized whenever the source texture's
// resolution changes; everything here lives for the process's lifetime, same
// as Renderer's own m_uploadedTexture.
struct GlareGpuState {
    bool initialized = false;
    GLuint vao = 0, vbo = 0;
    GLuint brightProgram = 0, blurProgram = 0, compositeProgram = 0;
    GLuint fbo[2] = {0, 0};
    GLuint tex[2] = {0, 0};
    int width = 0, height = 0;
};
GlareGpuState g_glare;

GLuint CompileGlareProgram(const char* vertexSource, const char* fragmentSource) {
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertexSource, nullptr);
    glCompileShader(vertex);

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragmentSource, nullptr);
    glCompileShader(fragment);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLchar log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Rendering::ApplyGlare (GPU): shader link failed: %s\n", log);
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

constexpr const char* kGlareQuadVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

constexpr const char* kGlareBrightFragmentShader = R"glsl(
#version 330 core
in vec2 vUv;
out vec4 outColor;
uniform sampler2D uTex;
uniform float uThreshold;
void main() {
    // Flip V: `uTex` is the raw lensed-render texture (row 0 = top of
    // image, same data UploadImage/DrawFullscreenTexture use), so this has
    // to apply the same flip DrawFullscreenTexture's texcoords do -- every
    // later pass in this pipeline (blur, composite's uGlow) samples
    // textures THIS pipeline produced, so they stay consistent without
    // needing the flip again; only this first read of the external source
    // texture, and the composite pass's uBase read of that same texture,
    // need it.
    vec3 c = texture(uTex, vec2(vUv.x, 1.0 - vUv.y)).rgb;
    float luminance = dot(c, vec3(0.2126, 0.7152, 0.0722));
    outColor = vec4(luminance > uThreshold ? c : vec3(0.0), 1.0);
}
)glsl";

// Small fixed 5-tap Gaussian, applied separably (H then V) and repeated a
// few times -- same "repeated small blur approximates a big one" idea the
// old CPU sliding-window box blur relied on, just spent as a handful of
// full-screen draw calls instead of a huge per-pixel sample window.
constexpr const char* kGlareBlurFragmentShader = R"glsl(
#version 330 core
in vec2 vUv;
out vec4 outColor;
uniform sampler2D uTex;
uniform vec2 uTexelStep;
void main() {
    vec3 sum = texture(uTex, vUv).rgb * 0.227027;
    sum += texture(uTex, vUv + uTexelStep * 1.3846153846).rgb * 0.3162162162;
    sum += texture(uTex, vUv - uTexelStep * 1.3846153846).rgb * 0.3162162162;
    sum += texture(uTex, vUv + uTexelStep * 3.2307692308).rgb * 0.0702702703;
    sum += texture(uTex, vUv - uTexelStep * 3.2307692308).rgb * 0.0702702703;
    outColor = vec4(sum, 1.0);
}
)glsl";

constexpr const char* kGlareCompositeFragmentShader = R"glsl(
#version 330 core
in vec2 vUv;
out vec4 outColor;
uniform sampler2D uBase;
uniform sampler2D uGlow;
uniform float uIntensity;
void main() {
    // uBase is the same raw external texture the bright-pass shader reads
    // (see its own comment) -- same flip needed here. uGlow is this
    // pipeline's own blurred result, already in the flipped/correct
    // orientation, so it's sampled as-is.
    vec3 base = texture(uBase, vec2(vUv.x, 1.0 - vUv.y)).rgb;
    vec3 glow = texture(uGlow, vUv).rgb;
    outColor = vec4(base + glow * uIntensity, 1.0);
}
)glsl";

GLuint CreateGlareTarget(GLuint& fbo, int width, int height) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // 16-bit float storage, not the usual 8-bit: the bright-pass threshold
    // is luminance > 1.0, i.e. values an ordinary LDR target would already
    // have clipped.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (fbo == 0) glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return tex;
}

void EnsureGlareResources(int width, int height) {
    if (!g_glare.initialized) {
        g_glare.initialized = true;

        // A single NDC-space quad (two triangles) covering the whole
        // viewport -- every pass below just reuses this with a different
        // shader/target bound, the standard full-screen-post-process setup.
        constexpr float kQuadVertices[] = {
            -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f,
        };
        glGenVertexArrays(1, &g_glare.vao);
        glGenBuffers(1, &g_glare.vbo);
        glBindVertexArray(g_glare.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_glare.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glBindVertexArray(0);

        g_glare.brightProgram = CompileGlareProgram(kGlareQuadVertexShader, kGlareBrightFragmentShader);
        g_glare.blurProgram = CompileGlareProgram(kGlareQuadVertexShader, kGlareBlurFragmentShader);
        g_glare.compositeProgram = CompileGlareProgram(kGlareQuadVertexShader, kGlareCompositeFragmentShader);
    }

    if (g_glare.width != width || g_glare.height != height) {
        g_glare.width = width;
        g_glare.height = height;
        if (g_glare.tex[0]) glDeleteTextures(1, &g_glare.tex[0]);
        if (g_glare.tex[1]) glDeleteTextures(1, &g_glare.tex[1]);
        g_glare.tex[0] = CreateGlareTarget(g_glare.fbo[0], width, height);
        g_glare.tex[1] = CreateGlareTarget(g_glare.fbo[1], width, height);
    }
}

// Backs DrawWormholeEffect (App.cpp's "capturedByBlackHole" wormhole
// sequence, No Man's Sky-style jump between catalog locations) -- a single
// shader pass, not a multi-FBO pipeline like glare above, since this just
// warps/tints the already-displayed texture rather than needing its own
// blurred render target.
struct WormholeGpuState {
    bool initialized = false;
    GLuint vao = 0, vbo = 0;
    GLuint program = 0;
};
WormholeGpuState g_wormhole;

constexpr const char* kWormholeFragmentShader = R"glsl(
#version 330 core
in vec2 vUv;
out vec4 outColor;
uniform sampler2D uTex;
uniform float uProgress; // 0..1 across the whole sequence (App.cpp's wormholeElapsed / duration)
uniform float uTime;     // free-running seconds, animates the tunnel rings
uniform bool uFlipped;   // see DrawWormholeEffect's own comment

void main() {
    vec2 uv = uFlipped ? vec2(vUv.x, 1.0 - vUv.y) : vUv;
    vec2 toCenter = uv - vec2(0.5, 0.5);
    float dist = length(toCenter);
    vec2 dir = dist > 0.0001 ? toCenter / dist : vec2(0.0);

    // Falls to 0 at both ends of the sequence, peaks at the midpoint --
    // that peak is exactly when App.cpp performs the actual catalog jump,
    // so the strongest warp/flash hides the instant camera-position cut.
    float pull = sin(clamp(uProgress, 0.0, 1.0) * 3.14159265);

    // Radial motion-streak: average samples pulled outward from center,
    // the classic "falling through a tunnel at speed" look.
    vec3 streak = vec3(0.0);
    const int kSamples = 10;
    for (int i = 0; i < kSamples; ++i) {
        float t = float(i) / float(kSamples - 1);
        vec2 sampleUv = clamp(uv - dir * t * pull * 0.6, 0.0, 1.0);
        streak += texture(uTex, sampleUv).rgb;
    }
    streak /= float(kSamples);

    // Pulsing concentric rings racing toward the viewer, tinted blue/violet
    // -- reads as a wormhole tunnel's walls rather than a plain radial blur.
    float rings = sin(dist * 50.0 - uTime * 18.0) * 0.5 + 0.5;
    vec3 tunnelGlow = vec3(0.45, 0.65, 1.0) * rings * pull * (1.0 - smoothstep(0.0, 0.6, dist));

    vec3 color = streak + tunnelGlow;

    // White/blue flash right at the midpoint, strong enough to fully
    // obscure the frame for a few frames around the actual jump.
    vec3 flashColor = vec3(0.85, 0.92, 1.0);
    float flash = smoothstep(0.7, 1.0, pull);
    color = mix(color, flashColor, flash);

    outColor = vec4(color, 1.0);
}
)glsl";

void EnsureWormholeResources() {
    if (g_wormhole.initialized) return;
    g_wormhole.initialized = true;

    constexpr float kQuadVertices[] = {
        -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f,
    };
    glGenVertexArrays(1, &g_wormhole.vao);
    glGenBuffers(1, &g_wormhole.vbo);
    glBindVertexArray(g_wormhole.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_wormhole.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    g_wormhole.program = CompileGlareProgram(kGlareQuadVertexShader, kWormholeFragmentShader);
}

} // namespace

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
        // Sized float internal format, not plain GL_RGB: an unsized
        // internal format lets the driver pick an 8-bit normalized store,
        // which silently clamps every pixel to [0,1] on upload. That
        // destroyed the HDR headroom DrawFullscreenTextureWithGlare's
        // bright-pass shader depends on (its threshold is luminance > 1.0),
        // so glare computed to effectively nothing no matter the pixel
        // data actually being traced above 1.0 before this fix.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, pixels.data());
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

void Renderer::DrawFullscreenTextureWithGlare(unsigned int texture, int width, int height, float intensity,
                                               float radius) const {
    if (intensity <= 0.0f || width <= 0 || height <= 0) {
        DrawFullscreenTexture(texture);
        return;
    }

    EnsureGlareResources(width, height);

    // The bright-pass/blur passes below render into their own FBOs at
    // `width`x`height` (the render's internal resolution, which can be
    // lower than the window) -- save the caller's viewport/framebuffer so
    // the final composite (and everything the rest of the frame draws
    // afterward) lands back at the real window size.
    GLint previousViewport[4];
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);

    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, width, height);
    glBindVertexArray(g_glare.vao);

    // 1) Bright-pass: threshold `texture` into tex[0].
    glBindFramebuffer(GL_FRAMEBUFFER, g_glare.fbo[0]);
    glUseProgram(g_glare.brightProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(g_glare.brightProgram, "uTex"), 0);
    glUniform1f(glGetUniformLocation(g_glare.brightProgram, "uThreshold"), 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 2) A handful of small separable blur passes, ping-ponging tex[0] and
    // tex[1] -- see kGlareBlurFragmentShader's comment. `radius` (same
    // 0..0.3 fractional-image-size slider ApplyGlare used) maps to an
    // iteration count rather than a sample-window size, since each
    // iteration here is one full-screen draw call, not a per-pixel loop.
    const int iterations = std::clamp(static_cast<int>(radius * 40.0f), 1, 12);
    glUseProgram(g_glare.blurProgram);
    const GLint texelStepLoc = glGetUniformLocation(g_glare.blurProgram, "uTexelStep");
    glUniform1i(glGetUniformLocation(g_glare.blurProgram, "uTex"), 0);
    int src = 0, dst = 1;
    for (int i = 0; i < iterations; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_glare.fbo[dst]);
        glBindTexture(GL_TEXTURE_2D, g_glare.tex[src]);
        glUniform2f(texelStepLoc, 1.0f / static_cast<float>(width), 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        std::swap(src, dst);

        glBindFramebuffer(GL_FRAMEBUFFER, g_glare.fbo[dst]);
        glBindTexture(GL_TEXTURE_2D, g_glare.tex[src]);
        glUniform2f(texelStepLoc, 0.0f, 1.0f / static_cast<float>(height));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        std::swap(src, dst);
    }

    // 3) Composite base + blurred glow back at the caller's real
    // viewport/framebuffer.
    glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glUseProgram(g_glare.compositeProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(g_glare.compositeProgram, "uBase"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_glare.tex[src]);
    glUniform1i(glGetUniformLocation(g_glare.compositeProgram, "uGlow"), 1);
    glUniform1f(glGetUniformLocation(g_glare.compositeProgram, "uIntensity"), intensity);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindVertexArray(0);
}

void Renderer::DrawWormholeEffect(unsigned int texture, float progress, float timeSeconds,
                                   bool textureIsFlippedVertically) const {
    EnsureWormholeResources();

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(g_wormhole.vao);
    glUseProgram(g_wormhole.program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(g_wormhole.program, "uTex"), 0);
    glUniform1f(glGetUniformLocation(g_wormhole.program, "uProgress"), progress);
    glUniform1f(glGetUniformLocation(g_wormhole.program, "uTime"), timeSeconds);
    glUniform1i(glGetUniformLocation(g_wormhole.program, "uFlipped"), textureIsFlippedVertically ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindVertexArray(0);
}

unsigned int Renderer::CaptureFramebufferTexture(int width, int height) const {
    if (width <= 0 || height <= 0) return m_captureTexture;

    if (m_captureTexture == 0) {
        glGenTextures(1, &m_captureTexture);
        glBindTexture(GL_TEXTURE_2D, m_captureTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_captureTexture);
    }

    if (m_captureWidth != width || m_captureHeight != height) {
        // glCopyTexImage2D (re)allocates storage at the new size and fills
        // it from the currently-bound read framebuffer in one call --
        // exactly what's needed on a resolution change; glCopyTexSubImage2D
        // below is the cheaper same-size path, same split as UploadImage.
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, width, height, 0);
        m_captureWidth = width;
        m_captureHeight = height;
    } else {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return m_captureTexture;
}

} // namespace Rendering
