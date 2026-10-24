#include "App.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

// GLEW must be included before any header that drags in GL/gl.h (GLFW's
// glfw3.h among them) -- it's only needed here for the jet piloting mode's
// modern-GL mesh rendering (see src/Jet/); everything else in this file
// stays on the legacy fixed-function GL that opengl32.lib exports directly.
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <chrono>

#include "Astronomy/AccretionDisk.h"
#include "Astronomy/Blackbody.h"
#include "Astronomy/StarTexture.h"
#include "Astronomy/PlanetBody.h"
#include "Astronomy/TidalDisruptionFlare.h"
#include "Astronomy/StarBody.h"
#include "Astronomy/StarField.h"
#include "Jet/FlightCamera.h"
#include "Jet/FlightPhysics.h"
#include "Jet/InputHandler.h"
#include "Jet/JetRenderer.h"
#include "Physics/BinaryGeodesicSolver.h"
#include "Physics/BlackHole.h"
#include "Physics/KerrGeodesicSolver.h"
#include "Physics/KerrMetric.h"
#include "Physics/Constants.h"
#include "Physics/GeodesicSolver.h"
#include "Physics/MultiBlackHoleSystem.h"
#include "Physics/MultiBodyGeodesicSolver.h"
#include "Physics/NewtonianSimulation.h"
#include "Physics/Photon.h"
#include "Physics/SchwarzschildMetric.h"
#include "RayTracing/CPUTracer.h"
#ifdef BH_CUDA_AVAILABLE
#include "RayTracing/CUDATracer.h"
#endif
#include "Rendering/Camera.h"
#include "Rendering/Renderer.h"
#include "Physics/GravitySource.h"
#include "Universe/ExoplanetCatalog.h"
#include "Universe/MultiStarSystem.h"
#include "Universe/StarCatalog.h"

using Physics::BlackHole;
using Physics::GeodesicConfig;
using Physics::IntegratorMethod;
using Physics::KerrGeodesicConfig;
using Physics::TracePhotonKerr;
using Physics::NewtonianSimulation;
using Physics::NewtonianSimulationConfig;
using Physics::ParticleFate;
using Physics::ParticleSpawnConfig;
using Physics::ParticleState;
using Physics::PhotonFate;
using Physics::PhotonTraceResult;
using Physics::TracePhoton;

namespace Application {

namespace {

// Method colors used consistently across the trajectory lines, points, and
// the stats panel, so a color in the plot always means the same integrator.
glm::vec3 ColorForMethod(IntegratorMethod method) {
    switch (method) {
        case IntegratorMethod::Euler: return {0.95f, 0.25f, 0.25f};
        case IntegratorMethod::VelocityVerlet: return {0.25f, 0.85f, 0.35f};
        case IntegratorMethod::RK4: return {0.35f, 0.55f, 0.95f};
    }
    return {1.0f, 1.0f, 1.0f};
}

const char* MethodName(IntegratorMethod method) {
    switch (method) {
        case IntegratorMethod::Euler: return "Euler";
        case IntegratorMethod::VelocityVerlet: return "Velocity Verlet";
        case IntegratorMethod::RK4: return "RK4";
    }
    return "?";
}

// ImGui's C++ API has no native SliderDouble (only SliderFloat/SliderScalar);
// this wraps SliderScalar so call sites below read like the float version.
bool SliderDouble(const char* label, double* v, double vMin, double vMax, const char* format = "%.3f") {
    return ImGui::SliderScalar(label, ImGuiDataType_Double, v, &vMin, &vMax, format);
}

// Formats a physical distance (in meters) adaptively -- km for anything
// solar-system-scale, light-years once it's bigger than that -- so the
// compass HUD (see the Run() loop) reads sensibly whether the black hole is
// a stellar-mass hole a few hundred km away or TON 618 a fraction of a
// light-year away (sim units always scale with the hole's own mass, see
// Physics/Constants.h, so the same on-screen distance means very different
// real distances depending on which hole is loaded).
std::string FormatDistance(double meters) {
    constexpr double kLightYearMeters = 9.4607e15;
    char buf[64];
    if (meters < 0.1 * kLightYearMeters) {
        std::snprintf(buf, sizeof(buf), "%.3g km", meters / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.3g ly", meters / kLightYearMeters);
    }
    return buf;
}

// Draws a small "(?)" next to the previous control and shows `text` as a
// plain-language tooltip on hover -- used throughout the Advanced panel so
// the technical labels (impact parameter, ISCO, redshift, ...) stay precise
// for a technical reader without being a wall of text for everyone else.
void HelpTooltip(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// A dark, slightly warm theme (deep space blue-black with an amber/orange
// accent, echoing an accretion disk's glow) replacing ImGui's flat default
// dark palette -- rounded corners, more breathing room between controls, and
// a clearer visual hierarchy between headers/active state/plain text.
void ApplyBlackHoleTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;

    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    ImVec4* colors = style.Colors;
    const ImVec4 bgDeep      (0.07f, 0.08f, 0.11f, 0.97f);
    const ImVec4 bgPanel     (0.10f, 0.11f, 0.15f, 1.00f);
    const ImVec4 bgRaised    (0.14f, 0.15f, 0.20f, 1.00f);
    const ImVec4 border      (0.24f, 0.22f, 0.20f, 0.55f);
    const ImVec4 textMain    (0.92f, 0.92f, 0.90f, 1.00f);
    const ImVec4 textDim     (0.58f, 0.58f, 0.60f, 1.00f);
    const ImVec4 accent      (0.95f, 0.55f, 0.22f, 1.00f); // accretion-disk amber
    const ImVec4 accentHover (1.00f, 0.65f, 0.30f, 1.00f);
    const ImVec4 accentActive(0.85f, 0.45f, 0.15f, 1.00f);
    const ImVec4 accentDim   (0.95f, 0.55f, 0.22f, 0.35f);

    colors[ImGuiCol_Text]                  = textMain;
    colors[ImGuiCol_TextDisabled]          = textDim;
    colors[ImGuiCol_WindowBg]              = bgDeep;
    colors[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg]               = bgPanel;
    colors[ImGuiCol_Border]                = border;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg]               = bgPanel;
    colors[ImGuiCol_FrameBgHovered]        = bgRaised;
    colors[ImGuiCol_FrameBgActive]         = accentDim;
    colors[ImGuiCol_TitleBg]               = bgDeep;
    colors[ImGuiCol_TitleBgActive]         = bgPanel;
    colors[ImGuiCol_TitleBgCollapsed]      = bgDeep;
    colors[ImGuiCol_MenuBarBg]             = bgPanel;
    colors[ImGuiCol_ScrollbarBg]           = bgDeep;
    colors[ImGuiCol_ScrollbarGrab]         = bgRaised;
    colors[ImGuiCol_ScrollbarGrabHovered]  = accentDim;
    colors[ImGuiCol_ScrollbarGrabActive]   = accent;
    colors[ImGuiCol_CheckMark]             = accent;
    colors[ImGuiCol_SliderGrab]            = accent;
    colors[ImGuiCol_SliderGrabActive]      = accentHover;
    colors[ImGuiCol_Button]                = bgRaised;
    colors[ImGuiCol_ButtonHovered]         = accentDim;
    colors[ImGuiCol_ButtonActive]          = accent;
    colors[ImGuiCol_Header]                = bgRaised;
    colors[ImGuiCol_HeaderHovered]         = accentDim;
    colors[ImGuiCol_HeaderActive]          = accentActive;
    colors[ImGuiCol_Separator]             = border;
    colors[ImGuiCol_SeparatorHovered]      = accent;
    colors[ImGuiCol_SeparatorActive]       = accentHover;
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered]     = accentDim;
    colors[ImGuiCol_ResizeGripActive]      = accent;
    colors[ImGuiCol_Tab]                   = bgPanel;
    colors[ImGuiCol_TabHovered]            = accentDim;
    colors[ImGuiCol_TabActive]             = accentActive;
    colors[ImGuiCol_TabUnfocused]          = bgPanel;
    colors[ImGuiCol_TabUnfocusedActive]    = bgRaised;
    colors[ImGuiCol_PlotLines]             = accent;
    colors[ImGuiCol_PlotLinesHovered]      = accentHover;
    colors[ImGuiCol_PlotHistogram]         = accent;
    colors[ImGuiCol_PlotHistogramHovered]  = accentHover;
    colors[ImGuiCol_TableHeaderBg]         = bgRaised;
    colors[ImGuiCol_TableBorderStrong]     = border;
    colors[ImGuiCol_TableBorderLight]      = ImVec4(border.x, border.y, border.z, 0.3f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    colors[ImGuiCol_TextSelectedBg]        = accentDim;
    colors[ImGuiCol_DragDropTarget]        = accent;
    colors[ImGuiCol_NavHighlight]          = accent;
}

// A short, bright section label used in Simple mode in place of plain
// ImGui::Text headings, so the quick-start panel has visible structure at a
// glance instead of reading as one continuous wall of controls.
void SectionLabel(const char* text) {
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.22f, 1.0f), "%s", text);
    ImGui::Separator();
}

enum class UiMode { Simple, Advanced };

const char* FateName(ParticleFate fate) {
    switch (fate) {
        case ParticleFate::ACTIVE: return "active";
        case ParticleFate::CAPTURED: return "CAPTURED";
        case ParticleFate::ESCAPED: return "ESCAPED";
        case ParticleFate::UNRESOLVED: return "UNRESOLVED";
    }
    return "?";
}

const char* PhotonFateName(PhotonFate fate) {
    switch (fate) {
        case PhotonFate::ACTIVE: return "active";
        case PhotonFate::CAPTURED: return "CAPTURED";
        case PhotonFate::ESCAPED: return "ESCAPED";
        case PhotonFate::UNRESOLVED: return "UNRESOLVED";
    }
    return "?";
}

// Photon paths are colored by fate rather than by method (there's only one
// "method" -- the exact Schwarzschild geodesic -- so fate is the
// interesting dimension to distinguish visually here).
glm::vec3 ColorForPhotonFate(PhotonFate fate) {
    switch (fate) {
        case PhotonFate::ESCAPED: return {0.3f, 0.9f, 0.95f};    // cyan
        case PhotonFate::CAPTURED: return {0.95f, 0.25f, 0.65f}; // magenta
        case PhotonFate::UNRESOLVED: return {0.95f, 0.9f, 0.2f}; // yellow -- near b_crit, orbits the photon sphere
        case PhotonFate::ACTIVE: return {1.0f, 1.0f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f};
}

// Stage 1 scenario: a "beam" of test particles approaching the black hole
// from the same starting distance along -x, offset in y by their impact
// parameter b, all moving in the +x direction at the same speed. Each
// impact parameter is spawned three times (once per integrator) so their
// divergence is directly comparable. This mirrors how Stage 2+ will treat
// photon impact parameters, just under Newtonian gravity for now.
struct ScenarioParams {
    int impactParameterCount = 9;
    double impactParameterMin = 3.0;
    double impactParameterMax = 15.0;
    double startDistance = 40.0;
    double speed = 1.0; // in units of c; 1.0 = "photon-like" (non-relativistic approximation, see UI note)
};

std::vector<ParticleSpawnConfig> BuildScenario(const ScenarioParams& params) {
    std::vector<ParticleSpawnConfig> spawns;
    const int n = std::max(1, params.impactParameterCount);
    spawns.reserve(static_cast<size_t>(n) * 3);

    for (int i = 0; i < n; ++i) {
        const double t = n > 1 ? static_cast<double>(i) / (n - 1) : 0.0;
        const double b = params.impactParameterMin + t * (params.impactParameterMax - params.impactParameterMin);

        ParticleState initial;
        initial.position = glm::dvec3(-params.startDistance, b, 0.0);
        initial.velocity = glm::dvec3(params.speed, 0.0, 0.0);

        for (IntegratorMethod method : {IntegratorMethod::Euler, IntegratorMethod::VelocityVerlet, IntegratorMethod::RK4}) {
            spawns.push_back(ParticleSpawnConfig{initial, method});
        }
    }
    return spawns;
}

// Stage 2: traces one exact Schwarzschild null geodesic per impact
// parameter, using the SAME start distance/impact-parameter range as the
// Newtonian scenario above so the two can be visually overlaid and
// compared directly -- same starting position and speed (c), the only
// difference is Newtonian 1/r^2 gravity vs the true geodesic.
std::vector<PhotonTraceResult> BuildPhotonTraces(const ScenarioParams& params) {
    std::vector<PhotonTraceResult> traces;
    const int n = std::max(1, params.impactParameterCount);
    traces.reserve(n);

    GeodesicConfig config;
    config.recordPath = true;
    config.maxPathPoints = 1000;
    config.escapeRadius = std::max(1.0e4, params.startDistance * 50.0);
    config.maxSteps = 100000;
    config.basePhiStep = 0.01;

    for (int i = 0; i < n; ++i) {
        const double t = n > 1 ? static_cast<double>(i) / (n - 1) : 0.0;
        const double b = params.impactParameterMin + t * (params.impactParameterMax - params.impactParameterMin);

        const glm::dvec3 origin(-params.startDistance, b, 0.0);
        const glm::dvec3 direction(1.0, 0.0, 0.0);
        traces.push_back(TracePhoton(origin, direction, config));
    }
    return traces;
}

// Stage 3: turns the Camera (which the debug 3D view already uses) into the
// orthonormal frame the per-pixel ray tracer needs. In Orbit mode this
// always looks at the black hole at the origin -- consistent with every
// other control in this app, which orbits around it. In FreeFly mode it
// looks wherever the camera itself is pointed, independent of the origin
// (see Rendering::Camera::Forward).
RayTracing::CameraFrame BuildCameraFrame(const Rendering::Camera& camera, int renderWidth, int renderHeight) {
    const Rendering::Camera::Basis basis = camera.OrthonormalBasis();

    RayTracing::CameraFrame frame;
    frame.position = camera.Position();
    frame.forward = basis.forward;
    frame.right = basis.right;
    frame.up = basis.up;
    frame.tanHalfFovY = std::tan(glm::radians(camera.fovYDegrees) * 0.5);
    frame.aspect = static_cast<double>(renderWidth) / static_cast<double>(renderHeight);
    return frame;
}

// The "Resolution" slider is a PERCENTAGE of the window's own framebuffer,
// which means its actual ray count scales with however large the user's
// window or monitor happens to be -- on a maximized window at a high
// resolution, even a modest-looking percentage can add up to several
// million rays per frame, and frame rate drops accordingly. That isn't a
// bug in any one feature (it reproduces identically with or without the
// accretion disk enabled); it's this function forgetting to bound the
// result. maxRenderPixels caps the traced image at a fixed ray budget
// regardless of window size, scaling both dimensions down proportionally
// (preserving aspect ratio, and thus the fix for the earlier "stretched at
// 1024x1024" bug) so frame rate stays predictable on any monitor.
// Default of 1.5M rays/frame was tuned against an RTX 4070 Ti Super; exposed
// as an Advanced-mode setting (see "Max rays/frame" slider) rather than a
// hardcoded constant so it can be raised or lowered for different hardware.
constexpr int kDefaultMaxRenderPixels = 1'500'000;

// Kerr's per-step cost is much higher than Schwarzschild's -- every RK4
// stage needs its own sin/cos pair (KerrGeodesicMath::RK4Step), where
// Schwarzschild's u(phi) RK4Step is pure polynomial arithmetic -- so
// reusing the Schwarzschild live-render step budget (8000, see
// PerformRender's geodesicConfig.maxSteps) for Kerr would let rays near
// the photon sphere (i.e. the shadow boundary -- a performance-critical
// RING of pixels, not a rare case) burn many times more work per pixel
// before resolving. This smaller cap bounds that worst case directly;
// any ray that doesn't resolve within it renders UNRESOLVED (black,
// visually indistinguishable from CAPTURED at the shadow edge), so the
// visual cost is a marginally thicker shadow boundary, not a visible
// artifact.
constexpr int kKerrLiveMaxSteps = 1500;

// Same reasoning as kKerrLiveMaxSteps above, for binary mode: TWO shadow
// boundaries now (one per mass) instead of one, each a performance-
// critical ring of pixels where the adaptive step shrinks and a ray can
// burn through many steps before resolving. escapeRadius is also capped
// well below BinaryGeodesicConfig's 1e4 default -- visually, a ray that's
// already this far from both masses is just heading to the starfield in
// a straight line, so there's nothing to gain from tracing it further,
// only step-count cost.
constexpr int kBinaryLiveMaxSteps = 1200;
// Must stay comfortably above the camera's own max orbit distance (300,
// see the Orbit-mode mouse-wheel clamp in App::Run) -- an escapeRadius
// smaller than the camera's own distance from the origin would make the
// very first integration step register as "escaped" trivially, before
// the ray has gone anywhere (the exact mistake tests/test_binary.cpp's
// own history already caught once this project).
constexpr double kBinaryLiveEscapeRadius = 600.0;

std::pair<int, int> ComputeRenderResolution(int fbWidth, int fbHeight, double scalePercent, int maxRenderPixels) {
    const double scale = scalePercent / 100.0;
    int w = std::max(1, static_cast<int>(fbWidth * scale));
    int h = std::max(1, static_cast<int>(fbHeight * scale));

    const long long pixelCount = static_cast<long long>(w) * h;
    if (pixelCount > maxRenderPixels) {
        const double shrink = std::sqrt(static_cast<double>(maxRenderPixels) / static_cast<double>(pixelCount));
        w = std::max(1, static_cast<int>(w * shrink));
        h = std::max(1, static_cast<int>(h * shrink));
    }
    return {w, h};
}

// Stage 4: a single distant point source, used for the Einstein ring test.
// Its direction is always camera.forward -- i.e. it sits on the camera's
// own viewing axis, beyond the black hole -- so camera/black-hole/source
// are automatically collinear for ANY orbit position without needing
// separate alignment controls. This is exactly the "camera -> black hole ->
// source" configuration the Einstein ring requires; the ring itself is not
// drawn, it emerges from CPUTracer sampling this function per pixel.
glm::vec3 SamplePointSource(const glm::dvec3& direction, const glm::dvec3& sourceDirection,
                             double angularRadius, double brightness) {
    const double angularDistance = std::acos(glm::clamp(glm::dot(direction, sourceDirection), -1.0, 1.0));
    if (angularDistance > angularRadius) {
        return glm::vec3(0.0f);
    }
    const double falloff = 1.0 - angularDistance / angularRadius;
    return glm::vec3(static_cast<float>(brightness * falloff * falloff));
}

// Stage 4 debug mode: traces a small fan of rays directly from the camera
// (the SAME origin/basis the Stage 3 renderer uses, not the parallel-beam
// setup Stage 2's debug view uses) along a vertical scanline through the
// image center, with full path recording. This is what exposes individual
// photon paths/impact parameters/deflection angles/closest approach for
// the actual camera-perspective rays that build the Einstein ring, rather
// than the flat-beam rays used for the Stage 1/2 comparison plot.
//
// spin: routes through TracePhotonKerr instead of TracePhoton when
// nonzero, exactly like RayTracing::RenderImage does for the actual
// lensed render -- this used to always call TracePhoton regardless of
// the black hole's spin, which silently showed Schwarzschild debug rays
// (and a Schwarzschild b) while the actual Stage 3 render next to it was
// using Kerr physics. Fixed alongside adding the Photon Inspector below,
// since both share this same "which solver for the current spin" need.
std::vector<PhotonTraceResult> BuildCameraDebugRays(const RayTracing::CameraFrame& frame, int count, double spin) {
    std::vector<PhotonTraceResult> traces;
    traces.reserve(count);

    GeodesicConfig config;
    config.recordPath = true;
    config.maxPathPoints = 500;
    config.escapeRadius = 1.0e4;
    config.maxSteps = 20000;
    config.basePhiStep = 0.01;

    KerrGeodesicConfig kerrConfig;
    kerrConfig.recordPath = true;
    kerrConfig.maxPathPoints = 500;
    kerrConfig.escapeRadius = 1.0e4;
    kerrConfig.maxSteps = 20000;

    for (int i = 0; i < count; ++i) {
        const double t = count > 1 ? static_cast<double>(i) / (count - 1) : 0.5;
        const double ndcY = (t * 2.0 - 1.0) * frame.tanHalfFovY;
        const glm::dvec3 direction = glm::normalize(frame.forward + ndcY * frame.up);
        traces.push_back(spin != 0.0 ? TracePhotonKerr(frame.position, direction, spin, kerrConfig)
                                      : TracePhoton(frame.position, direction, config));
    }
    return traces;
}

// Photon Inspector: traces exactly ONE ray, picked by NDC position within
// the current camera frame (same NDC convention RayTracing::RenderImage
// uses per-pixel), with full path recording -- this is the single-ray
// counterpart to BuildCameraDebugRays' fan of rays along one scanline,
// letting the user target any point in the image (including off the
// vertical centerline, e.g. the asymmetric part of a Kerr shadow) rather
// than only a fixed 1D sweep.
PhotonTraceResult TraceInspectorRay(const RayTracing::CameraFrame& frame, double ndcX, double ndcY, double spin) {
    const glm::dvec3 direction = glm::normalize(frame.forward + ndcX * frame.aspect * frame.tanHalfFovY * frame.right +
                                                 ndcY * frame.tanHalfFovY * frame.up);
    if (spin != 0.0) {
        KerrGeodesicConfig kerrConfig;
        kerrConfig.recordPath = true;
        kerrConfig.maxPathPoints = 2000;
        kerrConfig.escapeRadius = 1.0e4;
        kerrConfig.maxSteps = 20000;
        return TracePhotonKerr(frame.position, direction, spin, kerrConfig);
    }
    GeodesicConfig config;
    config.recordPath = true;
    config.maxPathPoints = 2000;
    config.escapeRadius = 1.0e4;
    config.maxSteps = 20000;
    config.basePhiStep = 0.01;
    return TracePhoton(frame.position, direction, config);
}

// Scientific Plots: one sample of the deflection-angle-vs-impact-parameter
// sweep, both numerically ray-traced (same solver the render uses) and
// from the closed-form weak-field formula (docs/physics.md Sec. 7) --
// the interactive counterpart to docs/validation.md's fixed table.
struct DeflectionSample {
    float b = 0.0f;
    float numericalDeflection = 0.0f; // radians; 0 if the ray didn't escape (captured/unresolved)
    float analyticDeflection = 0.0f;  // Schwarzschild weak-field formula, regardless of spin -- see caller
    bool escaped = false;
};

// Launches each ray from r0=1e5 (effectively "from infinity", same
// convention as docs/validation.md) at a flat beam toward the hole, exactly
// like the existing Stage 1/2 comparison and BuildCameraDebugRays already
// do, just sweeping b instead of fixing it. spin routes to TracePhotonKerr
// exactly like everywhere else in this file. The analytic curve is
// DELIBERATELY always the spin-less Schwarzschild formula (2*rs/b plus the
// next-order PPN term) even when spin != 0 -- with spin on, the gap
// between the two curves IS the frame-dragging signature (prograde deflects
// less, retrograde more, than a non-rotating hole of the same mass), a real
// physical effect to show, not an error to hide by deriving a spin-corrected
// reference curve.
std::vector<DeflectionSample> BuildDeflectionSweep(double bMin, double bMax, int count, double spin) {
    std::vector<DeflectionSample> samples;
    samples.reserve(count);

    const double r0 = 1.0e5;
    const double rs = BlackHole::SchwarzschildRadiusSim();

    GeodesicConfig config;
    config.escapeRadius = 1.0e6;
    config.maxSteps = 50000;
    config.basePhiStep = 0.01;

    KerrGeodesicConfig kerrConfig;
    kerrConfig.escapeRadius = 1.0e6;
    kerrConfig.maxSteps = 50000;

    for (int i = 0; i < count; ++i) {
        const double t = count > 1 ? static_cast<double>(i) / (count - 1) : 0.0;
        const double b = bMin + t * (bMax - bMin);

        const PhotonTraceResult result =
            spin != 0.0 ? TracePhotonKerr(glm::dvec3(-r0, b, 0.0), glm::dvec3(1.0, 0.0, 0.0), spin, kerrConfig)
                        : TracePhoton(glm::dvec3(-r0, b, 0.0), glm::dvec3(1.0, 0.0, 0.0), config);

        DeflectionSample s;
        s.b = static_cast<float>(b);
        s.escaped = result.fate == PhotonFate::ESCAPED;
        s.numericalDeflection = s.escaped ? static_cast<float>(std::abs(result.totalDeflectionAngle)) : 0.0f;
        s.analyticDeflection =
            static_cast<float>(2.0 * rs / b + (15.0 * glm::pi<double>() / 16.0) * (rs / b) * (rs / b));
        samples.push_back(s);
    }
    return samples;
}

} // namespace

App::App() {
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return;
    }

    m_window = glfwCreateWindow(1280, 800, "Black Hole Ray-Tracing Simulator", nullptr, nullptr);
    if (!m_window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE; // needed on some drivers to expose core-profile-only entry points via glew's legacy path
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "Failed to initialize GLEW -- the jet's mesh rendering will be unavailable\n");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ApplyBlackHoleTheme();

    // The default embedded font (ProggyClean) is a small pixel font that
    // gets blurry/chunky scaled up -- load a proper proportional TTF at a
    // larger size instead, same "load this resource file, fall back to a
    // built-in default if missing" pattern as the jet model below.
    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts->AddFontFromFileTTF("resources/fonts/DroidSans.ttf", 19.0f)) {
        ImFontConfig fallbackConfig;
        fallbackConfig.SizePixels = 18.0f;
        io.Fonts->AddFontDefault(&fallbackConfig);
    }

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
}

App::~App() {
    if (m_window) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

int App::Run() {
    if (!m_window) {
        return 1;
    }

    // Default to TON 618, one of the most massive black holes known
    // (~6.6e10 solar masses) -- still just a starting point for the mass
    // sliders below, not a fixed value.
    constexpr double kTon618MassSolar = 6.6e10;
    BlackHole blackHole(kTon618MassSolar);
    Rendering::Camera camera;
    Rendering::Renderer renderer;

    UiMode uiMode = UiMode::Simple;
    std::string activePreset = "Black Hole"; // which Simple-mode preset is currently framed up
    double blackHoleMassSolar = blackHole.MassSolarMasses(); // shared by the Simple and Advanced mass sliders
    double blackHoleSpin = 0.0; // a* = Jc/(GM^2); 0 = Schwarzschild (default), see Advanced > Spacetime

    // "M" toggles a standalone Universe/Star Map window regardless of
    // Simple/Advanced mode or camera mode (Orbit/FreeFly/Jet) -- without
    // this, reaching the Star Map required switching to Advanced mode and
    // scrolling to find it, which is a lot of friction for something meant
    // to be used mid-flight. See the main loop's key-handling block and
    // DrawUniversePanelContents below.
    bool showUniverseMapWindow = false;
    bool universeMapKeyWasDown = false; // edge-detects the M press, same pattern as the jet's Tab/C handling

    // --- Universe / Star Map (see Universe/StarCatalog.h) ---
    // Real stars/black holes/galaxies at their real distances/masses.
    // "Traveling" re-points this app's one black-hole-centered scene at a
    // different real object rather than simulating continuous flight across
    // light-years, which nothing in this app's camera/physics is built to
    // do -- see StarCatalog.h's header comment for the full reasoning.
    const std::vector<Universe::CatalogObject> universeCatalog = Universe::BuildCatalog();
    const std::vector<Universe::ExoplanetEntry> exoplanetCatalog = Universe::BuildExoplanetCatalog();
    int universeLocationIndex = -1;  // -1 = Sol (the implicit origin every session starts at) -- kept only for the catalog listbox's own highlight bookkeeping; see currentLocationLy/currentLocationName for the actual "where am I" state, which also has to cover multi-star systems that aren't single catalog entries
    glm::dvec3 currentLocationLy = glm::dvec3(0.0); // real heliocentric position, in light-years, of wherever the most recent REAL travel (single object or real multi-star system) landed
    std::string currentLocationName = "Sol (home)";
    int universeSelectedIndex = 0;   // row currently highlighted in the Star Map table, not yet traveled to
    bool inStarSystemView = false;   // true after traveling to a Star entry -- no GR metric to render there, see the dedicated render branch below
    int visitedStarIndex = -1;
    std::string lastTravelSummary;   // human-readable real distance/light-time for the most recent jump

    // --- Multi-star systems (see Universe/MultiStarSystem.h) ---
    // A generalization of inStarSystemView to 2+ mutually gravitating
    // stars, driven by a REAL per-frame Newtonian N-body integration (the
    // tuned mutual-gravity law in Physics/GravitySource.h, already
    // validated for exactly this "two stars spiral/swing past each other"
    // behavior by the existing Mutual Gravity feature) instead of a static
    // single sphere -- mutually exclusive with inStarSystemView (only one
    // of the two is ever true at once; both are false in the default
    // black-hole scene).
    bool inMultiStarView = false;
    Universe::MultiStarSystemEntry activeMultiStarSystem;
    std::vector<glm::dvec3> multiStarPositions;  // visual-scale sim units, barycenter at the origin
    std::vector<glm::dvec3> multiStarVelocities; // visual-scale sim units/s
    std::vector<unsigned int> multiStarTextureIds; // one Astronomy::GenerateStarTexture upload per member, regenerated on arrival
    int proceduralStarCount = 4; // the N-body generator's star-count slider, Universe/Star Map panel
    unsigned int proceduralSystemSeedCounter = 1; // increments each generation so repeated clicks don't reproduce the same system

    auto UniverseCurrentPositionLy = [&]() -> glm::dvec3 { return currentLocationLy; };
    // TravelToUniverseIndex itself is defined further down (after the jet
    // piloting state it also needs to reset on arrival, see there) --
    // forward-declared via std::function so the UI code below (which is
    // defined before that point) can still capture and call it.
    std::function<void(int)> TravelToUniverseIndex;
    // Same forward-declaration reasoning as TravelToUniverseIndex above --
    // defined later (after jet piloting state) via ResetJetForArrival.
    std::function<void(const Universe::MultiStarSystemEntry&)> TravelToMultiStarSystem;

    // Shared body of the Universe/Star Map UI, used both inside the
    // Advanced-mode "Universe / Star Map" panel and inside the standalone
    // "M"-toggled window (see showUniverseMapWindow above) -- written once
    // so pressing M gets the exact same catalog browser/Travel button as
    // Advanced mode's copy, not a second, independently-drifting UI.
    auto DrawUniversePanelContents = [&]() {
        ImGui::TextWrapped(
            "Real stars, black holes, and galaxies at their real distances/masses (see "
            "Universe/StarCatalog.h). Selecting a Star or Black Hole entry and pressing 'Travel Here' "
            "re-points this scene at it -- an instantaneous narrative jump, not simulated faster-than-"
            "light flight (the real light-travel time is always shown). Galaxy entries are too far and "
            "too extended for this app's single-point-source renderer to visit; they're listed for real-"
            "scale distance context only.");

        ImGui::Text("Current location: %s", currentLocationName.c_str());
        if (!lastTravelSummary.empty()) {
            ImGui::TextWrapped("%s", lastTravelSummary.c_str());
        }

        if (ImGui::BeginListBox("##UniverseCatalog", ImVec2(-FLT_MIN, 160.0f))) {
            for (int i = 0; i < static_cast<int>(universeCatalog.size()); ++i) {
                const Universe::CatalogObject& obj = universeCatalog[static_cast<size_t>(i)];
                const char* typeTag = obj.type == Universe::ObjectType::Star ? "[Star]"
                                      : obj.type == Universe::ObjectType::BlackHoleObj ? "[Black Hole]"
                                                                                        : "[Galaxy]";
                char label[160];
                std::snprintf(label, sizeof(label), "%s %s", typeTag, obj.name);
                if (ImGui::Selectable(label, universeSelectedIndex == i)) {
                    universeSelectedIndex = i;
                }
            }
            ImGui::EndListBox();
        }

        const Universe::CatalogObject& selected = universeCatalog[static_cast<size_t>(universeSelectedIndex)];
        const double distanceFromHereLy = glm::length(selected.CartesianLy() - UniverseCurrentPositionLy());
        ImGui::TextWrapped("%s -- %s", selected.name, selected.notes);
        ImGui::Text("Distance from current location: %.4g ly (light-travel time: %.4g years)",
                    distanceFromHereLy, distanceFromHereLy);
        if (selected.type == Universe::ObjectType::Star) {
            ImGui::Text("Mass %.3g M_sun, surface temperature %.0f K", selected.massSolar, selected.temperatureK);
        } else if (selected.type == Universe::ObjectType::BlackHoleObj) {
            ImGui::Text("Mass %.3e M_sun%s", selected.massSolar,
                        selected.spin != Universe::kUnknownSpin ? " (spin a* known, applied on arrival)" : " (spin not reliably measured)");
        }

        if (selected.type == Universe::ObjectType::Galaxy) {
            ImGui::TextDisabled("Not a travel target -- distance context only (see panel description above).");
        } else if (ImGui::Button("Travel Here")) {
            TravelToUniverseIndex(universeSelectedIndex);
        }
        if (camera.mode == Rendering::Camera::Mode::Jet) {
            ImGui::TextDisabled("Piloting the Endurance: traveling will carry it along, respawned at the new location.");
        }
        if (inStarSystemView) {
            ImGui::TextWrapped(
                "Currently in Star System View: a simplified, non-ray-traced scene (this app's GR "
                "renderer only models black holes) -- travel to a Black Hole entry above to return to "
                "the full ray-traced view.");
        }

        ImGui::Separator();
        ImGui::TextWrapped(
            "Multi-Star Systems: 2, 3, or N mutually gravitating stars, integrated as a REAL Newtonian "
            "N-body system every frame (not a scripted animation) -- 2-body settles into an ordinary bound "
            "orbit; 3+ has no general closed-form solution, so it's genuinely chaotic, the same unsolved "
            "N-body problem real astrophysics deals with.");
        if (ImGui::Button("Travel to Alpha Centauri AB (2-body, real)")) {
            TravelToMultiStarSystem(Universe::BuildAlphaCentauriBinary());
        }
        if (ImGui::Button("Travel to Alpha Centauri System (3-body, real)")) {
            TravelToMultiStarSystem(Universe::BuildAlphaCentauriTriple());
        }
        ImGui::SliderInt("Star count##Procedural", &proceduralStarCount, 2, 8);
        HelpTooltip("How many stars the generated system has. 2 = an ordinary binary; 3+ = a genuinely chaotic N-body system with no closed-form orbit -- not a specific real system, see Universe/MultiStarSystem.h.");
        if (ImGui::Button("Generate & Travel (N-body)")) {
            TravelToMultiStarSystem(Universe::GenerateProceduralSystem(proceduralStarCount, proceduralSystemSeedCounter++));
        }
        if (inMultiStarView) {
            ImGui::TextWrapped("Currently in: %s (%s)", activeMultiStarSystem.name.c_str(),
                                activeMultiStarSystem.isRealSystem ? "real" : "procedural");
            ImGui::TextWrapped("%s", activeMultiStarSystem.notes.c_str());
            for (size_t i = 0; i < activeMultiStarSystem.members.size(); ++i) {
                const Universe::StarMember& member = activeMultiStarSystem.members[i];
                ImGui::BulletText("%s -- %.2g M_sun, %.0f K", member.name.c_str(), member.massSolar, member.temperatureK);
            }
        }
    };

    // --- Binary black holes (approximate, see Physics/BinaryGeodesicSolver.h) ---
    bool binaryModeEnabled = false;
    Physics::BinarySystem binarySystem;
    bool binaryOrbitAnimated = true;

    // --- Extra black holes: freely placed anywhere (not constrained to
    // BinarySystem's circular orbit), via the free-fly camera -- "fly
    // there, spawn here" -- see Physics/MultiBlackHoleSystem.h and
    // Physics/MultiBodyGeodesicSolver.h for the generalized N-body lensing
    // this activates once 3+ bodies (primary + binary's second body if
    // also active + these) are in the scene. Capped at
    // Physics::kMaxLensingBodies - 2 (reserving one slot for the primary
    // and one for binary mode's second body, even though both aren't
    // always present) so the fixed-size arrays in MultiBodyGeodesicSolver
    // never overflow.
    std::vector<Physics::ExtraBlackHole> extraBlackHoles;
    constexpr int kMaxExtraBlackHoles = Physics::kMaxLensingBodies - 2;
    double extraBlackHoleMassSolar = blackHole.MassSolarMasses() * 0.5;
    double spawnDistanceFromCamera = 15.0; // sim units, shared by every "spawn here" control below

    // Spawns at wherever the free-fly camera is currently looking --
    // Rendering::Camera::Position()/Forward() are plain const methods that
    // work regardless of camera mode, so this needs no new camera/picking
    // infrastructure. Static by default (ExtraBlackHole::velocity = 0) --
    // see its own comment for why nothing pulls on it.
    auto SpawnExtraBlackHole = [&]() {
        if (static_cast<int>(extraBlackHoles.size()) >= kMaxExtraBlackHoles) return;
        Physics::ExtraBlackHole hole;
        hole.gmRatio = extraBlackHoleMassSolar / blackHole.MassSolarMasses();
        hole.position = camera.Position() + camera.Forward() * spawnDistanceFromCamera;
        extraBlackHoles.push_back(hole);
    };

    // --- Mutual gravity: lets spawned stars/planets pull on EACH OTHER
    // (object-level -- each is reduced to a single point mass at its own
    // center of mass, not full particle-to-particle N-body), so two
    // spawned bodies can settle into a real binary-star-like mutual orbit
    // instead of only ever responding to the black hole(s). Off by
    // default -- existing scenes/presets behave identically unless this is
    // explicitly turned on. See Physics/GravitySource.h for why this uses
    // a deliberately exaggerated, tuned force law rather than the real
    // (and, at any black hole mass this app allows, utterly invisible)
    // physical mutual gravity.
    bool mutualGravityEnabled = false;
    double mutualGravityStrength = 1.0;

    ScenarioParams scenarioParams;
    NewtonianSimulationConfig simConfig;
    simConfig.timeStep = 0.02;
    simConfig.outerBoundary = 50.0;
    simConfig.maxSteps = 20000;

    NewtonianSimulation simulation;
    simulation.Reset(BuildScenario(scenarioParams), simConfig);

    std::vector<PhotonTraceResult> photonTraces = BuildPhotonTraces(scenarioParams);

    bool paused = false;
    int stepsPerFrame = 2;
    bool showGrid = true;
    bool showWarpedGrid = false; // Flamm's-paraboloid funnel surface, see Renderer::DrawFlammParaboloid
    bool showAnalyticCurve = true;
    bool showHorizon = true;
    bool showNewtonianTrajectories = true;
    bool showPhotonGeodesics = true;
    bool showPhotonSphere = true;
    bool showPhotonDebugTable = false;

    // --- Stage 3: gravitational lensing render state ---
    bool viewLensedRender = true; // start on the rendered black hole, not an empty debug grid

    // The traced image is always rendered at (window size * renderScalePercent/100),
    // matching the window's own aspect ratio exactly -- rendering a fixed
    // square buffer (e.g. 1024x1024) and stretching it to fill a
    // non-square window distorts circles into ellipses, which is exactly
    // the "stretched" artifact this replaces. 100% = one traced ray per
    // screen pixel (native resolution); lower values trade resolution for
    // speed on slower hardware, without ever touching geodesic precision
    // (see PerformRender -- step budget/angular step are always the same,
    // full-precision values, whether the camera is moving or not).
    double renderScalePercent = 80.0; // 10-100 -- full precision is mandatory (see above), so this is the main speed knob
    int maxRenderPixels = kDefaultMaxRenderPixels; // advanced-mode override of the ray budget cap, see ComputeRenderResolution
    Astronomy::StarField::Settings starSettings;
    Astronomy::StarField starField(starSettings);
    unsigned int lensedTexture = 0;
    int lensedTextureWidth = 0, lensedTextureHeight = 0;
    double lastRenderMilliseconds = 0.0;
    bool needsRerender = true;
    int fbWidth = 1280, fbHeight = 800; // updated every frame from the actual window framebuffer size

    // Live mode: re-traces the scene from the CURRENT camera every single
    // frame at FULL precision (same step budget/angular step as a one-off
    // "final" render always -- see PerformRender), so dragging/orbiting
    // shows the actual bent light in real time rather than a reduced-
    // accuracy preview. The cost of that precision is frame rate, not
    // correctness: renderScale and the GPU toggle are the two knobs that
    // trade rendered resolution for responsiveness if a scene feels slow.
    bool liveMode = true;
    double smoothedFrameMs = 16.0; // exponential moving average, for the FPS readout

    // --- Stage 7: CUDA acceleration ---
#ifdef BH_CUDA_AVAILABLE
    const bool cudaCompiledIn = true;
    const bool cudaDeviceAvailable = RayTracing::CudaAvailable();
#else
    const bool cudaCompiledIn = false;
    const bool cudaDeviceAvailable = false;
#endif
    // Always use the GPU when one is available -- no toggle: the CPU path
    // is dramatically slower for live rendering and there's no reason to
    // let it be selected by accident. Only ever falls back to CPU when no
    // CUDA device exists at all (or this build wasn't compiled with CUDA).
    const bool useGpu = cudaDeviceAvailable;
    bool lastRenderWasGpu = false;
    std::string cpuVsGpuReport;

    // --- Stage 4: Einstein ring test state ---
    bool useEinsteinRingSource = false;
    double sourceAngularRadius = 0.01; // radians
    double sourceBrightness = 3.0;
    bool showCameraDebugRays = false;
    int cameraDebugRayCount = 21;
    std::vector<PhotonTraceResult> cameraDebugRays;

    // --- Photon Inspector: a single, precisely-targeted ray, with its
    // full conserved quantities/path -- see TraceInspectorRay. Recomputed
    // live every frame the panel is open (one ray is negligible cost),
    // not cached behind a button, so dragging the NDC sliders updates the
    // readout and the 3D path immediately.
    bool showPhotonInspector = false;
    double inspectorNdcX = 0.0;
    double inspectorNdcY = 0.3;
    bool inspectorShowPath = true;
    PhotonTraceResult inspectorResult;

    // --- Scientific Plots: deflection-angle sweep state (see
    // BuildDeflectionSweep) -- button-triggered, not recomputed live like
    // the Photon Inspector above, since a sweep is dozens of full ray
    // traces (expensive in Kerr mode specifically, see HANDOFF.md Session
    // 5's lag fix) rather than just one. ---
    double plotBMin = 4.0;
    double plotBMax = 20.0;
    int plotSampleCount = 60;
    std::vector<DeflectionSample> deflectionSweep;

    // --- Stage 6: accretion disk state ---
    bool diskEnabled = true; // on by default -- a black hole with no visible accretion disk reads as "broken" rather than "quiet" to most people arriving fresh
    Astronomy::AccretionDiskSettings diskSettings;
    Astronomy::AccretionDisk accretionDisk(diskSettings, blackHole.MassKg());

    // Body 2's own disk, in binary mode -- same diskEnabled/diskSettings
    // toggle and relativistic-effects flag as body 1's above (no separate
    // UI needed), but its own AccretionDisk instance since it has its own
    // mass (-> its own real Kelvin scale) and its own radii, scaled from
    // diskSettings' values by body 2's Schwarzschild radius relative to
    // body 1's (see PerformRender).
    Astronomy::AccretionDisk accretionDisk2(diskSettings, blackHole.MassKg());

    // One more AccretionDisk per extra black hole (see extraBlackHoles
    // below) -- same reasoning as accretionDisk2's own comment, generalized
    // from "exactly one extra" to however many are currently placed. Kept
    // in sync with extraBlackHoles.size() inside PerformRender.
    std::vector<Astronomy::AccretionDisk> extraAccretionDisks;

    // --- Stars: user-spawned tidal-disruption test bodies (see
    // Astronomy/StarBody.h) -- each is a small cluster of particles that
    // free-falls under the same exact gravity law as the jet, so any
    // stretching/shredding on screen is a real consequence of differential
    // gravity, not a scripted animation.
    std::vector<Astronomy::StarBody> stars;
    double starSpawnRadius = 20.0;             // sim units
    double starOrbitalSpeedFraction = 0.55;    // fraction of the local Newtonian circular speed; <1 spirals/plunges in
    // Stars never had a configurable mass before mutual gravity needed one
    // (see Physics/GravitySource.h) -- defaults to 1 solar mass.
    double starSpawnMassSolar = 1.0;
    std::mt19937 starSpawnRng(std::random_device{}());
    auto SpawnStarAt = [&](const glm::dvec3& position, const glm::dvec3& velocity) {
        Astronomy::StarBodySettings settings;
        settings.totalMassKg = starSpawnMassSolar * Physics::Constants::SOLAR_MASS_KG;
        stars.emplace_back(position, velocity, settings);
    };
    auto SpawnStar = [&]() {
        // Equatorial plane, same convention as the accretion disk, at a
        // random azimuth so repeated spawns don't all land on top of each
        // other.
        std::uniform_real_distribution<double> azimuthDist(0.0, 2.0 * glm::pi<double>());
        const double azimuth = azimuthDist(starSpawnRng);
        const glm::dvec3 radial(std::cos(azimuth), 0.0, std::sin(azimuth));
        const glm::dvec3 position = radial * starSpawnRadius;
        const glm::dvec3 tangential(-std::sin(azimuth), 0.0, std::cos(azimuth));
        const double circularSpeed = std::sqrt(1.0 / starSpawnRadius); // Newtonian v_circ, GM=1 sim units
        const glm::dvec3 velocity = tangential * (circularSpeed * starOrbitalSpeedFraction);
        SpawnStarAt(position, velocity);
    };

    // "Spawn here": free-fly to wherever you want, drop a star there
    // instead of only at a random point on the fixed-radius ring above --
    // launchSpeed=0 just drops it in place; otherwise it's launched along
    // the camera's current forward direction, like tossing it into the
    // scene. Shares spawnDistanceFromCamera with the extra-black-hole and
    // planet "spawn here" controls.
    double starLaunchSpeed = 0.0;
    auto SpawnStarHere = [&]() {
        const glm::dvec3 position = camera.Position() + camera.Forward() * spawnDistanceFromCamera;
        const glm::dvec3 velocity = camera.Forward() * starLaunchSpeed;
        SpawnStarAt(position, velocity);
    };

    // --- Planets: user-spawned tidal-disruption bodies with an actual
    // spring-network cohesion model (see Astronomy/PlanetBody.h). Unlike
    // stars, a planet resists deformation until its internal springs are
    // stressed past the chosen material's strength, so whether/how it
    // fractures depends on the chosen composition and initial condition,
    // not just on distance from the hole.
    std::vector<Astronomy::PlanetBody> planets;
    // Tuned (not an arbitrary default) so the plain "Add Planet" button
    // actually shows a fracture within well under a minute of real time --
    // re-measured (scripted probe against PlanetBody directly) after a
    // correctness fix to the spring force's mass handling (see
    // ApplySpringForces's comment: it previously divided by each
    // particle's real KILOGRAM mass instead of its mass FRACTION of the
    // planet, making spring forces ~15-20 orders of magnitude weaker than
    // gravity -- i.e. springs were doing essentially nothing). With that
    // fixed, springs genuinely resist deformation now, so a planet needs a
    // noticeably closer/faster encounter than before to actually fracture.
    double planetSpawnRadius = 9.0;            // sim units
    double planetOrbitalSpeedFraction = 0.7;   // fraction of local Newtonian circular speed
    double planetMassEarths = 1.0;             // Earth masses
    double planetMaterialStrengthMultiplier = 1.0; // scales the chosen preset's materialStrength
    int planetParticleCount = 90;
    Physics::PlanetComposition planetComposition = Physics::PlanetComposition::Rocky;
    bool planetSelfGravityEnabled = true;
    double planetSelfGravityStrength = 1.0; // deliberately exaggerated/tuned, not physically exact -- see PlanetBody.h
    std::mt19937 planetSpawnRng(std::random_device{}());

    // Captured planetary debris temporarily brightens/heats the accretion
    // disk -- a simplified "tidal disruption flare" (spec 28.12/28.13),
    // not the real fallback-accretion light curve; see
    // TidalDisruptionFlare.h. Single/Kerr-hole scope only, same existing
    // limitation stars/planets already have for their horizon checks --
    // binary mode isn't coupled.
    Astronomy::TidalDisruptionFlare tidalFlare;

    constexpr double kEarthMassKg = 5.972e24;

    auto SpawnPlanetAt = [&](const glm::dvec3& position, const glm::dvec3& velocity) {
        Astronomy::PlanetBodySettings settings;
        settings.particleCount = planetParticleCount;
        settings.totalMassKg = planetMassEarths * kEarthMassKg;
        settings.material = Physics::PlanetMaterial::Preset(planetComposition);
        settings.material.materialStrength *= planetMaterialStrengthMultiplier;
        settings.selfGravityEnabled = planetSelfGravityEnabled;
        settings.selfGravityStrength = planetSelfGravityStrength;
        planets.emplace_back(position, velocity, settings);
    };
    auto SpawnPlanetWith = [&](double spawnRadius, double orbitalSpeedFraction) {
        std::uniform_real_distribution<double> azimuthDist(0.0, 2.0 * glm::pi<double>());
        const double azimuth = azimuthDist(planetSpawnRng);
        const glm::dvec3 radial(std::cos(azimuth), 0.0, std::sin(azimuth));
        const glm::dvec3 position = radial * spawnRadius;
        const glm::dvec3 tangential(-std::sin(azimuth), 0.0, std::cos(azimuth));
        const double circularSpeed = std::sqrt(1.0 / spawnRadius); // Newtonian v_circ, GM=1 sim units
        const glm::dvec3 velocity = tangential * (circularSpeed * orbitalSpeedFraction);
        SpawnPlanetAt(position, velocity);
    };
    auto SpawnPlanet = [&]() { SpawnPlanetWith(planetSpawnRadius, planetOrbitalSpeedFraction); };

    // "Spawn here" -- same free-fly-then-drop idea as SpawnStarHere.
    double planetLaunchSpeed = 0.0;
    auto SpawnPlanetHere = [&]() {
        const glm::dvec3 position = camera.Position() + camera.Forward() * spawnDistanceFromCamera;
        const glm::dvec3 velocity = camera.Forward() * planetLaunchSpeed;
        SpawnPlanetAt(position, velocity);
    };

    // One-click initial-condition presets (spec 28.19, subset): each sets
    // the spawn distance/speed/composition together and immediately spawns
    // a planet, rather than just filling in the sliders, so a click always
    // produces something to watch.
    const double planetIsco = 3.0 * Physics::BlackHole::SchwarzschildRadiusSim();
    auto SpawnPlanetPresetStableOrbit = [&]() {
        planetComposition = Physics::PlanetComposition::Rocky;
        SpawnPlanetWith(60.0, 1.0);
    };
    auto SpawnPlanetPresetCloseFlyby = [&]() {
        planetComposition = Physics::PlanetComposition::Rocky;
        SpawnPlanetWith(planetIsco * 2.5, 0.85);
    };
    // 7.0 (not an isco multiple -- just the closest-in, fastest-fracturing
    // point short of a radial plunge that was actually probed) is tuned
    // the same way planetSpawnRadius's default is -- see its comment.
    auto SpawnPlanetPresetRockyDisruption = [&]() {
        planetComposition = Physics::PlanetComposition::Rocky;
        SpawnPlanetWith(7.0, 0.5);
    };
    auto SpawnPlanetPresetIcyDisruption = [&]() {
        planetComposition = Physics::PlanetComposition::Icy;
        SpawnPlanetWith(7.0, 0.5);
    };
    auto SpawnPlanetPresetDirectInfall = [&]() {
        planetComposition = Physics::PlanetComposition::Rocky;
        SpawnPlanetWith(planetIsco * 3.0, 0.0);
    };

    double lastMouseX = 0.0, lastMouseY = 0.0;
    bool dragging = false;

    // --- Free-fly camera state ---
    // Free-fly movement needs an actual wall-clock frame delta (not
    // lastRenderMilliseconds, which measures ray-tracer cost and is only
    // known partway through the loop) so speed stays constant regardless of
    // frame rate.
    double lastFrameTime = glfwGetTime();
    double freeFlyMoveSpeed = 12.0; // sim units/sec at 1x -- Shift multiplies this
    bool freeFlyLooking = false;    // right mouse button held
    Rendering::Camera::Mode previousCameraMode = camera.mode; // detects a mode switch this frame, to avoid a mouse-delta jump

    // --- Jet piloting mode (see src/Jet/) ---
    // Spawned well outside the disk/photon sphere, matching the scale the
    // Orbit/FreeFly presets already use, so the black hole is immediately
    // visible without the jet starting dangerously close to the horizon.
    const glm::vec3 jetSpawnPosition(0.0f, 15.0f, 180.0f);
    Jet::FlightPhysics jetPhysics(jetSpawnPosition);
    Jet::InputHandler jetInput;
    Jet::FlightCamera jetFlightCamera;
    Jet::JetRenderer jetRenderer;
    bool jetRendererLoadAttempted = false; // load lazily on first use, see PerformRender's sibling below
    bool jetMouseCaptured = true; // Tab toggles this while piloting, see the Jet input block
    bool jetTabWasDown = false;   // edge-detects the Tab press above (GLFW has no "just pressed" query)
    bool jetCameraCycleKeyWasDown = false; // edge-detects the C press that cycles chase/cockpit view
    double jetMaxSpeedFractionOfC = jetPhysics.maxSpeedFractionOfC; // mirrors the slider below into FlightPhysics (SliderDouble needs a double*)

    // Resets the piloted jet to a sensible starting position/velocity/
    // camera for whichever view it's about to appear in -- shared by both
    // the "Pilot Endurance" button (App.cpp, further down) and
    // TravelToUniverseIndex below, so arriving somewhere while already
    // piloting the jet doesn't leave it wherever it drifted to in a
    // completely different view's coordinate scale (see that lambda's
    // comment for why that was a real, reported bug: a sim-units position
    // tens of units from a supermassive hole is effectively "far away and
    // invisible" if the next view is Star System View's 0-25-unit visual
    // scale, and vice versa).
    auto ResetJetForArrival = [&](const glm::vec3& position) {
        jetPhysics.Reset(position);
        jetFlightCamera = Jet::FlightCamera();
        jetFlightCamera.Update(jetPhysics.position, jetPhysics.rotation, 0.0f);
        camera.jetPosition = glm::dvec3(jetFlightCamera.position);
        camera.jetForward = glm::dvec3(glm::normalize(jetFlightCamera.target - jetFlightCamera.position));
        camera.jetUp = glm::dvec3(jetFlightCamera.up);
    };

    // Jumps to catalog entry `index`. BlackHoleObj entries re-point the
    // existing ray-traced scene at the target's real mass/spin (the same
    // scene, just a different real hole -- exactly what the Simple-mode
    // mass slider already does, just driven from catalog data instead of a
    // slider drag). Star entries switch to the simplified, non-ray-traced
    // "star system view" render branch (App::Run's main loop) since there
    // is no black-hole metric to trace around an ordinary star. Galaxy
    // entries are not travel targets (see StarCatalog.h) and are ignored.
    //
    // If the jet is currently being piloted, it's reset to a fresh position
    // appropriate to the DESTINATION view's own coordinate scale (and the
    // view itself keeps following it, i.e. the user keeps flying through
    // the jump) -- without this, the jet silently stayed wherever it was in
    // the OLD view's scale, which in the new view was either off in the
    // void (black hole sim-units, e.g. 180, seen from Star System View's
    // ~25-unit scale) or absurdly close to the star (Star System View's
    // ~20-unit position, seen from black-hole sim-units where that's deep
    // inside a normal orbit) -- a real reported bug, not a hypothetical.
    TravelToUniverseIndex = [&](int index) {
        if (index < 0 || index >= static_cast<int>(universeCatalog.size())) return;
        const Universe::CatalogObject& target = universeCatalog[static_cast<size_t>(index)];
        if (target.type == Universe::ObjectType::Galaxy) return;

        const double distanceLy = glm::length(target.CartesianLy() - UniverseCurrentPositionLy());
        char buffer[320];
        std::snprintf(buffer, sizeof(buffer),
                      "Arrived at %s -- %.4g light-years from the previous location. Light itself would take "
                      "%.4g years to make that trip: this jump is an instantaneous narrative convenience, "
                      "not simulated faster-than-light travel.",
                      target.name, distanceLy, distanceLy);
        lastTravelSummary = buffer;

        const bool wasPilotingJet = camera.mode == Rendering::Camera::Mode::Jet;

        if (target.type == Universe::ObjectType::BlackHoleObj) {
            blackHole.SetMassSolarMasses(target.massSolar);
            blackHoleMassSolar = target.massSolar;
            if (target.spin != Universe::kUnknownSpin) {
                blackHole.SetSpin(target.spin);
                blackHoleSpin = target.spin;
            }
            inStarSystemView = false;
            inMultiStarView = false;
            visitedStarIndex = -1;
            if (wasPilotingJet) {
                ResetJetForArrival(jetSpawnPosition);
                viewLensedRender = true; // the jet is only ever drawn in the ray-traced/overlay view, see "Pilot Endurance"
                needsRerender = true;
            } else {
                camera.mode = Rendering::Camera::Mode::Orbit;
                camera.distance = 30.0;
            }
        } else { // Star
            inStarSystemView = true;
            inMultiStarView = false;
            visitedStarIndex = index;
            if (wasPilotingJet) {
                // Just outside the outermost orbit ring Star System View
                // draws (kOutermostOrbitVisualRadius = 20 there), so the
                // jet starts with the whole system already in view.
                ResetJetForArrival(glm::vec3(0.0f, 3.0f, 23.0f));
            } else {
                camera.mode = Rendering::Camera::Mode::FreeFly;
                // Fixed viewing spot in Star System View's own arbitrary
                // visual scale (see that render branch's comment) --
                // independent of whatever sim-unit free-fly position was
                // last used near a black hole, which would be meaningless
                // here.
                camera.freeFlyPosition = glm::dvec3(0.0, 2.0, 25.0);
                camera.freeFlyYawRadians = 3.14159265358979323846;
                camera.freeFlyPitchRadians = 0.0;
            }
        }
        universeLocationIndex = index;
        currentLocationLy = target.CartesianLy();
        currentLocationName = target.name;
    };

    // Jumps to a multi-star system (Universe/MultiStarSystem.h) -- see that
    // file and the Universe panel's own description for the real-vs-
    // procedural and 2-body-vs-3+-body distinctions. Initializes a REAL
    // N-body state (positions centered on the barycenter, velocities from
    // the tuned mutual-gravity law in Physics/GravitySource.h -- the same
    // law, and the same visual pacing, the existing "Mutual Gravity"
    // feature already validates) that the main loop integrates every frame
    // thereafter (see the per-frame update block and the dedicated render
    // branch, both further down).
    TravelToMultiStarSystem = [&](const Universe::MultiStarSystemEntry& entry) {
        activeMultiStarSystem = entry;
        inMultiStarView = true;
        inStarSystemView = false;
        visitedStarIndex = -1;

        const size_t n = entry.members.size();
        multiStarPositions.assign(n, glm::dvec3(0.0));
        multiStarVelocities.assign(n, glm::dvec3(0.0));
        multiStarTextureIds.assign(n, 0u);

        // Initial layout: evenly spaced on a circle (arbitrary visual
        // scale, same spirit as Star System View's own single-star
        // scale), radius scaled slightly with member count so more stars
        // don't start overlapping. Shifted afterward so the MASS-WEIGHTED
        // centroid sits exactly at the origin, regardless of this
        // arbitrary starting shape -- a real two-body/N-body barycenter
        // is defined that way, not assumed true of an arbitrary circle.
        const double baseRadius = 6.0 + 1.5 * static_cast<double>(n);
        double totalMass = 0.0;
        glm::dvec3 massWeightedCentroid(0.0);
        for (size_t i = 0; i < n; ++i) {
            const double angle = 2.0 * glm::pi<double>() * static_cast<double>(i) / static_cast<double>(n);
            multiStarPositions[i] = glm::dvec3(baseRadius * std::cos(angle), 0.0, baseRadius * std::sin(angle));
            totalMass += entry.members[i].massSolar;
            massWeightedCentroid += multiStarPositions[i] * entry.members[i].massSolar;
        }
        massWeightedCentroid /= totalMass;
        for (size_t i = 0; i < n; ++i) {
            multiStarPositions[i] -= massWeightedCentroid;
        }

        // Initial velocities: a tangential kick sized from this star's OWN
        // tuned mutual-gravity acceleration at its starting radius (a
        // local circular-orbit estimate, not an exact N-body solution --
        // no such thing exists in closed form for N>=3, see the panel's
        // own description) -- gives the system a coherent initial
        // rotation to integrate from, rather than starting at rest and
        // immediately collapsing radially inward. All in the same
        // rotational sense (counrterclockwise in the XZ plane) so the
        // system doesn't immediately tear itself apart from colliding
        // initial velocities. Net momentum is then zeroed (subtract the
        // mass-weighted mean velocity) so the whole system's barycenter
        // stays put at the origin instead of drifting off-screen.
        std::vector<Physics::GravitySource> allSources;
        allSources.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            allSources.push_back({multiStarPositions[i], entry.members[i].massSolar * Physics::Constants::SOLAR_MASS_KG});
        }
        glm::dvec3 massWeightedVelocity(0.0);
        for (size_t i = 0; i < n; ++i) {
            std::vector<Physics::GravitySource> others;
            others.reserve(n - 1);
            for (size_t j = 0; j < n; ++j) {
                if (j != i) others.push_back(allSources[j]);
            }
            const glm::dvec3 accel = Physics::MutualGravityAccelSim(multiStarPositions[i], others, 1.0);
            const double r = glm::length(multiStarPositions[i]);
            const double speed = (r > 1e-6) ? std::sqrt(glm::length(accel) * r) : 0.0;
            const glm::dvec3 radialDir = (r > 1e-6) ? multiStarPositions[i] / r : glm::dvec3(1.0, 0.0, 0.0);
            const glm::dvec3 tangentDir(-radialDir.z, 0.0, radialDir.x); // 90-degree rotation in the XZ plane
            multiStarVelocities[i] = tangentDir * speed;
            massWeightedVelocity += multiStarVelocities[i] * entry.members[i].massSolar;
        }
        massWeightedVelocity /= totalMass;
        for (size_t i = 0; i < n; ++i) {
            multiStarVelocities[i] -= massWeightedVelocity;
        }

        // Textures: one real-temperature-tinted procedural texture per
        // member (Astronomy::GenerateStarTexture), generated immediately
        // rather than lazily, since every member needs one up front (the
        // single-star Star System View's lazy single-texture cache
        // doesn't generalize cleanly to a variable-length member list).
        for (size_t i = 0; i < n; ++i) {
            std::vector<glm::vec3> texturePixels =
                Astronomy::GenerateStarTexture(entry.members[i].temperatureK, static_cast<unsigned int>(i + 1));
            for (glm::vec3& pixel : texturePixels) {
                pixel = glm::clamp(pixel * 1.4f, 0.0f, 1.0f);
            }
            multiStarTextureIds[i] = renderer.UploadImage(texturePixels, 256, 128, 0);
        }

        if (entry.isRealSystem) {
            const Universe::CatalogObject proxy{entry.name.c_str(), Universe::ObjectType::Star, entry.raHours,
                                                 entry.decDegrees, entry.distanceLy, 0.0, 0.0, Universe::kUnknownSpin, ""};
            const double distanceLy = glm::length(proxy.CartesianLy() - currentLocationLy);
            char buffer[320];
            std::snprintf(buffer, sizeof(buffer),
                          "Arrived at %s -- %.4g light-years from the previous location. Light itself would take "
                          "%.4g years to make that trip: this jump is an instantaneous narrative convenience, "
                          "not simulated faster-than-light travel.",
                          entry.name.c_str(), distanceLy, distanceLy);
            lastTravelSummary = buffer;
            currentLocationLy = proxy.CartesianLy();
        } else {
            lastTravelSummary =
                "Generated at your current location -- a procedural system has no real sky position, so this "
                "isn't a real travel jump the way visiting a cataloged object is.";
        }
        currentLocationName = entry.name;
        universeLocationIndex = -1; // no single catalog entry corresponds to a multi-star system

        const bool wasPilotingJet = camera.mode == Rendering::Camera::Mode::Jet;
        if (wasPilotingJet) {
            ResetJetForArrival(glm::vec3(0.0f, 3.0f, static_cast<float>(baseRadius * 2.5)));
        } else {
            camera.mode = Rendering::Camera::Mode::FreeFly;
            camera.freeFlyPosition = glm::dvec3(0.0, 3.0, baseRadius * 2.5);
            camera.freeFlyYawRadians = 3.14159265358979323846;
            camera.freeFlyPitchRadians = 0.0;
        }
    };

#ifdef BH_CUDA_AVAILABLE
    if (cudaDeviceAvailable) {
        // A CUDA context's first-ever kernel launch in a process pays a
        // one-time driver/context initialization cost (observed ~150ms on
        // this machine) that has nothing to do with the tracer's actual
        // throughput. Absorbing that cost here, before the user's first
        // real render, keeps the in-UI GPU timing meaningful instead of
        // making the first click look misleadingly slow.
        RayTracing::RenderSettings warmup;
        warmup.width = 4;
        warmup.height = 4;
        RayTracing::CameraFrame warmupFrame;
        warmupFrame.position = glm::dvec3(0.0, 0.0, 30.0);
        (void)RayTracing::RenderImageCUDA(warmupFrame, warmup, [](const glm::dvec3&) { return glm::vec3(0.0f); });
    }
#endif

    // Performs one lensed render using whatever the current UI state is
    // (camera, resolution scale, disk/Einstein-ring settings, CPU/GPU
    // choice) and uploads the result. Defined once here (not inlined at
    // each call site) so BOTH the Simple-mode presets (which just flip
    // flags and rely on the automatic trigger below) and the Advanced
    // "Render" button actually produce an image -- earlier this logic
    // lived only inside the Advanced panel's UI code, so Simple-mode
    // presets silently never rendered anything.
    //
    // Always full geodesic precision (maxSteps/basePhiStep never change
    // whether the camera is moving or still) -- resolution/frame rate is
    // the only thing renderScale and the GPU toggle trade off, never the
    // accuracy of the light paths themselves.
    const auto PerformRender = [&]() {
        const auto [w, h] = ComputeRenderResolution(fbWidth, fbHeight, renderScalePercent, maxRenderPixels);

        RayTracing::RenderSettings renderSettings;
        renderSettings.width = w;
        renderSettings.height = h;
        renderSettings.geodesicConfig.escapeRadius = 1.0e4;
        renderSettings.geodesicConfig.maxSteps = 8000;
        renderSettings.geodesicConfig.basePhiStep = 0.015;
        renderSettings.geodesicConfig.checkDisk = diskEnabled;
        renderSettings.geodesicConfig.diskInnerRadius = diskSettings.innerRadius;
        renderSettings.geodesicConfig.diskOuterRadius = diskSettings.outerRadius;
        renderSettings.geodesicConfig.diskClockwise = diskSettings.clockwise;

        // Kerr mode (see Advanced > Spacetime): routes every pixel through
        // the Kerr geodesic solver instead of the Schwarzschild one, on
        // either CPU (RayTracing::RenderImage) or GPU (RenderImageCUDA ->
        // TraceKerrPhotonsKernel) -- see HANDOFF.md Session 4.
        renderSettings.spin = blackHole.Spin();
        renderSettings.kerrConfig.escapeRadius = renderSettings.geodesicConfig.escapeRadius;
        renderSettings.kerrConfig.maxSteps = kKerrLiveMaxSteps;
        renderSettings.kerrConfig.baseLambdaStep = 0.05; // looser than the 0.02 default -- same speed/accuracy tradeoff as geodesicConfig.basePhiStep above
        renderSettings.kerrConfig.checkDisk = diskEnabled;
        renderSettings.kerrConfig.diskInnerRadius = diskSettings.innerRadius;
        renderSettings.kerrConfig.diskOuterRadius = diskSettings.outerRadius;

        // Binary black holes (see Advanced > Binary Black Holes): takes
        // priority over Kerr/Schwarzschild when enabled -- a rotating
        // member of a binary isn't modeled. Each body gets its own disk,
        // reusing the SAME diskEnabled/diskSettings the single-body modes
        // use (see accretionDisk2's declaration) rather than a separate
        // set of sliders -- body 2's radii are scaled by its Schwarzschild
        // radius relative to body 1's, preserving the same "radius in
        // units of r_s" the user actually set.
        renderSettings.binaryEnabled = binaryModeEnabled;
        renderSettings.binarySystem = binarySystem;
        renderSettings.binaryConfig.escapeRadius = kBinaryLiveEscapeRadius;
        renderSettings.binaryConfig.maxSteps = kBinaryLiveMaxSteps;
        renderSettings.binaryConfig.checkDisk = diskEnabled;
        renderSettings.binaryConfig.disk1InnerRadius = diskSettings.innerRadius;
        renderSettings.binaryConfig.disk1OuterRadius = diskSettings.outerRadius;
        {
            const double rs2OverRs1 = binarySystem.massRatio; // SchwarzschildRadius2()/SchwarzschildRadius1()
            renderSettings.binaryConfig.disk2InnerRadius = diskSettings.innerRadius * rs2OverRs1;
            renderSettings.binaryConfig.disk2OuterRadius = diskSettings.outerRadius * rs2OverRs1;
        }

        // Extra black holes (see Physics/MultiBlackHoleSystem.h): once 3+
        // bodies are active (primary + binary's second body if also on +
        // these), CPUTracer.cpp routes through Physics::TracePhotonMultiBody
        // instead of the chain above -- same index order (0=primary,
        // 1=binary's second body if binaryModeEnabled, then extras) as
        // CPUTracer.cpp's own BuildLensingBodies, so multiBodyConfig's
        // per-index disk radii below line up with whatever body each index
        // actually turns out to be.
        renderSettings.extraBlackHoles = extraBlackHoles;
        renderSettings.multiBodyConfig.escapeRadius = kBinaryLiveEscapeRadius;
        renderSettings.multiBodyConfig.maxSteps = kBinaryLiveMaxSteps;
        renderSettings.multiBodyConfig.checkDisk = diskEnabled;
        {
            int bodyIndex = 0;
            renderSettings.multiBodyConfig.diskInnerRadius[static_cast<size_t>(bodyIndex)] = diskSettings.innerRadius;
            renderSettings.multiBodyConfig.diskOuterRadius[static_cast<size_t>(bodyIndex)] = diskSettings.outerRadius;
            ++bodyIndex;
            if (binaryModeEnabled) {
                renderSettings.multiBodyConfig.diskInnerRadius[static_cast<size_t>(bodyIndex)] =
                    renderSettings.binaryConfig.disk2InnerRadius;
                renderSettings.multiBodyConfig.diskOuterRadius[static_cast<size_t>(bodyIndex)] =
                    renderSettings.binaryConfig.disk2OuterRadius;
                ++bodyIndex;
            }
            for (const Physics::ExtraBlackHole& extra : extraBlackHoles) {
                if (bodyIndex >= Physics::kMaxLensingBodies) break;
                renderSettings.multiBodyConfig.diskInnerRadius[static_cast<size_t>(bodyIndex)] =
                    diskSettings.innerRadius * extra.gmRatio;
                renderSettings.multiBodyConfig.diskOuterRadius[static_cast<size_t>(bodyIndex)] =
                    diskSettings.outerRadius * extra.gmRatio;
                ++bodyIndex;
            }
        }

        const RayTracing::CameraFrame frame = BuildCameraFrame(camera, w, h);
        const glm::dvec3 sourceDirection = frame.forward; // Stage 4: always collinear, see SamplePointSource

        starField.SetSettings(starSettings);
        const RayTracing::BackgroundSampler background =
            useEinsteinRingSource
                ? RayTracing::BackgroundSampler([&](const glm::dvec3& dir) {
                      return SamplePointSource(dir, sourceDirection, sourceAngularRadius, sourceBrightness);
                  })
                : RayTracing::BackgroundSampler([&](const glm::dvec3& dir) { return starField.Sample(dir); });

        {
            // Tidal disruption flare boost applied on top of the user's
            // own slider-backed diskSettings, never mutating it directly --
            // the boost disappears cleanly as it decays without disturbing
            // their chosen baseline. Single/Kerr-hole scope only (not
            // added to disk2Settings below) -- see tidalFlare's own comment.
            Astronomy::AccretionDiskSettings effectiveDiskSettings = diskSettings;
            effectiveDiskSettings.eddingtonFraction += tidalFlare.EddingtonFractionBoost();
            accretionDisk.SetSettings(effectiveDiskSettings);
        }
        accretionDisk.SetBlackHoleMassKg(blackHole.MassKg());
        {
            Astronomy::AccretionDiskSettings disk2Settings = diskSettings;
            disk2Settings.innerRadius = renderSettings.binaryConfig.disk2InnerRadius;
            disk2Settings.outerRadius = renderSettings.binaryConfig.disk2OuterRadius;
            accretionDisk2.SetSettings(disk2Settings);
            accretionDisk2.SetBlackHoleMassKg(blackHole.MassKg() * binarySystem.massRatio);
        }

        // Index order matches CPUTracer.cpp's BuildLensingBodies exactly
        // (0=primary, 1=binary's second body if active, then extras) --
        // diskByBodyIndex[i] is the AccretionDisk whichever solver's
        // result.diskBodyIndex==i refers to. Only relevant when the
        // multi-body solver is in play (3+ bodies); with 1-2 bodies the
        // existing ternary below (index 0 or 1) already covers it, kept
        // as-is since it's exactly indices 0/1 of this same list.
        extraAccretionDisks.resize(extraBlackHoles.size());
        std::vector<Astronomy::AccretionDisk*> diskByBodyIndex;
        diskByBodyIndex.push_back(&accretionDisk);
        if (binaryModeEnabled) diskByBodyIndex.push_back(&accretionDisk2);
        for (size_t i = 0; i < extraBlackHoles.size(); ++i) {
            Astronomy::AccretionDiskSettings extraDiskSettings = diskSettings;
            extraDiskSettings.innerRadius = diskSettings.innerRadius * extraBlackHoles[i].gmRatio;
            extraDiskSettings.outerRadius = diskSettings.outerRadius * extraBlackHoles[i].gmRatio;
            extraAccretionDisks[i].SetSettings(extraDiskSettings);
            extraAccretionDisks[i].SetBlackHoleMassKg(blackHole.MassKg() * extraBlackHoles[i].gmRatio);
            diskByBodyIndex.push_back(&extraAccretionDisks[i]);
        }

        const RayTracing::DiskSampler diskSampler = [&](const PhotonTraceResult& hit) {
            const Astronomy::AccretionDisk& whichDisk =
                (hit.diskBodyIndex >= 0 && hit.diskBodyIndex < static_cast<int>(diskByBodyIndex.size()))
                    ? *diskByBodyIndex[static_cast<size_t>(hit.diskBodyIndex)]
                    : accretionDisk;
            const double emittedTemperature = whichDisk.Temperature(hit.diskHitRadius);
            const double observedTemperature =
                diskSettings.relativisticEffects ? emittedTemperature * hit.diskRedshiftFactor : emittedTemperature;
            return whichDisk.EmittedColor(observedTemperature);
        };

        // The GPU kernels don't know about extra black holes at all (see
        // Physics/MultiBodyGeodesicSolver.h) -- rather than silently
        // ignoring them, a frame with any extra black holes placed always
        // renders on CPU, same "fall back instead of running the wrong
        // physics fast" precedent PlanetBody::UpdateGPU already set for
        // binary mode.
        const bool useGpuThisFrame = useGpu && cudaDeviceAvailable && extraBlackHoles.empty();

        const auto t0 = std::chrono::steady_clock::now();
        std::vector<glm::vec3> pixels;
#ifdef BH_CUDA_AVAILABLE
        if (useGpuThisFrame) {
            pixels = RayTracing::RenderImageCUDA(frame, renderSettings, background, diskSampler);
        } else
#endif
        {
            pixels = RayTracing::RenderImage(frame, renderSettings, background, diskSampler);
        }
        const auto t1 = std::chrono::steady_clock::now();
        lastRenderMilliseconds = std::chrono::duration<double, std::milli>(t1 - t0).count();
        lastRenderWasGpu = useGpuThisFrame;

        lensedTexture = renderer.UploadImage(pixels, w, h, lensedTexture);
        lensedTextureWidth = w;
        lensedTextureHeight = h;
    };

    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        const double now = glfwGetTime();
        const double dt = std::clamp(now - lastFrameTime, 0.0, 0.1); // clamp guards against a huge dt after a stall/breakpoint
        lastFrameTime = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const bool cameraModeJustChanged = (camera.mode != previousCameraMode);
        previousCameraMode = camera.mode;

        const ImGuiIO& io = ImGui::GetIO();

        // "M" toggles the standalone Universe/Star Map window -- checked
        // before the per-camera-mode input blocks below so it works
        // regardless of which one is active (including while the jet's own
        // Tab/mouse-capture handling is running).
        const bool mDown = !io.WantCaptureKeyboard && glfwGetKey(m_window, GLFW_KEY_M) == GLFW_PRESS;
        if (mDown && !universeMapKeyWasDown) {
            showUniverseMapWindow = !showUniverseMapWindow;
        }
        universeMapKeyWasDown = mDown;

        if (camera.mode == Rendering::Camera::Mode::Jet) {
            // --- Jet piloting controls: continuous mouse-look by default ---
            // (cursor hidden/captured, mouse acts as a virtual joystick,
            // matching the source project's HOTAS-style control feel), WASD
            // pitch/roll, Q/E yaw, Shift/Ctrl for thrust (Ctrl past zero is
            // reverse thrust). Tab toggles the
            // cursor free so the ImGui panel (camera-view switch, etc.) can
            // be clicked without leaving Jet mode -- flight keyboard
            // controls keep working either way, only mouse-look pauses.
            // See Jet::InputHandler/Jet::FlightPhysics.
            if (cameraModeJustChanged) {
                jetMouseCaptured = true;
            }
            const bool tabDown = glfwGetKey(m_window, GLFW_KEY_TAB) == GLFW_PRESS;
            if (tabDown && !jetTabWasDown) {
                jetMouseCaptured = !jetMouseCaptured;
            }
            jetTabWasDown = tabDown;

            // C cycles Chase/Cockpit view, same key as the source project.
            const bool cDown = !io.WantCaptureKeyboard && glfwGetKey(m_window, GLFW_KEY_C) == GLFW_PRESS;
            if (cDown && !jetCameraCycleKeyWasDown) {
                jetFlightCamera.CycleMode();
            }
            jetCameraCycleKeyWasDown = cDown;

            glfwSetInputMode(m_window, GLFW_CURSOR, jetMouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            freeFlyLooking = false;

            double mx, my;
            glfwGetCursorPos(m_window, &mx, &my);
            if (jetMouseCaptured && !cameraModeJustChanged && !io.WantCaptureMouse) {
                jetInput.ProcessMouse(static_cast<float>(mx - lastMouseX), static_cast<float>(my - lastMouseY),
                                       jetPhysics, static_cast<float>(dt));
            }
            lastMouseX = mx;
            lastMouseY = my;

            if (!io.WantCaptureKeyboard) {
                jetInput.ProcessKeyboard(m_window, jetPhysics, static_cast<float>(dt));
            }

            jetPhysics.Update(static_cast<float>(dt), Physics::Constants::GravitationalRadiusMeters(blackHole.MassKg()));
            if (jetPhysics.capturedByBlackHole) {
                // Respawn at a position appropriate to whichever view is
                // CURRENTLY showing -- jetSpawnPosition (180 sim units) is
                // the black-hole-view default; Star System View's own
                // much smaller visual scale needs its own respawn point,
                // same reasoning as TravelToUniverseIndex's ResetJetForArrival.
                jetPhysics.Reset(inStarSystemView ? glm::vec3(0.0f, 3.0f, 23.0f) : jetSpawnPosition);
            }

            jetFlightCamera.Update(jetPhysics.position, jetPhysics.rotation, static_cast<float>(dt));
            camera.jetPosition = glm::dvec3(jetFlightCamera.position);
            camera.jetForward = glm::dvec3(glm::normalize(jetFlightCamera.target - jetFlightCamera.position));
            camera.jetUp = glm::dvec3(jetFlightCamera.up);
        } else if (camera.mode == Rendering::Camera::Mode::Orbit) {
            // --- Orbit controls: mouse drag = orbit, scroll = zoom, ---
            // only active when ImGui isn't consuming the mouse.
            freeFlyLooking = false;
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

            if (!io.WantCaptureMouse) {
                double mx, my;
                glfwGetCursorPos(m_window, &mx, &my);
                const bool leftDown = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

                if (leftDown && !dragging) {
                    dragging = true;
                } else if (leftDown && dragging) {
                    camera.yawRadians += (mx - lastMouseX) * 0.01;
                    camera.pitchRadians = std::clamp(camera.pitchRadians - (my - lastMouseY) * 0.01, -1.5, 1.5);
                } else {
                    dragging = false;
                }
                lastMouseX = mx;
                lastMouseY = my;

                camera.distance = std::clamp(camera.distance - io.MouseWheel * 2.0, 5.0, 300.0);
            }
        } else {
            // --- Free-fly controls: hold right mouse button to look ---
            // around, WASD to move, Space/Ctrl for up/down, Shift to move
            // faster. Cursor is only captured/hidden while actually
            // looking, so ImGui panels stay fully usable otherwise.
            double mx, my;
            glfwGetCursorPos(m_window, &mx, &my);
            const bool rightDown = !io.WantCaptureMouse &&
                                    glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

            if (rightDown) {
                if (freeFlyLooking) {
                    // Inverted from a naive "+=": Camera::Forward() for
                    // FreeFly is (sin(yaw), ..., cos(yaw)), and
                    // OrthonormalBasis() defines `right` as
                    // cross(forward, up) -- working through those two
                    // formulas together, INCREASING yaw actually rotates
                    // Forward() toward the LEFT of that basis.right, not
                    // the right. A naive "+=" on mouse-right-movement
                    // therefore turned the view left, and vice versa --
                    // reported directly by the user, confirmed by tracing
                    // the math rather than just flipping the sign and
                    // hoping. Subtracting here makes mouse-right actually
                    // turn the view right.
                    camera.freeFlyYawRadians -= (mx - lastMouseX) * 0.005;
                    camera.freeFlyPitchRadians =
                        std::clamp(camera.freeFlyPitchRadians - (my - lastMouseY) * 0.005, -1.5, 1.5);
                }
                freeFlyLooking = true;
                glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            } else {
                freeFlyLooking = false;
                glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
            lastMouseX = mx;
            lastMouseY = my;

            if (!io.WantCaptureKeyboard) {
                const Rendering::Camera::Basis basis = camera.OrthonormalBasis();
                double speed = freeFlyMoveSpeed * dt;
                if (glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
                    speed *= 4.0;
                }
                if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) camera.freeFlyPosition += basis.forward * speed;
                if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) camera.freeFlyPosition -= basis.forward * speed;
                if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) camera.freeFlyPosition -= basis.right * speed;
                if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) camera.freeFlyPosition += basis.right * speed;
                if (glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.freeFlyPosition.y += speed;
                if (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.freeFlyPosition.y -= speed;
            }
        }

        glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);

        // --- ImGui control panel ---
        ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Black Hole Simulator");

        // --- Mode switcher: Simple (presets, plain language) vs Advanced ---
        // (the full Stage 1-7 scientific control panel, unchanged below).
        {
            const float fullWidth = ImGui::GetContentRegionAvail().x;
            const float halfWidth = (fullWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

            if (uiMode == UiMode::Simple) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            }
            if (ImGui::Button("Simple", ImVec2(halfWidth, 0))) uiMode = UiMode::Simple;
            if (uiMode == UiMode::Simple) ImGui::PopStyleColor();

            ImGui::SameLine();
            if (uiMode == UiMode::Advanced) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            }
            if (ImGui::Button("Advanced (all controls)", ImVec2(halfWidth, 0))) uiMode = UiMode::Advanced;
            if (uiMode == UiMode::Advanced) ImGui::PopStyleColor();
        }
        ImGui::Separator();

        // --- Camera mode: Orbit (locked onto the black hole, what every ---
        // preset/scientific control assumes) vs FreeFly (noclip fly-around)
        // vs Jet (pilot a fighter jet, with real thrust/drag/gravity -- see
        // src/Jet/).
        {
            const float fullWidth = ImGui::GetContentRegionAvail().x;
            const float thirdWidth = (fullWidth - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
            const bool isOrbit = camera.mode == Rendering::Camera::Mode::Orbit;
            const bool isFreeFly = camera.mode == Rendering::Camera::Mode::FreeFly;
            const bool isJet = camera.mode == Rendering::Camera::Mode::Jet;

            if (isOrbit) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button("Orbit Camera", ImVec2(thirdWidth, 0))) camera.mode = Rendering::Camera::Mode::Orbit;
            if (isOrbit) ImGui::PopStyleColor();

            ImGui::SameLine();
            if (isFreeFly) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button("Free-Fly Camera", ImVec2(thirdWidth, 0))) {
                if (isOrbit) {
                    // Start free-flying from wherever the orbit camera
                    // currently is, looking at the black hole, so switching
                    // modes doesn't jar the view. Orbit's Forward() is
                    // -Position() (i.e. yaw/pitch rotated 180 degrees from
                    // FreeFly's own yaw/pitch convention), so the angles
                    // need that same flip here, not a direct copy.
                    camera.freeFlyPosition = camera.Position();
                    camera.freeFlyYawRadians = camera.yawRadians + glm::pi<double>();
                    camera.freeFlyPitchRadians = -camera.pitchRadians;
                }
                camera.mode = Rendering::Camera::Mode::FreeFly;
            }
            if (isFreeFly) ImGui::PopStyleColor();

            ImGui::SameLine();
            if (isJet) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button("Pilot Endurance", ImVec2(thirdWidth, 0))) {
                if (!jetRendererLoadAttempted) {
                    jetRendererLoadAttempted = true;
                    if (!jetRenderer.Load("resources/jet/endurance.glb", "resources/jet/endurance_basecolor.png", 3.0f)) {
                        std::fprintf(stderr, "Jet mode: model/texture failed to load -- piloting still works, but the jet won't be visible.\n");
                    }
                }
                // The jet is only ever drawn/tracked in the ray-traced view
                // (see the composite draw below) -- force it on so clicking
                // this button always shows the jet, even if the debug 3D
                // view (e.g. from the "Newtonian vs Einstein" preset) was
                // showing instead.
                viewLensedRender = true;
                needsRerender = true;
                jetMouseCaptured = true;

                // Always start from a clean, known-good flight state --
                // without this, re-entering Jet mode after the jet drifted
                // far away (or was reset by falling into the black hole)
                // kept whatever position/velocity/camera-smoothing state it
                // last had, which could look like "the camera isn't
                // following the jet" if that state was bad.
                jetPhysics.Reset(jetSpawnPosition);
                jetFlightCamera = Jet::FlightCamera();

                // Compute the camera's first frame synchronously (rather
                // than waiting for next frame's top-of-loop input block) so
                // there's no one-frame glitch where camera.jetPosition still
                // holds its stale default (the origin -- i.e. inside the
                // black hole) before this mode's own update code has run.
                jetFlightCamera.Update(jetPhysics.position, jetPhysics.rotation, 0.0f);
                camera.jetPosition = glm::dvec3(jetFlightCamera.position);
                camera.jetForward = glm::dvec3(glm::normalize(jetFlightCamera.target - jetFlightCamera.position));
                camera.jetUp = glm::dvec3(jetFlightCamera.up);
                camera.mode = Rendering::Camera::Mode::Jet;
            }
            if (isJet) ImGui::PopStyleColor();

            if (isFreeFly) {
                ImGui::TextDisabled("WASD to move, hold Right Mouse to look, Space/Ctrl for up/down, Shift to go faster.");
            } else if (isJet) {
                ImGui::TextDisabled("Mouse to look (virtual joystick), WASD pitch/roll, Q/E yaw, Shift/Ctrl thrust (Ctrl past zero reverses), Shift+W afterburner, C to cycle camera view, Tab to free the cursor.");
                if (ImGui::Button(jetFlightCamera.mode == Jet::FlightCameraMode::Chase ? "Switch to Cockpit View" : "Switch to Chase View")) {
                    jetFlightCamera.CycleMode();
                }
                const double jetMetersPerSimUnit = Physics::Constants::GravitationalRadiusMeters(blackHole.MassKg());
                const double jetSpeedMetersPerSec = jetPhysics.GetSpeed() * jetMetersPerSimUnit;
                const double jetSpeedC = jetSpeedMetersPerSec / Physics::Constants::C_SI;
                ImGui::Text("Speed: %.0f sim units/s = %.3gc   G: %.1f (peak %.1f)", jetPhysics.GetSpeed(),
                            jetSpeedC, jetPhysics.currentGForce, jetPhysics.peakGForce);
                // Jet::MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC (FlightPhysics.h)
                // floors the sim-units/s cap so flight never becomes
                // literally imperceptible near a supermassive hole -- but
                // that floor, unlike the fraction-of-c slider below, is NOT
                // a real-light-speed-respecting cap: at a mass heavy enough
                // that 0.2c-in-sim-units falls below the floor, the actual
                // speed shown above can read as many times c. Surfaced
                // here explicitly rather than silently shown as an honest
                // "0.2c" -- this app doesn't misrepresent its own physics,
                // even for a deliberate playability concession.
                const double relativisticCapSimUnits = (jetMaxSpeedFractionOfC * Physics::Constants::C_SI) / jetMetersPerSimUnit;
                if (relativisticCapSimUnits < Jet::MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC) {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                        "Speed floor active: this hole's mass makes %.3gc (%.2e sim units/s) imperceptibly slow, "
                                        "so flight is floored at %.0f sim units/s for playability -- that's %.2gc, well above the "
                                        "slider's real-light-speed intent. A deliberate, documented concession (see FlightPhysics.h), "
                                        "not a claim this is really sub-light.",
                                        jetMaxSpeedFractionOfC, relativisticCapSimUnits,
                                        Jet::MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC,
                                        (Jet::MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC * jetMetersPerSimUnit) / Physics::Constants::C_SI);
                }
                ImGui::TextDisabled("Jet pos: (%.1f, %.1f, %.1f)   Camera pos: (%.1f, %.1f, %.1f)",
                                     jetPhysics.position.x, jetPhysics.position.y, jetPhysics.position.z,
                                     camera.jetPosition.x, camera.jetPosition.y, camera.jetPosition.z);
                SliderDouble("Max speed (fraction of c)", &jetMaxSpeedFractionOfC, 0.001, 0.9, "%.3f");
                HelpTooltip("Hard ceiling on the jet's real speed, as a fraction of light speed -- this is what keeps flight from becoming 'cross several light-years in a few seconds' once the black hole is supermassive (one sim unit is much bigger in real terms for a hole like TON 618 than for a stellar-mass one, see Physics/Constants.h). Raise it if a heavier hole feels too slow to get anywhere; it can never reach or exceed 1.0 (actual light speed) BY ITSELF -- but see the playability floor warning above, which can override it at extreme masses.");
                jetPhysics.maxSpeedFractionOfC = static_cast<float>(jetMaxSpeedFractionOfC);
            }
        }
        ImGui::Separator();

        if (uiMode == UiMode::Simple) {
            ImGui::TextWrapped("Explore how a black hole bends light. Pick a scene below, then drag to look around.");

            // --- Preset scenes: each sets up camera + scene flags and ---
            // triggers a render, so one click is a complete demonstration.
            struct Preset {
                const char* name;
                const char* blurb;
            };
            static const Preset presets[] = {
                {"Black Hole", "A star field lensed around the shadow -- the baseline view."},
                {"Einstein Ring", "A light source hidden exactly behind the black hole, bent into a ring."},
                {"Accretion Disk", "A glowing disk of matter, its far side visibly wrapped around the shadow."},
                {"Newtonian vs Einstein", "Old-physics vs. real-physics light paths, side by side."},
            };

            SectionLabel("Scenes");
            const float presetWidth = ImGui::GetContentRegionAvail().x;
            for (const Preset& preset : presets) {
                const bool isActive = activePreset == preset.name;
                if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                const bool clicked = ImGui::Button(preset.name, ImVec2(presetWidth, 34));
                if (isActive) ImGui::PopStyleColor();
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::TextWrapped("%s", preset.blurb);
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 4));

                if (clicked) {
                    activePreset = preset.name;
                    diskEnabled = false;
                    useEinsteinRingSource = false;
                    showCameraDebugRays = false;
                    // Presets are all framed as Orbit shots (several rely on
                    // the camera pointing at the origin, e.g. the Einstein
                    // Ring's source-alignment) -- picking one always returns
                    // to Orbit mode even if the user was free-flying.
                    camera.mode = Rendering::Camera::Mode::Orbit;

                    if (std::string(preset.name) == "Black Hole") {
                        viewLensedRender = true;
                        camera.distance = 30.0;
                        camera.pitchRadians = 0.3;
                    } else if (std::string(preset.name) == "Einstein Ring") {
                        viewLensedRender = true;
                        useEinsteinRingSource = true;
                        sourceAngularRadius = 0.008;
                        camera.distance = 220.0;
                        camera.pitchRadians = 0.0;
                    } else if (std::string(preset.name) == "Accretion Disk") {
                        viewLensedRender = true;
                        diskEnabled = true;
                        camera.distance = 45.0;
                        camera.pitchRadians = 0.35;
                    } else if (std::string(preset.name) == "Newtonian vs Einstein") {
                        viewLensedRender = false;
                        showNewtonianTrajectories = true;
                        showPhotonGeodesics = true;
                        showGrid = true;
                        showAnalyticCurve = true;
                        camera.distance = 60.0;
                        camera.pitchRadians = 0.3;
                    }
                    needsRerender = true;
                }
            }

            if (viewLensedRender) {
                SectionLabel("Rendering");

                const double fps = smoothedFrameMs > 0.0 ? 1000.0 / smoothedFrameMs : 0.0;
                const ImVec4 fpsColor = fps >= 24.0 ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                                        : fps >= 10.0 ? ImVec4(0.95f, 0.8f, 0.2f, 1.0f)
                                                       : ImVec4(0.95f, 0.35f, 0.3f, 1.0f);
                ImGui::TextColored(fpsColor, "%.0f fps", fps);
                ImGui::SameLine();
                ImGui::TextDisabled("(%d x %d, full precision, re-traced every frame)", lensedTextureWidth, lensedTextureHeight);

                SliderDouble("Resolution", &renderScalePercent, 10.0, 100.0, "%.0f%%");
                ImGui::TextWrapped("Lower resolution = faster. This never reduces the physics precision -- only how many rays are traced.");
#ifdef BH_CUDA_AVAILABLE
                if (cudaDeviceAvailable) {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "GPU (CUDA) active");
                } else {
                    ImGui::TextDisabled("No CUDA GPU found -- running on CPU. Try a lower resolution if it feels slow.");
                }
#else
                ImGui::TextDisabled("Running on CPU (built without CUDA). Try a lower resolution if it feels slow.");
#endif
            }

            SectionLabel("Black Hole");
            if (SliderDouble("Mass (solar masses)", &blackHoleMassSolar, 1.0, 1.0e11, "%.2e")) {
                blackHole.SetMassSolarMasses(blackHoleMassSolar);
            }

            SectionLabel("Objects");
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);

            ImGui::BeginChild("##StarCard", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border);
            ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.4f, 1.0f), "Star");
            if (ImGui::Button("Add Star")) {
                SpawnStar();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Here")) {
                SpawnStarHere();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(stars.empty());
            if (ImGui::Button("Clear")) {
                stars.clear();
            }
            ImGui::EndDisabled();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped(
                "Drops a small star into orbit and watches real tidal gravity pull it apart -- no "
                "scripted stretching, just ~28 particles each falling under the exact same physics "
                "as everything else here. 'Add Here' drops it wherever the camera is currently "
                "looking (switch to Free-fly mode to fly somewhere first) instead of a random orbit.");
            ImGui::PopStyleColor();
            ImGui::EndChild();

            ImGui::Dummy(ImVec2(0, 6));

            ImGui::BeginChild("##PlanetCard", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border);
            ImGui::TextColored(ImVec4(0.55f, 0.8f, 0.95f, 1.0f), "Planet");
            if (ImGui::Button("Add Planet")) {
                SpawnPlanet();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Here")) {
                SpawnPlanetHere();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(planets.empty());
            if (ImGui::Button("Clear")) {
                planets.clear();
            }
            ImGui::EndDisabled();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped(
                "Drops a planet held together by an actual spring network -- it visibly stretches "
                "and, if tidal stress overcomes the chosen material's strength, fractures into "
                "tracked fragments. Switch to Advanced mode for material presets, encounter "
                "presets, and live diagnostics.");
            ImGui::PopStyleColor();
            ImGui::EndChild();

            ImGui::PopStyleColor(); // ChildBg

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("Tip: drag with the left mouse button to look around -- the light bends in real time as you move. Scroll to zoom.");
            ImGui::PopStyleColor();
            if (ImGui::Button("Show every control (Advanced mode)", ImVec2(presetWidth, 0))) {
                uiMode = UiMode::Advanced;
            }
        } // uiMode == Simple

        bool scenarioChanged = false;

        if (uiMode == UiMode::Advanced) {
        ImGui::TextWrapped(
            "Stage 1 (Newtonian, red/green/blue): 1/r^2 gravity integrated "
            "three ways -- Euler, Verlet, RK4 -- against the exact analytic "
            "Kepler orbit, showing each method's error and stability. "
            "Stage 2 (Schwarzschild, cyan/magenta/yellow): the SAME impact "
            "parameters traced as exact null geodesics through curved "
            "spacetime instead. Compare the two directly -- the Newtonian "
            "curve is not how light actually bends near a black hole.");

        if (ImGui::CollapsingHeader("Black Hole", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (SliderDouble("Mass (solar masses)", &blackHoleMassSolar, 1.0, 1.0e11, "%.3e")) {
                blackHole.SetMassSolarMasses(blackHoleMassSolar);
            }
            HelpTooltip("How much mass the black hole has, in units of our Sun's mass. Heavier = larger Schwarzschild radius, but the SHAPE of everything you see stays the same (it's all scale-invariant) -- only the physical size in kilometers changes.");
            ImGui::Text("Schwarzschild radius r_s = 2GM/c^2 = %.3e km", blackHole.SchwarzschildRadiusMeters() / 1000.0);
            ImGui::Text("(In simulation units, r_s is always exactly 2.0 -- see Physics/Constants.h)");

            ImGui::Separator();
            if (SliderDouble("Spin a* = Jc/(GM^2)", &blackHoleSpin, -0.998, 0.998, "%.3f")) {
                blackHole.SetSpin(blackHoleSpin);
            }
            HelpTooltip(
                "Dimensionless spin; 0 is the non-rotating Schwarzschild hole above every other stage was "
                "built against. Nonzero switches ray tracing to the Kerr geodesic solver (frame dragging, "
                "an ergosphere, a prograde/retrograde-asymmetric shadow) -- runs on GPU when available, same "
                "as Schwarzschild mode, via a separate CUDA kernel.");
            if (blackHole.IsRotating()) {
                const double a = blackHole.Spin();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Kerr mode active (a*=%.3f).", a);
                ImGui::Text("Outer horizon r+ = %.4f   Inner horizon r- = %.4f (sim units)",
                             Physics::Kerr::OuterHorizonSim(a), Physics::Kerr::InnerHorizonSim(a));
                ImGui::Text("Equatorial ergosphere r = %.4f (always 2M, independent of spin)",
                             Physics::Kerr::ErgosphereRadiusSim(a, 0.0));
                ImGui::Text("Photon orbit: prograde r = %.4f   retrograde r = %.4f",
                             Physics::Kerr::EquatorialPhotonOrbitRadiusSim(a, true),
                             Physics::Kerr::EquatorialPhotonOrbitRadiusSim(a, false));
                ImGui::Text("ISCO: prograde r = %.4f   retrograde r = %.4f",
                             Physics::Kerr::EquatorialIscoRadiusSim(a, true), Physics::Kerr::EquatorialIscoRadiusSim(a, false));
                ImGui::TextWrapped(
                    "The accretion disk below is assumed prograde (co-rotating with the spin) in Kerr "
                    "mode -- physically standard (Lense-Thirring torquing aligns a real disk with the "
                    "spin), so its own rotation-direction toggle is ignored here.");
            }
        }

        if (ImGui::CollapsingHeader("Binary Black Holes (approximate)")) {
            ImGui::Checkbox("Enable binary mode", &binaryModeEnabled);
            HelpTooltip(
                "Two orbiting masses instead of one. UNLIKE the Schwarzschild/Kerr modes above, there is "
                "no exact closed-form solution for two orbiting black holes in general relativity at all -- "
                "this uses a deliberately approximate scheme instead: each mass bends light via TWICE its "
                "Newtonian gravitational acceleration (the standard trick that reproduces GR's correct "
                "weak-field bending angle from a cheap, easy-to-superpose force law), summed across both "
                "masses. It reproduces the real weak-field deflection formula closely (see "
                "tests/test_binary.cpp) but does NOT reproduce exact strong-field effects (the real photon "
                "sphere, critical impact parameter, frame dragging) near either hole. Each body gets its "
                "own accretion disk, reusing the SAME 'Enable accretion disk' toggle and settings as "
                "Stage 6 below (body 2's radii scale with its own Schwarzschild radius). Takes priority "
                "over Kerr spin above when enabled (a rotating member of a binary isn't modeled) -- runs "
                "on GPU when available, same as Schwarzschild/Kerr, via a separate CUDA kernel.");
            if (binaryModeEnabled) {
                SliderDouble("Mass ratio (M2/M1)", &binarySystem.massRatio, 0.05, 1.0, "%.3f");
                HelpTooltip("Body 1 IS the black hole configured above (same mass slider, same r_s). This is body 2's mass relative to it -- 1.0 means equal masses.");
                SliderDouble("Separation (sim units)", &binarySystem.separation, 10.0, 150.0, "%.2f");
                ImGui::Checkbox("Animate orbit", &binaryOrbitAnimated);
                if (!binaryOrbitAnimated) {
                    SliderDouble("Orbital phase (rad)", &binarySystem.phase, -glm::pi<double>(), glm::pi<double>(), "%.3f");
                }

                ImGui::Text("r1 = %.3f   r2 = %.3f   (from the barycenter, which sits at the origin)",
                             binarySystem.Radius1(), binarySystem.Radius2());
                ImGui::Text("rs1 = %.3f   rs2 = %.3f   separation/orbital period = %.3f",
                             binarySystem.SchwarzschildRadius1(), binarySystem.SchwarzschildRadius2(),
                             binarySystem.OrbitalPeriod());
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                                    "Binary mode active -- rendering with the approximate superposed-lensing solver.");
            }
        }

        if (ImGui::CollapsingHeader("Extra Black Holes (place anywhere)")) {
            ImGui::TextWrapped(
                "Freely-placed black holes, not constrained to an orbit like Binary mode's second body -- "
                "fly the camera (Free-fly mode) to wherever you want one, set its mass, and drop it. Uses "
                "the SAME approximate doubled-Newtonian lensing scheme as Binary mode, generalized to "
                "however many bodies are active at once (primary + binary's second body if also enabled + "
                "these). Runs on CPU only while any are placed -- see the GPU status line in the render "
                "settings above.");
            SliderDouble("Next black hole's mass (solar masses)", &extraBlackHoleMassSolar, 0.1, blackHole.MassSolarMasses(), "%.3e");
            SliderDouble("Spawn distance from camera (sim units)", &spawnDistanceFromCamera, 2.0, 100.0, "%.1f");
            HelpTooltip("Shared with the star/planet 'spawn here' controls below -- how far in front of wherever the camera is currently looking a new object appears.");
            if (static_cast<int>(extraBlackHoles.size()) >= kMaxExtraBlackHoles) {
                ImGui::TextDisabled("Add black hole here (limit of %d reached)", kMaxExtraBlackHoles);
            } else if (ImGui::Button("Add black hole here")) {
                SpawnExtraBlackHole();
            }
            if (!extraBlackHoles.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear extra black holes")) {
                    extraBlackHoles.clear();
                }
            }
            for (size_t i = 0; i < extraBlackHoles.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("#%d: %.3e Msun at (%.1f, %.1f, %.1f)", static_cast<int>(i),
                            extraBlackHoles[i].gmRatio * blackHole.MassSolarMasses(), extraBlackHoles[i].position.x,
                            extraBlackHoles[i].position.y, extraBlackHoles[i].position.z);
                ImGui::SameLine();
                if (ImGui::Button("Remove")) {
                    extraBlackHoles.erase(extraBlackHoles.begin() + static_cast<long>(i));
                    ImGui::PopID();
                    break; // erased mid-loop -- indices below i are now stale, stop rather than continue over them
                }
                ImGui::PopID();
            }
        }

        if (ImGui::CollapsingHeader("Mutual Gravity (stars/planets pull on each other)")) {
            ImGui::TextWrapped(
                "Off by default -- existing scenarios behave exactly as before unless you turn this on. "
                "Each spawned star/planet is reduced to a single point mass at its own center of mass for "
                "the purposes of pulling on OTHER spawned stars/planets (not full particle-to-particle "
                "physics, and black holes are NOT included -- they stay fixed gravity sources, never pulled "
                "by anything). This is a DELIBERATELY EXAGGERATED, tuned force, not the real physical mutual "
                "gravity -- at any black hole mass this app allows, two real stars'/planets' actual mutual "
                "pull on each other is far too small to ever visibly matter (same reason planet self-gravity "
                "uses a tuned strength instead of the real formula). With it on, two stars or planets given "
                "the right initial conditions can settle into a real binary-star-like mutual orbit.");
            ImGui::Checkbox("Enable mutual gravity", &mutualGravityEnabled);
            if (mutualGravityEnabled) {
                SliderDouble("Mutual gravity strength", &mutualGravityStrength, 0.0, 5.0, "%.2f");
                HelpTooltip("1.0 is the tuned default. Higher = faster/stronger mutual attraction; 0 behaves like mutual gravity off.");
            }
        }

        if (ImGui::CollapsingHeader("Universe / Star Map (real catalog)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Tip: press M anytime (any mode, while flying too) to pop this out as its own window.");
            DrawUniversePanelContents();
        }

        if (ImGui::CollapsingHeader("Scenario", ImGuiTreeNodeFlags_DefaultOpen)) {
            scenarioChanged |= ImGui::SliderInt("Impact parameter count", &scenarioParams.impactParameterCount, 1, 20);
            HelpTooltip("The 'impact parameter' is how far off-center a ray of light starts -- like the distance between a thrown ball's path and a goalpost it's aimed near. A small impact parameter passes close to the black hole (more bending, possibly captured); a large one passes far away (barely bent).");
            scenarioChanged |= SliderDouble("Impact parameter min (b)", &scenarioParams.impactParameterMin, 0.5, 20.0);
            scenarioChanged |= SliderDouble("Impact parameter max (b)", &scenarioParams.impactParameterMax, 0.5, 30.0);
            scenarioChanged |= SliderDouble("Start distance", &scenarioParams.startDistance, 10.0, 100.0);
            scenarioChanged |= SliderDouble("Particle speed (c)", &scenarioParams.speed, 0.05, 1.0);
            ImGui::TextWrapped(
                "Note: speed = 1.0c under NEWTONIAN gravity is NOT how real "
                "light behaves -- this is the classical (pre-GR) baseline, "
                "included specifically so Stage 2's relativistic result can "
                "be shown to differ from it.");
        }

        if (ImGui::CollapsingHeader("Integration", ImGuiTreeNodeFlags_DefaultOpen)) {
            scenarioChanged |= SliderDouble("Timestep dt", &simConfig.timeStep, 0.001, 0.1, "%.4f");
            ImGui::SliderInt("Steps per frame", &stepsPerFrame, 1, 20);
            ImGui::Checkbox("Paused", &paused);
            if (ImGui::Button("Reset Scenario")) {
                scenarioChanged = true;
            }
        }

        ImGui::Checkbox("Show grid", &showGrid);
        ImGui::Checkbox("Show Flamm's paraboloid (spacetime curvature funnel)", &showWarpedGrid);
        HelpTooltip(
            "The classic embedding-diagram 'funnel' around each black hole: a surface of revolution "
            "whose shape follows the exact formula for the real Schwarzschild spatial embedding "
            "(y = -2*sqrt(r_s*(rho-r_s)), rho = distance from the hole), flaring out from a vertical-"
            "tangent throat right at the horizon to nearly flat far away. This is a genuine textbook "
            "result, but it's a 2D ANALOGY: a 2D surface embedded in 3D space standing in for 4D "
            "spacetime curvature, not a literal picture of it. In binary mode, each body gets its own "
            "funnel.");
        ImGui::Checkbox("Show analytic Kepler reference", &showAnalyticCurve);
        ImGui::Checkbox("Show black hole horizon", &showHorizon);
        ImGui::Checkbox("Show Newtonian trajectories (Stage 1)", &showNewtonianTrajectories);
        ImGui::Checkbox("Show Schwarzschild photon geodesics (Stage 2)", &showPhotonGeodesics);
        ImGui::Checkbox("Show photon sphere (r = 1.5 r_s)", &showPhotonSphere);
        HelpTooltip("The photon sphere is a razor's-edge orbit where light can (in theory) circle the black hole forever. It's larger than the black hole itself -- get closer than this and light can no longer escape.");

        if (scenarioChanged) {
            simulation.Reset(BuildScenario(scenarioParams), simConfig);
            photonTraces = BuildPhotonTraces(scenarioParams);
        }

        ImGui::Separator();
        ImGui::Text("Validation: numerical vs analytic Kepler orbit");
        {
            std::array<double, 3> maxErrorByMethod{0.0, 0.0, 0.0};
            std::array<int, 3> countByMethod{0, 0, 0};
            std::array<std::array<int, 4>, 3> fateCounts{}; // [method][fate]

            for (const auto& p : simulation.Particles()) {
                const int mi = static_cast<int>(p.method);
                maxErrorByMethod[mi] = std::max(maxErrorByMethod[mi], p.maxRadialError);
                countByMethod[mi]++;
                fateCounts[mi][static_cast<int>(p.fate)]++;
            }

            for (int mi = 0; mi < 3; ++mi) {
                const auto method = static_cast<IntegratorMethod>(mi);
                const glm::vec3 c = ColorForMethod(method);
                ImGui::TextColored(ImVec4(c.r, c.g, c.b, 1.0f), "%s", MethodName(method));
                ImGui::SameLine();
                ImGui::Text(" max |r_numeric - r_analytic| = %.5f  (n=%d, active=%d, captured=%d, escaped=%d, unresolved=%d)",
                            maxErrorByMethod[mi], countByMethod[mi],
                            fateCounts[mi][static_cast<int>(ParticleFate::ACTIVE)],
                            fateCounts[mi][static_cast<int>(ParticleFate::CAPTURED)],
                            fateCounts[mi][static_cast<int>(ParticleFate::ESCAPED)],
                            fateCounts[mi][static_cast<int>(ParticleFate::UNRESOLVED)]);
            }
        }

        ImGui::Separator();
        ImGui::Text("Stage 2: Schwarzschild photon geodesics (exact, not an approximation)");
        {
            int escaped = 0, captured = 0, unresolved = 0;
            for (const auto& p : photonTraces) {
                if (p.fate == PhotonFate::ESCAPED) ++escaped;
                else if (p.fate == PhotonFate::CAPTURED) ++captured;
                else if (p.fate == PhotonFate::UNRESOLVED) ++unresolved;
            }
            ImGui::Text("Critical impact parameter b_crit = 3*sqrt(3) GM/c^2 = %.4f (sim units)",
                        BlackHole::CriticalImpactParameterSim());
            ImGui::Text("n=%d  escaped=%d  captured=%d  unresolved(near photon sphere)=%d",
                        static_cast<int>(photonTraces.size()), escaped, captured, unresolved);
            ImGui::Checkbox("Show per-ray debug table", &showPhotonDebugTable);

            if (showPhotonDebugTable && ImGui::BeginTable("PhotonDebugTable", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("b");
                ImGui::TableSetupColumn("fate");
                ImGui::TableSetupColumn("closest approach");
                ImGui::TableSetupColumn("deflection (rad)");
                ImGui::TableSetupColumn("steps");
                ImGui::TableHeadersRow();
                for (const auto& p : photonTraces) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%.3f", p.impactParameter);
                    ImGui::TableSetColumnIndex(1);
                    const glm::vec3 c = ColorForPhotonFate(p.fate);
                    ImGui::TextColored(ImVec4(c.r, c.g, c.b, 1.0f), "%s", PhotonFateName(p.fate));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.4f", p.closestApproach);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.5f", p.totalDeflectionAngle);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", p.steps);
                }
                ImGui::EndTable();
            }
        }

        ImGui::Separator();
        ImGui::Text("Stage 3: Gravitational Lensing (backward ray tracing)");
        {
            ImGui::Checkbox("Show lensed render instead of debug view", &viewLensedRender);
            ImGui::SameLine();
            ImGui::Checkbox("Live (re-trace every frame)", &liveMode);
            if (viewLensedRender) {
                ImGui::SameLine();
                if (liveMode) {
                    const double fps = smoothedFrameMs > 0.0 ? 1000.0 / smoothedFrameMs : 0.0;
                    ImGui::TextDisabled("(%.0f fps)", fps);
                } else {
                    ImGui::TextDisabled("(paused -- use Render or a slider change to update)");
                }
            }

            SliderDouble("Resolution", &renderScalePercent, 10.0, 100.0, "%.0f%%");
            ImGui::SameLine();
            {
                const auto [previewW, previewH] = ComputeRenderResolution(fbWidth, fbHeight, renderScalePercent, maxRenderPixels);
                ImGui::TextDisabled("(%d x %d px, aspect-matched, capped at %.1fM rays/frame)", previewW, previewH,
                                     maxRenderPixels / 1.0e6);
            }
            HelpTooltip("Percentage of the window's actual pixel count to trace, capped at a fixed ray budget so frame rate stays predictable no matter how large your window or monitor is. Lower = faster, never less accurate -- geodesic precision (step budget) is always full regardless of this setting.");
            {
                double maxRenderPixelsM = maxRenderPixels / 1.0e6;
                if (SliderDouble("Max rays/frame (millions)", &maxRenderPixelsM, 0.1, 8.0, "%.1fM")) {
                    maxRenderPixels = std::max(1, static_cast<int>(maxRenderPixelsM * 1.0e6));
                }
                HelpTooltip("Hard cap on how many rays are traced per frame, regardless of the Resolution slider or window size. Tuned by default (1.5M) against a mid-high-end consumer GPU (RTX 4070 Ti Super) -- raise it on a faster machine for more detail, or lower it on a slower one to keep frame rate up.");
            }
            SliderDouble("Camera FOV (deg)", &camera.fovYDegrees, 10.0, 120.0, "%.1f");

            {
                // Stage 5: the shadow's apparent angular size is NOT the
                // horizon's angular size -- shown here quantitatively for
                // the camera's current distance (see test_shadow.cpp for
                // the derivation and numeric validation).
                const double r0 = camera.distance;
                const double rs = BlackHole::SchwarzschildRadiusSim();
                const double lapse = Physics::Schwarzschild::LapseFactor(r0, rs);
                const double shadowAngleDeg = glm::degrees(std::asin(std::clamp(
                    BlackHole::CriticalImpactParameterSim() * std::sqrt(lapse) / r0, -1.0, 1.0)));
                const double horizonAngleDeg = glm::degrees(std::asin(std::clamp(rs / r0, -1.0, 1.0)));
                ImGui::Text("At this distance: shadow angular radius = %.2f deg  (horizon's OWN angular size = %.2f deg -- not the same thing)",
                            shadowAngleDeg, horizonAngleDeg);
            }
            needsRerender |= SliderDouble("Star density", &starSettings.density, 0.0, 0.35, "%.4f");
            needsRerender |= SliderDouble("Star brightness", &starSettings.brightness, 0.0, 4.0, "%.2f");
            starField.SetSettings(starSettings);

            ImGui::TextWrapped(
                "Each pixel below is one photon traced BACKWARD from the "
                "camera through curved spacetime via the same GeodesicSolver "
                "as Stage 2's paths -- wherever a ray escapes, it samples "
                "the starfield in that escape direction; wherever it's "
                "captured or unresolved, the pixel is black (the shadow). "
                "In Live mode this re-traces every frame, so orbiting the "
                "camera shows the light bending in real time.");

            if (cudaCompiledIn) {
                if (cudaDeviceAvailable) {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "GPU (CUDA) active -- always used when available, no CPU fallback toggle.");
                } else {
                    ImGui::TextDisabled("Compiled with CUDA, but no CUDA device found -- running on CPU.");
                }
            } else {
                ImGui::TextDisabled("GPU (CUDA): not compiled in this build (configure with -DBH_ENABLE_CUDA=ON)");
            }
            if (ImGui::Button(liveMode ? "Render Now (force refresh)" : "Render Now")) {
                PerformRender();
                needsRerender = false;
            }

#ifdef BH_CUDA_AVAILABLE
            if (cudaDeviceAvailable && ImGui::Button("Validate: Compare CPU vs GPU")) {
                const auto [w, h] = ComputeRenderResolution(fbWidth, fbHeight, renderScalePercent, maxRenderPixels);

                RayTracing::RenderSettings renderSettings;
                renderSettings.width = w;
                renderSettings.height = h;
                renderSettings.geodesicConfig.escapeRadius = 1.0e4;
                renderSettings.geodesicConfig.maxSteps = 8000;
                renderSettings.geodesicConfig.basePhiStep = 0.015;
                renderSettings.geodesicConfig.checkDisk = diskEnabled;
                renderSettings.geodesicConfig.diskInnerRadius = diskSettings.innerRadius;
                renderSettings.geodesicConfig.diskOuterRadius = diskSettings.outerRadius;
                renderSettings.geodesicConfig.diskClockwise = diskSettings.clockwise;

                // Mirrors PerformRender: with spin != 0, both sides of this
                // comparison exercise the Kerr solver instead (CPU
                // TracePhotonKerr vs. the GPU TraceKerrPhotonsKernel) -- the
                // comparison is meaningful either way, since RenderImage/
                // RenderImageCUDA both branch on the SAME renderSettings.spin.
                renderSettings.spin = blackHole.Spin();
                renderSettings.kerrConfig.escapeRadius = renderSettings.geodesicConfig.escapeRadius;
                renderSettings.kerrConfig.maxSteps = kKerrLiveMaxSteps;
                renderSettings.kerrConfig.baseLambdaStep = 0.05;
                renderSettings.kerrConfig.checkDisk = diskEnabled;
                renderSettings.kerrConfig.diskInnerRadius = diskSettings.innerRadius;
                renderSettings.kerrConfig.diskOuterRadius = diskSettings.outerRadius;

                const RayTracing::CameraFrame frame = BuildCameraFrame(camera, w, h);
                const glm::dvec3 sourceDirection = frame.forward;

                starField.SetSettings(starSettings);
                const RayTracing::BackgroundSampler background =
                    useEinsteinRingSource
                        ? RayTracing::BackgroundSampler([&](const glm::dvec3& dir) {
                              return SamplePointSource(dir, sourceDirection, sourceAngularRadius, sourceBrightness);
                          })
                        : RayTracing::BackgroundSampler([&](const glm::dvec3& dir) { return starField.Sample(dir); });

                {
                    Astronomy::AccretionDiskSettings effectiveDiskSettings = diskSettings;
                    effectiveDiskSettings.eddingtonFraction += tidalFlare.EddingtonFractionBoost();
                    accretionDisk.SetSettings(effectiveDiskSettings);
                }
                accretionDisk.SetBlackHoleMassKg(blackHole.MassKg());
                const RayTracing::DiskSampler diskSampler = [&](const PhotonTraceResult& hit) {
                    const double emittedTemperature = accretionDisk.Temperature(hit.diskHitRadius);
                    const double observedTemperature = diskSettings.relativisticEffects
                                                             ? emittedTemperature * hit.diskRedshiftFactor
                                                             : emittedTemperature;
                    return accretionDisk.EmittedColor(observedTemperature);
                };

                const auto cpuT0 = std::chrono::steady_clock::now();
                const std::vector<glm::vec3> cpuPixels = RayTracing::RenderImage(frame, renderSettings, background, diskSampler);
                const auto cpuT1 = std::chrono::steady_clock::now();
                const double cpuMs = std::chrono::duration<double, std::milli>(cpuT1 - cpuT0).count();

                const auto gpuT0 = std::chrono::steady_clock::now();
                const std::vector<glm::vec3> gpuPixels = RayTracing::RenderImageCUDA(frame, renderSettings, background, diskSampler);
                const auto gpuT1 = std::chrono::steady_clock::now();
                const double gpuMs = std::chrono::duration<double, std::milli>(gpuT1 - gpuT0).count();

                float maxAbsDiff = 0.0f;
                double sumAbsDiff = 0.0;
                for (size_t i = 0; i < cpuPixels.size(); ++i) {
                    const glm::vec3 d = glm::abs(cpuPixels[i] - gpuPixels[i]);
                    maxAbsDiff = std::max({maxAbsDiff, d.r, d.g, d.b});
                    sumAbsDiff += d.r + d.g + d.b;
                }
                const double meanAbsDiff = sumAbsDiff / (cpuPixels.size() * 3.0);

                char buf[512];
                std::snprintf(buf, sizeof(buf),
                              "CPU: %.1f ms (%.2f Mrays/s)   GPU: %.1f ms (%.2f Mrays/s)   speedup: %.1fx\n"
                              "Max abs color diff: %.6f   Mean abs color diff: %.6f",
                              cpuMs, (w * h) / (cpuMs * 1000.0), gpuMs, (w * h) / (gpuMs * 1000.0), cpuMs / gpuMs,
                              maxAbsDiff, meanAbsDiff);
                cpuVsGpuReport = buf;

                lensedTexture = renderer.UploadImage(gpuPixels, w, h, lensedTexture);
                lensedTextureWidth = w;
                lensedTextureHeight = h;
            }
            if (!cpuVsGpuReport.empty()) {
                ImGui::TextWrapped("%s", cpuVsGpuReport.c_str());
            }
#endif

            if (lensedTextureWidth > 0) {
                ImGui::Text("Last render (%s): %dx%d in %.1f ms (%.2f Mrays/s)",
                            lastRenderWasGpu ? "GPU/CUDA" : "CPU multi-threaded",
                            lensedTextureWidth, lensedTextureHeight, lastRenderMilliseconds,
                            (lensedTextureWidth * lensedTextureHeight) / (lastRenderMilliseconds * 1000.0));
            }
        }

        ImGui::Separator();
        ImGui::Text("Stage 4: Einstein Ring (camera -> black hole -> point source)");
        {
            needsRerender |= ImGui::Checkbox("Use aligned point source instead of starfield", &useEinsteinRingSource);
            HelpTooltip("An Einstein ring forms when a light source sits exactly behind the black hole from the viewer. Light from the source reaches you from every direction around the shadow at once, so it appears as a ring instead of a point.");
            ImGui::TextWrapped(
                "The source direction is always camera.forward -- i.e. it "
                "sits on the camera's own viewing axis, on the far side of "
                "the black hole -- so camera/hole/source are automatically "
                "collinear for ANY orbit angle. No manual alignment needed. "
                "With this enabled, render at Stage 3 above: a ring at "
                "angular radius ~ b_crit/distance should appear around the "
                "shadow, emerging purely from the geodesic integration.");
            needsRerender |= SliderDouble("Source angular radius", &sourceAngularRadius, 0.001, 0.1, "%.4f");
            needsRerender |= SliderDouble("Source brightness", &sourceBrightness, 0.5, 10.0, "%.2f");

            ImGui::Checkbox("Show camera debug rays in 3D view", &showCameraDebugRays);
            if (ImGui::SliderInt("Debug ray count", &cameraDebugRayCount, 3, 41)) {
                showCameraDebugRays = true;
            }
            if (ImGui::Button("Trace Camera Debug Rays") || (showCameraDebugRays && cameraDebugRays.empty())) {
                const RayTracing::CameraFrame frame = BuildCameraFrame(camera, 320, 180);
                cameraDebugRays = BuildCameraDebugRays(frame, cameraDebugRayCount, blackHole.Spin());
            }

            if (!cameraDebugRays.empty() && ImGui::BeginTable("CameraRayDebugTable", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("b (impact param)");
                ImGui::TableSetupColumn("fate");
                ImGui::TableSetupColumn("closest approach");
                ImGui::TableSetupColumn("deflection (rad)");
                ImGui::TableSetupColumn("steps");
                ImGui::TableHeadersRow();
                for (const auto& p : cameraDebugRays) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%.4f", p.impactParameter);
                    ImGui::TableSetColumnIndex(1);
                    const glm::vec3 c = ColorForPhotonFate(p.fate);
                    ImGui::TextColored(ImVec4(c.r, c.g, c.b, 1.0f), "%s", PhotonFateName(p.fate));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.4f", p.closestApproach);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.5f", p.totalDeflectionAngle);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", p.steps);
                }
                ImGui::EndTable();
            }
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Photon Inspector", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable", &showPhotonInspector);
            HelpTooltip(
                "Pick exactly one ray by its position in the rendered image (NDC, "
                "-1 to 1 in each axis -- 0,0 is image center) and see everything the "
                "solver actually tracked for it: its conserved quantities, how many "
                "integration steps it took, and how it ended up. Updates live as you "
                "drag the sliders -- useful for pointing at a specific feature (the "
                "shadow edge, a lensed star, the Kerr shadow's flattened side) and "
                "seeing the real numbers behind it, not just the picture.");

            if (showPhotonInspector) {
                SliderDouble("Pixel X (NDC)", &inspectorNdcX, -1.0, 1.0, "%.3f");
                SliderDouble("Pixel Y (NDC)", &inspectorNdcY, -1.0, 1.0, "%.3f");
                ImGui::Checkbox("Show this ray's path in the 3D debug view", &inspectorShowPath);

                const auto [iw, ih] = ComputeRenderResolution(fbWidth, fbHeight, renderScalePercent, maxRenderPixels);
                const RayTracing::CameraFrame inspectorFrame = BuildCameraFrame(camera, iw, ih);
                inspectorResult = TraceInspectorRay(inspectorFrame, inspectorNdcX, inspectorNdcY, blackHole.Spin());

                const glm::vec3 fateColor = ColorForPhotonFate(inspectorResult.fate);
                ImGui::TextColored(ImVec4(fateColor.r, fateColor.g, fateColor.b, 1.0f), "Fate: %s",
                                    PhotonFateName(inspectorResult.fate));
                ImGui::Text("Steps taken: %d", inspectorResult.steps);
                ImGui::Text("Closest approach: %.5f sim units", inspectorResult.closestApproach);
                ImGui::Text("Impact parameter b = |L_z / E|: %.5f", inspectorResult.impactParameter);

                if (blackHole.IsRotating()) {
                    ImGui::Text("Conserved quantities (Kerr, Carter's constant formulation):");
                    ImGui::Text("  E (energy)        = %.6f", inspectorResult.kerrEnergyE);
                    ImGui::Text("  L_z (ang. mom.)   = %.6f", inspectorResult.kerrAngularMomentumLz);
                    ImGui::Text("  Q (Carter const.) = %.6f", inspectorResult.kerrCarterQ);
                    HelpTooltip(
                        "Q=0 means this ray stays in the equatorial plane for its whole "
                        "path (no polar motion) -- generic rays off the equator have Q!=0. "
                        "See docs/physics.md Sec. 10 for the governing equations these "
                        "satisfy.");
                } else {
                    ImGui::Text("(Schwarzschild mode: only b is tracked -- E and L_z separately "
                                "don't matter for a non-rotating hole's null geodesics.)");
                }

                if (inspectorResult.fate == PhotonFate::ESCAPED) {
                    const glm::dvec3 d = inspectorResult.escapeDirection;
                    ImGui::Text("Escaped toward direction (%.3f, %.3f, %.3f) -- sampling the sky there.",
                                d.x, d.y, d.z);
                }
                if (inspectorResult.hitDisk) {
                    ImGui::Text("Hit the accretion disk at r = %.4f (redshift factor g = %.4f)",
                                inspectorResult.diskHitRadius, inspectorResult.diskRedshiftFactor);
                }
                if (inspectorResult.fate == PhotonFate::UNRESOLVED) {
                    ImGui::TextDisabled(
                        "Unresolved: hit the step budget before escaping or being captured -- "
                        "typically a ray passing extremely close to the photon sphere/critical "
                        "impact parameter. Rendered as part of the shadow either way.");
                }
                if (inspectorShowPath && viewLensedRender) {
                    ImGui::TextDisabled("(Switch to the 3D debug view -- uncheck 'Show lensed render' above -- to see the path.)");
                }
            }
        }

        ImGui::Separator();
        ImGui::Text("Stage 6: Accretion Disk");
        {
            needsRerender |= ImGui::Checkbox("Enable accretion disk", &diskEnabled);
            ImGui::TextWrapped(
                "A thin equatorial disk (Shakura-Sunyaev radial temperature "
                "profile). Each disk pixel's color/brightness is redshifted "
                "by the EXACT gravitational + Doppler factor at that ray's "
                "disk crossing (computed from the same conserved impact "
                "parameter the geodesic solver already tracks) -- toggle "
                "'Relativistic effects' below to compare against a flat, "
                "unshifted disk.");

            const double isco = 3.0 * BlackHole::SchwarzschildRadiusSim();
            needsRerender |= SliderDouble("Inner radius", &diskSettings.innerRadius, isco * 0.5, diskSettings.outerRadius - 0.1, "%.2f");
            HelpTooltip("ISCO = Innermost Stable Circular Orbit: the closest distance matter can orbit the black hole without spiraling in. Real accretion disks start around here -- material that gets closer falls in within one orbit.");
            needsRerender |= SliderDouble("Outer radius", &diskSettings.outerRadius, diskSettings.innerRadius + 0.1, 100.0, "%.2f");
            ImGui::Text("(ISCO = 3*r_s = %.2f: physically, stable circular orbits require inner radius >= this)", isco);
            needsRerender |= SliderDouble("Temperature scale", &diskSettings.temperatureScale, 0.1, 3.0, "%.2f");
            needsRerender |= SliderDouble("Brightness scale", &diskSettings.brightnessScale, 0.01, 2.0, "%.3f");
            needsRerender |= ImGui::Checkbox("Clockwise rotation", &diskSettings.clockwise);
            needsRerender |= ImGui::Checkbox("Relativistic effects (redshift + beaming)", &diskSettings.relativisticEffects);

            needsRerender |= SliderDouble("Eddington fraction (L / L_Edd)", &diskSettings.eddingtonFraction, 0.01, 1.0, "%.2f");
            HelpTooltip("How hard the black hole is accreting, as a fraction of the Eddington limit (the luminosity at which radiation pressure balances gravity). Together with the mass, this sets the disk's REAL temperature in Kelvin (bigger mass + same Eddington fraction = a cooler disk, since the emitting area grows faster than the luminosity) -- it does not just recolor the image, it's the physical quantity the color comes from.");
            needsRerender |= SliderDouble("Radiative efficiency (eta)", &diskSettings.radiativeEfficiency, 0.01, 0.4, "%.2f");
            HelpTooltip("Fraction of accreted rest-mass energy radiated away (eta = L / Mdot*c^2). 0.1 is the standard thin-disk (Shakura-Sunyaev) value; a rapidly spinning hole can reach ~0.3-0.4.");
            ImGui::Text("Disk inner-edge temperature: %.0f K (mass-dependent -- see tooltips above)", accretionDisk.PeakKelvin());

            if (tidalFlare.IsActive()) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                    "Tidal disruption flare active: Eddington fraction +%.3f (decaying)",
                                    tidalFlare.EddingtonFractionBoost());
            } else {
                ImGui::TextDisabled("No recent planetary debris captured -- no flare boost.");
            }
            HelpTooltip("Captured planetary debris (from the Planets panel below) temporarily boosts the disk's effective Eddington fraction above, then decays back to the slider's own value over several seconds -- a simplified stand-in for a real tidal-disruption-event fallback light curve (spec 28.12/28.13), not an exact one. Single-hole/Kerr mode only.");
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Scientific Plots", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "Interactive counterparts to docs/validation.md's fixed tables -- computed live from "
                "the SAME solvers the render uses, not textbook curves drawn for illustration.");

            ImGui::SeparatorText("Deflection angle vs. impact parameter");
            SliderDouble("b min", &plotBMin, 3.0, 50.0, "%.2f");
            SliderDouble("b max", &plotBMax, 3.0, 50.0, "%.2f");
            ImGui::SliderInt("Sample count", &plotSampleCount, 10, 150);
            HelpTooltip(
                "Each sample is a full ray trace (from r=1e5, effectively 'from infinity'), not an "
                "approximation -- more samples means more traces, which costs more time, especially "
                "in Kerr mode. Button-triggered rather than live for that reason.");
            if (ImGui::Button("Compute Deflection Sweep")) {
                deflectionSweep =
                    BuildDeflectionSweep(std::min(plotBMin, plotBMax), std::max(plotBMin, plotBMax),
                                         plotSampleCount, blackHole.Spin());
            }
            if (!deflectionSweep.empty()) {
                std::vector<float> numerical, analytic;
                numerical.reserve(deflectionSweep.size());
                analytic.reserve(deflectionSweep.size());
                double maxAbsErrorAtEscaped = 0.0;
                int escapedCount = 0;
                for (const DeflectionSample& s : deflectionSweep) {
                    numerical.push_back(s.numericalDeflection);
                    analytic.push_back(s.analyticDeflection);
                    if (s.escaped) {
                        maxAbsErrorAtEscaped =
                            std::max(maxAbsErrorAtEscaped,
                                     static_cast<double>(std::abs(s.numericalDeflection - s.analyticDeflection)));
                        ++escapedCount;
                    }
                }
                ImGui::PlotLines("Numerical (rad)", numerical.data(), static_cast<int>(numerical.size()), 0,
                                  nullptr, 0.0f, FLT_MAX, ImVec2(0, 100));
                ImGui::PlotLines("Schwarzschild weak-field formula (rad)", analytic.data(),
                                  static_cast<int>(analytic.size()), 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 100));
                ImGui::Text("b range: [%.3f, %.3f] sim units, %d samples, %d escaped (rest captured/unresolved)",
                            plotBMin, plotBMax, static_cast<int>(deflectionSweep.size()), escapedCount);
                if (blackHole.IsRotating()) {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                        "Spin is ON: the gap between the two curves above is a REAL physical effect "
                        "(the Kerr spin correction/frame dragging), not numerical error -- the analytic "
                        "curve is always the SPIN-LESS Schwarzschild formula, kept fixed as a reference "
                        "so this gap is visible rather than hidden.");
                } else {
                    ImGui::Text("Max |numerical - analytic| among escaped samples: %.6f rad", maxAbsErrorAtEscaped);
                }
            }

            ImGui::SeparatorText("Gravitational time dilation (lapse factor) vs. radius");
            HelpTooltip(
                "How much slower a clock runs at radius r compared to a distant observer's: "
                "sqrt(f(r)) for Schwarzschild (an exact static-observer result), or the analogous "
                "ZAMO lapse (equatorial plane) for Kerr. 1.0 far away, 0 at the horizon.");
            {
                constexpr int kLapseSamples = 80;
                const double rStart =
                    (blackHole.IsRotating() ? Physics::Kerr::OuterHorizonSim(blackHole.Spin())
                                             : BlackHole::SchwarzschildRadiusSim()) *
                    1.01;
                const double rEnd = 30.0;
                std::vector<float> lapseValues(kLapseSamples);
                for (int i = 0; i < kLapseSamples; ++i) {
                    const double t = static_cast<double>(i) / (kLapseSamples - 1);
                    const double r = rStart + t * (rEnd - rStart);
                    const double lapse = blackHole.IsRotating()
                                              ? Physics::Kerr::ZamoLapse(r, 0.0, 1.0, blackHole.Spin())
                                              : std::sqrt(Physics::Schwarzschild::LapseFactor(
                                                    r, BlackHole::SchwarzschildRadiusSim()));
                    lapseValues[i] = static_cast<float>(lapse);
                }
                ImGui::PlotLines("Lapse factor (d(tau)/dt)", lapseValues.data(), kLapseSamples, 0, nullptr, 0.0f,
                                  1.0f, ImVec2(0, 100));
                ImGui::Text("r range: [%.3f, %.3f] sim units (1.01x horizon to 30)", rStart, rEnd);
            }

            if (diskEnabled) {
                ImGui::SeparatorText("Accretion disk temperature profile");
                constexpr int kTempSamples = 80;
                std::vector<float> tempValues(kTempSamples);
                double maxT = 0.0;
                for (int i = 0; i < kTempSamples; ++i) {
                    const double t = static_cast<double>(i) / (kTempSamples - 1);
                    const double r = diskSettings.innerRadius + t * (diskSettings.outerRadius - diskSettings.innerRadius);
                    const double T = accretionDisk.Temperature(r);
                    tempValues[i] = static_cast<float>(T);
                    maxT = std::max(maxT, T);
                }
                ImGui::PlotLines("Temperature (dimensionless shape)", tempValues.data(), kTempSamples, 0, nullptr,
                                  0.0f, static_cast<float>(maxT * 1.1), ImVec2(0, 100));
                ImGui::Text("r range: [%.3f, %.3f] sim units, inner-edge Kelvin scale ~ %.3e K",
                            diskSettings.innerRadius, diskSettings.outerRadius, accretionDisk.PeakKelvin());
            } else {
                ImGui::TextDisabled("(Enable the accretion disk above to see its temperature profile here.)");
            }
        }

        ImGui::Separator();
        ImGui::Text("Stars: tidal disruption");
        {
            ImGui::TextWrapped(
                "Each star is ~28 independent particles released together with no self-gravity "
                "binding them -- they free-fall under the exact same point-mass law "
                "(Physics::StaticObserverGravityAccelSim) as the piloted jet. Any stretching into "
                "a 'spaghetti' streak you see is the real, measured difference in gravity between "
                "a particle's near side and far side, not a scripted effect.");

            const double isco = 3.0 * BlackHole::SchwarzschildRadiusSim();
            SliderDouble("Spawn radius", &starSpawnRadius, isco, 100.0, "%.2f");
            HelpTooltip("How far from the black hole (in sim units, same scale as everything else) a newly-added star starts. Closer in = stronger, faster tidal disruption.");
            SliderDouble("Orbital speed (fraction of circular)", &starOrbitalSpeedFraction, 0.0, 1.3, "%.2f");
            HelpTooltip("0 = drops straight in (radial plunge). 1.0 = a circular orbit that (ignoring tidal spreading) would never fall in. Something around 0.4-0.7 gives an elliptical orbit that dives close enough to the hole to visibly spaghettify, which is usually the most interesting case.");
            SliderDouble("Mass (solar masses)", &starSpawnMassSolar, 0.01, 1000.0, "%.3f");
            HelpTooltip("Only used by Mutual Gravity (see its own panel below) -- a star's own particles are still massless test particles under the black hole's field, unaffected by this slider.");

            if (ImGui::Button("Add Star")) {
                SpawnStar();
            }
            ImGui::SameLine();
            if (!stars.empty() && ImGui::Button("Clear Stars")) {
                stars.clear();
            }

            ImGui::TextWrapped("Or place one freely: fly the camera (Free-fly mode) wherever you want, set a launch speed (0 = just drop it), and spawn.");
            SliderDouble("Launch speed (toward camera's forward direction)", &starLaunchSpeed, 0.0, 1.0, "%.2f");
            if (ImGui::Button("Add Star Here")) {
                SpawnStarHere();
            }

            int remainingParticles = 0;
            for (const Astronomy::StarBody& star : stars) {
                for (const Astronomy::StarParticle& particle : star.Particles()) {
                    if (!particle.consumed) ++remainingParticles;
                }
            }
            ImGui::Text("Active stars: %d (%d particles not yet consumed)", static_cast<int>(stars.size()), remainingParticles);
        }

        ImGui::Separator();
        ImGui::Text("Planets: tidal deformation and fracture");
        {
            ImGui::TextWrapped(
                "Each planet is a lattice of particles connected by breakable springs -- it "
                "resists deformation until internal spring strain exceeds its material's "
                "strength, then fractures into tracked fragments. The tidal-acceleration and "
                "Roche-limit numbers below are the weak-field Newtonian diagnostics from spec "
                "28.3/28.4: informative reference values, not switches that themselves trigger "
                "destruction -- what actually happens is whatever the spring network decides.");

            const bool planetGpuAvailable = Astronomy::PlanetBody::GpuAvailable();
            if (planetGpuAvailable) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "GPU (CUDA) active for planet physics");
            } else {
#ifdef BH_PLANET_CUDA_AVAILABLE
                ImGui::TextDisabled("No CUDA GPU found -- planet physics runs on CPU.");
#else
                ImGui::TextDisabled("Running on CPU (built without CUDA).");
#endif
            }
            HelpTooltip("At the particle counts below (tens to low hundreds), the GPU path is NOT expected to be faster than the CPU path -- it exists so particle count can go much higher while staying real-time, not to speed up small planets. See HANDOFF.md for the measurement this is based on.");

            const char* compositionNames[] = {"Rocky", "Iron-rich", "Icy", "Gas giant", "Differentiated (approx.)"};
            int compositionIndex = static_cast<int>(planetComposition);
            if (ImGui::Combo("Composition", &compositionIndex, compositionNames,
                              static_cast<int>(sizeof(compositionNames) / sizeof(compositionNames[0])))) {
                planetComposition = static_cast<Physics::PlanetComposition>(compositionIndex);
            }
            HelpTooltip("What the planet is made of -- sets its reference density, how stiff its internal springs are, and (most importantly for how it comes apart) how much strain a spring can take before it snaps. Icy and gas-giant bodies fracture far earlier than rocky or iron-rich ones.");

            SliderDouble("Mass (Earth masses)", &planetMassEarths, 0.01, 1000.0, "%.3f");
            HelpTooltip("Only used for the tidal-acceleration/Roche-limit diagnostics below and to split mass across fragments -- the planet's own gravity does not pull on itself (self-gravity isn't modeled this milestone; see HANDOFF.md).");
            {
                // Raised only when the GPU path is actually active -- a
                // CPU-only build/machine keeps the original 20-250 range,
                // which is already the sane real-time ceiling for a
                // per-frame CPU loop; pushing particle count into the
                // thousands is the one thing the GPU path actually buys
                // over the CPU path at this scale (see the status text
                // above).
                const int maxParticles = planetGpuAvailable ? 2000 : 250;
                if (planetParticleCount > maxParticles) planetParticleCount = maxParticles;
                int particleCountInt = planetParticleCount;
                if (ImGui::SliderInt("Particle count (resolution)", &particleCountInt, 20, maxParticles)) {
                    planetParticleCount = particleCountInt;
                }
            }
            HelpTooltip("How many particles/springs make up the next planet you spawn (existing planets keep whatever resolution they were created with). More particles = finer fragments, more cost.");
            SliderDouble("Material strength multiplier", &planetMaterialStrengthMultiplier, 0.1, 3.0, "%.2f");
            HelpTooltip("Scales the chosen composition's fracture-strain threshold -- below 1.0 makes the next planet you spawn more brittle than its preset, above 1.0 tougher.");

            ImGui::Checkbox("Self-gravity", &planetSelfGravityEnabled);
            HelpTooltip("Lets separated fragments pull back toward each other after a fracture, instead of drifting apart forever. This is a DELIBERATELY EXAGGERATED effect, not the real physical self-gravity -- at any black hole mass this app allows, a planet's actual self-gravity (roughly its mass divided by the black hole's mass) is far too small to ever visibly matter, so this uses a standalone tuned strength instead of the real formula.");
            if (planetSelfGravityEnabled) {
                SliderDouble("Self-gravity strength", &planetSelfGravityStrength, 0.0, 5.0, "%.2f");
                HelpTooltip("1.0 is the tuned default (fragments noticeably drift back together over several seconds once other forces die down). Higher = faster/stronger reaccumulation; 0 behaves like self-gravity off.");
            }

            SliderDouble("Spawn radius", &planetSpawnRadius, planetIsco, 100.0, "%.2f");
            HelpTooltip("How far from the black hole (sim units) a newly-added planet starts. Closer in = stronger, faster tidal stress.");
            SliderDouble("Orbital speed (fraction of circular)", &planetOrbitalSpeedFraction, 0.0, 1.3, "%.2f");
            HelpTooltip("0 = radial plunge. 1.0 = circular (would never fall in, ignoring tidal effects). 0.4-0.7 gives an elliptical dive that tends to actually fracture the planet.");

            if (ImGui::Button("Add Planet")) {
                SpawnPlanet();
            }
            ImGui::SameLine();
            if (!planets.empty() && ImGui::Button("Clear Planets")) {
                planets.clear();
            }

            ImGui::TextWrapped("Or place one freely, same idea as the star's 'Add Star Here' above.");
            SliderDouble("Launch speed (toward camera's forward direction)##planet", &planetLaunchSpeed, 0.0, 1.0, "%.2f");
            if (ImGui::Button("Add Planet Here")) {
                SpawnPlanetHere();
            }

            ImGui::TextWrapped("Initial-condition presets (spec 28.19, representative subset):");
            if (ImGui::Button("Stable orbit")) SpawnPlanetPresetStableOrbit();
            ImGui::SameLine();
            if (ImGui::Button("Close flyby")) SpawnPlanetPresetCloseFlyby();
            ImGui::SameLine();
            if (ImGui::Button("Direct infall")) SpawnPlanetPresetDirectInfall();
            if (ImGui::Button("Rocky disruption")) SpawnPlanetPresetRockyDisruption();
            ImGui::SameLine();
            if (ImGui::Button("Icy disruption")) SpawnPlanetPresetIcyDisruption();
            HelpTooltip("Stable orbit: far out, near-circular -- should stay intact. Close flyby: closer, still fast -- mild stretching. Direct infall: radial plunge from moderate distance. Rocky/Icy disruption: the same close, slow encounter with two different materials, to compare how differently they come apart.");

            int fragmentCount = 0, capturedParticles = 0, escapedFragments = 0, liveParticles = 0;
            for (const Astronomy::PlanetBody& planet : planets) {
                fragmentCount += static_cast<int>(planet.Fragments().size());
                for (const Astronomy::PlanetFragment& fragment : planet.Fragments()) {
                    if (fragment.escaped) ++escapedFragments;
                }
                for (const Astronomy::PlanetParticle& particle : planet.Particles()) {
                    if (particle.consumed) ++capturedParticles;
                    else ++liveParticles;
                }
            }
            ImGui::Text("Active planets: %d  |  fragments: %d  |  escaped fragments: %d",
                        static_cast<int>(planets.size()), fragmentCount, escapedFragments);
            ImGui::Text("Particles: %d live, %d captured", liveParticles, capturedParticles);

            if (!planets.empty()) {
                const Astronomy::PlanetBody& latest = planets.back();
                ImGui::Text("Most recent planet -- tidal accel (diagnostic): %.4e sim units  |  Roche limit (diagnostic): %.3f sim units",
                            latest.LastTidalAccelerationSim(), latest.LastRocheLimitSim());
            }
        }
        } // uiMode == Advanced

        ImGui::End();

        // Standalone Universe/Star Map window, toggled by "M" (see the key
        // handling near the top of this loop) -- available regardless of
        // Simple/Advanced mode or camera mode, specifically so it's usable
        // mid-flight without first switching to Advanced mode and scrolling
        // to find it.
        if (showUniverseMapWindow) {
            ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Universe / Star Map (M to toggle)", &showUniverseMapWindow)) {
                DrawUniversePanelContents();
            }
            ImGui::End();
        }

        // Single shared trigger for "a render is due": works regardless of
        // uiMode, so Simple-mode presets (which just flip flags) and the
        // Advanced panel's own controls both actually produce an image. In
        // Live mode this fires every single frame, at full precision,
        // whether the camera is moving or not -- see PerformRender. Skipped
        // entirely in Star System View: that view never displays
        // lensedTexture (it has its own render branch below), so tracing a
        // full image every frame here would be pure wasted GPU/CPU work --
        // a real perf bug, not a hypothetical one, since Live mode fires
        // this unconditionally every frame.
        if (!inStarSystemView && !inMultiStarView && viewLensedRender && (liveMode || needsRerender)) {
            PerformRender();
            needsRerender = false;
            smoothedFrameMs = smoothedFrameMs * 0.9 + lastRenderMilliseconds * 0.1;
        }

        // Multi-star N-body integration (Universe/MultiStarSystem.h,
        // TravelToMultiStarSystem): real per-frame Newtonian gravity, same
        // tuned mutual-gravity law (Physics::MutualGravityAccelSim) and
        // semi-implicit-Euler integration style the existing "Mutual
        // Gravity" star/planet feature already uses, just applied to the
        // system's own members pulling on EACH OTHER instead of on
        // spawned stars/planets. Two explicit passes (compute every
        // acceleration from the pre-step positions, THEN apply every
        // velocity/position update) so the result doesn't depend on
        // member iteration order within a single step.
        if (inMultiStarView && !paused) {
            const size_t n = multiStarPositions.size();
            std::vector<Physics::GravitySource> sources;
            sources.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                sources.push_back({multiStarPositions[i],
                                    activeMultiStarSystem.members[i].massSolar * Physics::Constants::SOLAR_MASS_KG});
            }
            std::vector<glm::dvec3> accelerations(n, glm::dvec3(0.0));
            for (size_t i = 0; i < n; ++i) {
                std::vector<Physics::GravitySource> others;
                others.reserve(n - 1);
                for (size_t j = 0; j < n; ++j) {
                    if (j != i) others.push_back(sources[j]);
                }
                accelerations[i] = Physics::MutualGravityAccelSim(multiStarPositions[i], others, 1.0);
            }
            for (size_t i = 0; i < n; ++i) {
                multiStarVelocities[i] += accelerations[i] * dt;
            }
            for (size_t i = 0; i < n; ++i) {
                multiStarPositions[i] += multiStarVelocities[i] * dt;
            }
        }

        if (!paused) {
            for (int s = 0; s < stepsPerFrame; ++s) {
                simulation.Step();
            }

            // Binary mode moves both masses off the origin (see
            // Physics::BinarySystem) -- stars/planets need to feel BOTH
            // bodies' gravity and check capture against both, or they
            // behave as if a single invisible hole still sits at the
            // (now-empty) barycenter instead of responding to the two
            // actual bodies on screen.
            const Physics::BinarySystem* activeBinary = binaryModeEnabled ? &binarySystem : nullptr;
            // Extra black holes (see Physics/MultiBlackHoleSystem.h) pull
            // on stars/planets too, same reasoning as activeBinary above --
            // nullptr when empty so Update()/UpdateGPU() skip the extra
            // work entirely (same convention activeBinary already uses).
            const std::vector<Physics::ExtraBlackHole>* activeExtraBlackHoles =
                extraBlackHoles.empty() ? nullptr : &extraBlackHoles;

            // Mutual gravity (object-level, see Physics/GravitySource.h):
            // built once per frame from each star/planet's CURRENT
            // (pre-update) center of mass/mass -- same "frozen for one
            // step" convention every other external field here already
            // uses. Left empty (a no-op) when mutualGravityEnabled is off,
            // so this costs nothing when the feature isn't in use.
            std::vector<Physics::GravitySource> starGravitySources, planetGravitySources;
            if (mutualGravityEnabled) {
                starGravitySources.reserve(stars.size());
                for (const Astronomy::StarBody& s : stars) {
                    starGravitySources.push_back({s.CenterOfMass(), s.CurrentMassKg()});
                }
                planetGravitySources.reserve(planets.size());
                for (const Astronomy::PlanetBody& p : planets) {
                    planetGravitySources.push_back({p.CenterOfMassSim(), p.CurrentMassKg()});
                }
            }

            for (size_t i = 0; i < stars.size(); ++i) {
                std::vector<Physics::GravitySource> others;
                const std::vector<Physics::GravitySource>* othersPtr = nullptr;
                if (mutualGravityEnabled) {
                    others.reserve(starGravitySources.size() + planetGravitySources.size());
                    for (size_t k = 0; k < starGravitySources.size(); ++k) {
                        if (k != i) others.push_back(starGravitySources[k]);
                    }
                    others.insert(others.end(), planetGravitySources.begin(), planetGravitySources.end());
                    othersPtr = &others;
                }
                stars[i].Update(dt, activeBinary, activeExtraBlackHoles, othersPtr, mutualGravityStrength);
            }
            stars.erase(std::remove_if(stars.begin(), stars.end(),
                                        [](const Astronomy::StarBody& star) { return star.IsFullyConsumed(); }),
                        stars.end());

            for (size_t j = 0; j < planets.size(); ++j) {
                std::vector<Physics::GravitySource> others;
                const std::vector<Physics::GravitySource>* othersPtr = nullptr;
                if (mutualGravityEnabled) {
                    others.reserve(starGravitySources.size() + planetGravitySources.size());
                    others.insert(others.end(), starGravitySources.begin(), starGravitySources.end());
                    for (size_t k = 0; k < planetGravitySources.size(); ++k) {
                        if (k != j) others.push_back(planetGravitySources[k]);
                    }
                    othersPtr = &others;
                }
                Astronomy::PlanetBody& planet = planets[j];
                if (!planet.UpdateGPU(dt, blackHole, activeBinary, activeExtraBlackHoles, othersPtr, mutualGravityStrength)) {
                    planet.Update(dt, blackHole, activeBinary, activeExtraBlackHoles, othersPtr, mutualGravityStrength);
                }
                tidalFlare.RegisterCapturedMass(planet.MassCapturedLastStep());
            }
            planets.erase(std::remove_if(planets.begin(), planets.end(),
                                          [](const Astronomy::PlanetBody& planet) { return planet.IsFullyConsumedOrDispersed(); }),
                          planets.end());
            tidalFlare.Update(dt);

            // No-op for the common case (velocity defaults to 0, i.e.
            // "placed here and stays put") -- only matters if an extra
            // black hole was ever given a nonzero drift velocity.
            for (Physics::ExtraBlackHole& extra : extraBlackHoles) {
                extra.Advance(dt);
            }
        }

        // Deliberately NOT gated on `paused` -- that flag is the Stage 1
        // Newtonian-simulation pause toggle; binary mode has its own,
        // independent "Animate orbit" checkbox, and gating it behind an
        // unrelated Stage 1 control would silently freeze the binary's
        // motion for anyone who happened to have Stage 1 paused without
        // realizing it affected a completely different feature.
        if (binaryModeEnabled && binaryOrbitAnimated) {
            binarySystem.AdvancePhase(dt);
        }

        const int width = fbWidth, height = fbHeight; // set earlier this frame, before PerformRender used them
        const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

        // Draws every star's non-consumed particles (a short fading trail
        // plus the particle itself) -- shared between the ray-traced view's
        // overlay pass and the debug 3D view below, since a star's position
        // means the same sim-unit coordinates in either one.
        const auto DrawStars = [&]() {
            for (const Astronomy::StarBody& star : stars) {
                for (const Astronomy::StarParticle& particle : star.Particles()) {
                    if (particle.consumed) continue;
                    if (particle.trail.size() >= 2) {
                        renderer.DrawPath(particle.trail, star.Color(), 1.5f);
                    }
                    renderer.DrawPoint(particle.position, star.Color(), 4.0f);
                }
            }
        };

        // Draws every planet as solid shapes rather than a sparse point
        // cloud, so it actually reads as "a planet" instead of a handful of
        // dots: while still a single intact fragment, it's one solid
        // spheroid stretched along the radial direction from the black
        // hole (DrawEllipsoid) -- a direct visualization of the same tidal
        // stretching StarBody's dots only implied. Once it has fractured
        // (Fragments().size() > 1), each fragment with enough live
        // particles to read as a chunk (>=4) is drawn as its own solid
        // sphere sized to its current spread, flying apart independently;
        // smaller debris falls back to points+trails, same as before,
        // since a 1-3 particle "chunk" would just look like a tiny warty
        // sphere rather than debris. Colored per-fragment (a deterministic
        // shade of the material's base color) so broken-off pieces read as
        // visually distinct. Shared between the overlay pass and the debug
        // 3D view, same rationale as DrawStars; drawSprings additionally
        // overlays the live spring lattice (only meaningful/used in the
        // debug view, where the solid shapes don't fight with the
        // ray-traced background for the depth buffer).
        const auto DrawPlanets = [&](bool drawSprings) {
            for (const Astronomy::PlanetBody& planet : planets) {
                const glm::vec3 base = planet.BaseColor();
                const auto& particles = planet.Particles();
                const auto& fragments = planet.Fragments();
                const bool stillIntact = fragments.size() <= 1;

                if (drawSprings) {
                    for (const Astronomy::PlanetSpring& spring : planet.Springs()) {
                        if (spring.broken) continue;
                        const Astronomy::PlanetParticle& pa = particles[static_cast<size_t>(spring.a)];
                        const Astronomy::PlanetParticle& pb = particles[static_cast<size_t>(spring.b)];
                        if (pa.consumed || pb.consumed) continue;
                        std::vector<glm::dvec3> edge{pa.position, pb.position};
                        renderer.DrawPath(edge, base * 0.6f, 1.0f);
                    }
                }

                for (const Astronomy::PlanetFragment& fragment : fragments) {
                    if (fragment.particleIndices.empty()) continue;

                    glm::dvec3 com(0.0);
                    double mass = 0.0;
                    int liveCount = 0;
                    for (int index : fragment.particleIndices) {
                        const Astronomy::PlanetParticle& p = particles[static_cast<size_t>(index)];
                        if (p.consumed) continue;
                        com += p.position * p.mass;
                        mass += p.mass;
                        ++liveCount;
                    }
                    if (liveCount == 0 || mass <= 0.0) continue;
                    com /= mass;

                    const int fragmentId = std::max(0, fragment.id);
                    const float shade = 0.55f + 0.45f * static_cast<float>((fragmentId * 37) % 11) / 10.0f;
                    const glm::vec3 color = base * shade;

                    if (liveCount < 4) {
                        // Too few particles to read as a solid chunk --
                        // draw as debris points+trails instead.
                        for (int index : fragment.particleIndices) {
                            const Astronomy::PlanetParticle& p = particles[static_cast<size_t>(index)];
                            if (p.consumed) continue;
                            if (p.trail.size() >= 2) renderer.DrawPath(p.trail, color, 1.5f);
                            renderer.DrawPoint(p.position, color, 4.0f);
                        }
                        continue;
                    }

                    if (stillIntact) {
                        // Still one whole body: a spheroid stretched along
                        // the radial direction from the black hole, sized
                        // from the actual particle spread (so it visibly
                        // elongates as tidal stress increases, rather than
                        // just uniformly inflating).
                        const glm::dvec3 radialDir = glm::length(com) > 1e-6 ? glm::normalize(com) : glm::dvec3(0.0, 1.0, 0.0);
                        double radialExtent = 0.0, lateralExtent = 0.0;
                        for (int index : fragment.particleIndices) {
                            const Astronomy::PlanetParticle& p = particles[static_cast<size_t>(index)];
                            if (p.consumed) continue;
                            const glm::dvec3 offset = p.position - com;
                            const double alongRadial = glm::dot(offset, radialDir);
                            radialExtent = std::max(radialExtent, std::abs(alongRadial));
                            const glm::dvec3 lateralOffset = offset - radialDir * alongRadial;
                            lateralExtent = std::max(lateralExtent, glm::length(lateralOffset));
                        }
                        const double radialSemiAxis = std::max(0.2, radialExtent * 1.15);
                        const double lateralSemiAxis = std::max(0.2, lateralExtent * 1.15);
                        // radialDir already IS the direction from the
                        // black hole (the scene's one light source, at the
                        // origin) to this body -- reused directly as the
                        // light direction, same convention Jet::JetRenderer
                        // already uses.
                        renderer.DrawShadedEllipsoid(com, radialDir, radialSemiAxis, lateralSemiAxis, color, radialDir);
                    } else {
                        // Already a separate broken-off piece: an
                        // (isotropic) solid sphere sized to its own spread.
                        double maxDist = 0.0;
                        for (int index : fragment.particleIndices) {
                            const Astronomy::PlanetParticle& p = particles[static_cast<size_t>(index)];
                            if (p.consumed) continue;
                            maxDist = std::max(maxDist, glm::length(p.position - com));
                        }
                        const double radius = std::max(0.15, maxDist * 1.15);
                        const glm::dvec3 lightDir = glm::length(com) > 1e-6 ? glm::normalize(com) : glm::dvec3(0.0, 1.0, 0.0);
                        renderer.DrawShadedSphere(com, radius, color, lightDir, 8, 12);
                    }
                }
            }
        };

        // Shared by Star System View and Multi-Star System View below:
        // neither ray-traces a background (both clear to flat black
        // otherwise, unlike the main view which always samples
        // Astronomy::StarField per-pixel) -- a fixed, once-generated set
        // of DIRECTIONS (not world positions, see below) and colors, same
        // spirit as StarField itself ("not astronomical data -- a stand-in
        // background"). Real bug fixed in an earlier session, not a
        // hypothetical: these points were originally placed at a FIXED
        // world position (direction * radius, centered on the origin)
        // rather than re-centered on the camera every frame -- a proper
        // skybox has to recenter on the viewer, or flying far enough away
        // (easy at the jet's cruise speeds) leaves the whole "sky" behind.
        // Recomputing `camera.Position() + direction * radius` every frame
        // keeps every point at a constant distance from the CURRENT
        // viewer instead, the standard way any engine draws a skybox.
        auto DrawBackgroundStarfield = [&]() {
            static const std::vector<std::pair<glm::dvec3, glm::vec3>> kBackgroundStars = [] {
                std::vector<std::pair<glm::dvec3, glm::vec3>> points;
                std::mt19937 rng(1337u);
                std::uniform_real_distribution<double> unit(-1.0, 1.0);
                std::uniform_real_distribution<double> kelvinDist(3000.0, 15000.0);
                std::uniform_real_distribution<double> brightDist(0.35, 1.0);
                points.reserve(1600);
                for (int i = 0; i < 1600; ++i) {
                    glm::dvec3 dir(unit(rng), unit(rng), unit(rng));
                    while (glm::length(dir) < 1e-6) dir = glm::dvec3(unit(rng), unit(rng), unit(rng));
                    dir = glm::normalize(dir);
                    const glm::vec3 hue = Astronomy::ApproximateBlackbodyHue(kelvinDist(rng));
                    points.emplace_back(dir, hue * static_cast<float>(brightDist(rng)));
                }
                return points;
            }();
            constexpr double kBackgroundStarRadius = 450.0;
            const glm::dvec3 camPos = camera.Position();
            for (const auto& [direction, color] : kBackgroundStars) {
                renderer.DrawPoint(camPos + direction * kBackgroundStarRadius, color, 2.0f);
            }
        };

        // Multi-Star System View: reached by traveling to a multi-star
        // system (Universe/MultiStarSystem.h) -- see TravelToMultiStarSystem
        // and the per-frame N-body integration block above for the actual
        // dynamics. Checked before Star System View below since the two
        // are mutually exclusive and this one takes priority if somehow
        // both were ever true.
        if (inMultiStarView) {
            glViewport(0, 0, width, height);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderer.BeginFrame(camera, aspect, width, height);
            DrawBackgroundStarfield();

            for (size_t i = 0; i < multiStarPositions.size(); ++i) {
                const Universe::StarMember& member = activeMultiStarSystem.members[i];
                // Radius grows with the cube root of mass (same spirit as
                // a real star's mass-radius relation being sub-linear, not
                // a precise mass-radius formula) so heavier members read
                // as visibly bigger without any one star dwarfing the
                // others at these mass ratios.
                const double radius = 1.0 + 1.3 * std::cbrt(member.massSolar);
                renderer.DrawTexturedSphere(multiStarPositions[i], radius, multiStarTextureIds[i], 24, 32);
            }

            if (camera.mode == Rendering::Camera::Mode::Jet && jetRenderer.IsLoaded()) {
                const glm::vec3 lightDirection = glm::length(jetPhysics.position) > 1e-4f
                                                      ? glm::normalize(jetPhysics.position)
                                                      : glm::vec3(0.0f, 0.0f, -1.0f);
                jetRenderer.Draw(jetPhysics.position, jetPhysics.rotation, camera.ViewMatrix(),
                                  camera.ProjectionMatrix(aspect), lightDirection);
            }
        } else
        // Star System View: reached by traveling to a Star catalog entry
        // (Universe/StarCatalog.h). This app's ray tracer only models
        // black holes (it traces null geodesics through a Schwarzschild/
        // Kerr metric) -- there is no equivalent GR renderer for an
        // ordinary star here, so rather than silently mislabeling a
        // black-hole render as "visiting a star", this is a deliberately
        // simple, explicitly-labeled stand-in: the star drawn as a plain
        // (unlit -- it IS the light source, not a lit object) sphere
        // colored from its real effective temperature via
        // Astronomy::ApproximateBlackbodyHue, sized on an arbitrary but
        // fixed visual scale (NOT the star's real radius relative to its
        // distance -- that ratio is astronomically tiny and would render
        // as an invisible point). The free-fly camera here is positioned
        // in the same arbitrary visual-scale units, independent of
        // whichever black hole's sim-unit scale (GM/c^2) happens to be
        // configured.
        if (inStarSystemView && visitedStarIndex >= 0 &&
            visitedStarIndex < static_cast<int>(universeCatalog.size())) {
            glViewport(0, 0, width, height);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderer.BeginFrame(camera, aspect, width, height);
            DrawBackgroundStarfield();

            const Universe::CatalogObject& star = universeCatalog[static_cast<size_t>(visitedStarIndex)];
            const double kelvin = star.temperatureK > 0.0 ? star.temperatureK : 5778.0;

            // Textured, not flat-colored: a procedural "granulation" pattern
            // (Astronomy::GenerateStarTexture, see that file's header for
            // why it's procedural rather than a real photograph -- genuine
            // resolved surface imagery exists for essentially no star but
            // the Sun) tinted by the star's real effective temperature.
            // Generated once per visited star, not every frame -- cached by
            // catalog index in these function-local statics, same
            // "generate lazily, reuse across frames" pattern LoadTexture's
            // callers already use for the jet model's texture.
            static int cachedStarTextureIndex = -1;
            static unsigned int cachedStarTextureId = 0;
            if (visitedStarIndex != cachedStarTextureIndex) {
                std::vector<glm::vec3> texturePixels =
                    Astronomy::GenerateStarTexture(kelvin, static_cast<unsigned int>(visitedStarIndex + 1));
                // Boosted past 1.0 (and left unlit -- DrawTexturedSphere
                // applies no separate lighting, same as plain DrawSphere)
                // so it reads as glowing/self-luminous rather than a lit
                // solid object -- baked into the texture itself here since
                // DrawTexturedSphere has no separate tint parameter.
                for (glm::vec3& pixel : texturePixels) {
                    pixel = glm::clamp(pixel * 1.4f, 0.0f, 1.0f);
                }
                cachedStarTextureId = renderer.UploadImage(texturePixels, 256, 128, 0);
                cachedStarTextureIndex = visitedStarIndex;
            }
            renderer.DrawTexturedSphere(glm::dvec3(0.0), 6.0, cachedStarTextureId, 32, 48);

            // Real exoplanets for this star (Universe/ExoplanetCatalog.h),
            // most stars have none listed (see that file's header comment --
            // no planet is invented for a star with no real detection).
            // Each planet's angular speed is real Kepler's-third-law motion
            // (Universe::OrbitalPeriodDays) relative to the OTHER planets in
            // the same system -- a closer-in planet genuinely orbits faster
            // than a farther one here, same as in reality. What is NOT real:
            // the absolute timescale (compressed so an orbit completes in
            // seconds, not days/years -- otherwise nothing would visibly
            // move in a sitting) and the orbital radii (spaced out by
            // relative semi-major axis only, not to the star's true
            // physical scale, for the same "would be invisible otherwise"
            // reason the star's own render size isn't to scale).
            double maxPeriodDays = 0.0;
            for (const Universe::ExoplanetEntry& planet : exoplanetCatalog) {
                if (std::string(planet.hostStarName) != star.name) continue;
                maxPeriodDays = std::max(maxPeriodDays, Universe::OrbitalPeriodDays(planet.semiMajorAxisAu, star.massSolar));
            }
            double maxSemiMajorAxisAu = 0.0;
            for (const Universe::ExoplanetEntry& planet : exoplanetCatalog) {
                if (std::string(planet.hostStarName) != star.name) continue;
                maxSemiMajorAxisAu = std::max(maxSemiMajorAxisAu, planet.semiMajorAxisAu);
            }
            constexpr double kOutermostOrbitVisualRadius = 20.0; // visual units from the star's center
            constexpr double kInnermostOrbitVisualRadius = 8.0;  // just outside the star sphere (radius 6)
            constexpr double kLongestOrbitVisualPeriodSeconds = 24.0; // the outermost listed planet takes this long to go around, in real wall-clock seconds
            if (maxPeriodDays > 0.0 && maxSemiMajorAxisAu > 0.0) {
                for (const Universe::ExoplanetEntry& planet : exoplanetCatalog) {
                    if (std::string(planet.hostStarName) != star.name) continue;
                    const double periodDays = Universe::OrbitalPeriodDays(planet.semiMajorAxisAu, star.massSolar);
                    const double visualPeriodSeconds = kLongestOrbitVisualPeriodSeconds * (periodDays / maxPeriodDays);
                    const double angleRadians = (visualPeriodSeconds > 1e-6)
                        ? (now / visualPeriodSeconds) * 2.0 * glm::pi<double>()
                        : 0.0;
                    const double orbitRadius = kInnermostOrbitVisualRadius +
                        (planet.semiMajorAxisAu / maxSemiMajorAxisAu) * (kOutermostOrbitVisualRadius - kInnermostOrbitVisualRadius);
                    const glm::dvec3 planetPos(orbitRadius * std::cos(angleRadians), 0.0, orbitRadius * std::sin(angleRadians));

                    const glm::vec3 planetColor = (planet.kind == Universe::PlanetKind::GasGiant)
                        ? glm::vec3(0.85f, 0.65f, 0.35f)
                        : glm::vec3(0.55f, 0.48f, 0.42f);
                    const double planetRadius = std::clamp(0.3 + 0.15 * std::log10(planet.massEarth + 1.0), 0.3, 1.2);
                    renderer.DrawShadedSphere(planetPos, planetRadius, planetColor, glm::normalize(-planetPos), 10, 16);

                    constexpr int kOrbitSamples = 64;
                    std::vector<glm::dvec3> orbitPath;
                    orbitPath.reserve(kOrbitSamples + 1);
                    for (int i = 0; i <= kOrbitSamples; ++i) {
                        const double t = 2.0 * glm::pi<double>() * i / kOrbitSamples;
                        orbitPath.push_back(glm::dvec3(orbitRadius * std::cos(t), 0.0, orbitRadius * std::sin(t)));
                    }
                    renderer.DrawPath(orbitPath, glm::vec3(0.3f, 0.3f, 0.35f), 1.0f);
                }
            }

            // Real debris disks: a handful of StarCatalog.h entries have an
            // actual, observationally-confirmed circumstellar dust/gas disk
            // (Vega and Fomalhaut's are among the best-studied of any
            // star's; Epsilon Eridani's is mentioned directly in its own
            // catalog note). Drawn as a faint, semi-transparent dust-
            // colored ring at a fixed visual radius beyond the exoplanet
            // orbits -- a real feature these specific stars have, not a
            // generic decoration added to every star.
            static const std::vector<const char*> kStarsWithKnownDebrisDisks = {"Vega", "Fomalhaut", "Epsilon Eridani"};
            const bool hasDebrisDisk = std::find_if(kStarsWithKnownDebrisDisks.begin(), kStarsWithKnownDebrisDisks.end(),
                                                     [&](const char* name) { return std::string(name) == star.name; }) !=
                                        kStarsWithKnownDebrisDisks.end();
            if (hasDebrisDisk) {
                constexpr double kDebrisDiskRadius = 24.0;
                constexpr int kDebrisDiskPointCount = 500;
                static const std::vector<glm::dvec3> kDebrisDiskOffsets = [] {
                    std::vector<glm::dvec3> offsets;
                    std::mt19937 rng(2024u);
                    std::uniform_real_distribution<double> angleDist(0.0, 2.0 * glm::pi<double>());
                    std::uniform_real_distribution<double> radiusJitter(-1.5, 1.5);
                    std::uniform_real_distribution<double> heightJitter(-0.4, 0.4);
                    offsets.reserve(kDebrisDiskPointCount);
                    for (int i = 0; i < kDebrisDiskPointCount; ++i) {
                        const double angle = angleDist(rng);
                        const double radius = kDebrisDiskRadius + radiusJitter(rng);
                        offsets.emplace_back(radius * std::cos(angle), heightJitter(rng), radius * std::sin(angle));
                    }
                    return offsets;
                }();
                for (const glm::dvec3& offset : kDebrisDiskOffsets) {
                    renderer.DrawPoint(offset, glm::vec3(0.55f, 0.45f, 0.38f), 2.0f);
                }
            }

            // The piloted jet, if currently flying it -- Star System View
            // originally had no equivalent of the lensed view's jet overlay
            // at all, so arriving here while piloting left the Endurance
            // invisible even though flight controls kept working
            // underneath. renderer.BeginFrame above already set up the
            // matrices/depth state jetRenderer.Draw needs (it manages its
            // own shader/VAO state regardless), so no separate overlay pass
            // is needed here the way the lensed branch below needs one.
            if (camera.mode == Rendering::Camera::Mode::Jet && jetRenderer.IsLoaded()) {
                const glm::vec3 lightDirection = glm::length(jetPhysics.position) > 1e-4f
                                                      ? glm::normalize(jetPhysics.position)
                                                      : glm::vec3(0.0f, 0.0f, -1.0f);
                jetRenderer.Draw(jetPhysics.position, jetPhysics.rotation, camera.ViewMatrix(),
                                  camera.ProjectionMatrix(aspect), lightDirection);
            }
        } else if (viewLensedRender && lensedTextureWidth > 0) {
            glViewport(0, 0, width, height);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderer.DrawFullscreenTexture(lensedTexture);

            // Composite the piloted jet and any stars on top of the
            // ray-traced image, using the SAME camera the tracer used this
            // frame (see BuildCameraFrame) so both line up with the lensed
            // background. The jet is lit from the black hole's direction --
            // it's the only light source in this scene.
            const bool jetVisible = camera.mode == Rendering::Camera::Mode::Jet && jetRenderer.IsLoaded();
            if (jetVisible || !stars.empty() || !planets.empty()) {
                renderer.BeginOverlayPass(camera, aspect);
                if (jetVisible) {
                    const glm::vec3 lightDirection = glm::length(jetPhysics.position) > 1e-4f
                                                          ? glm::normalize(jetPhysics.position)
                                                          : glm::vec3(0.0f, 0.0f, -1.0f);
                    jetRenderer.Draw(jetPhysics.position, jetPhysics.rotation, camera.ViewMatrix(),
                                      camera.ProjectionMatrix(aspect), lightDirection);
                }
                DrawStars();
                DrawPlanets(false);
            }
        } else {
            renderer.BeginFrame(camera, aspect, width, height);
            DrawStars();
            DrawPlanets(true);

            if (showGrid) {
                renderer.DrawGrid(40.0, 40, glm::vec3(0.15f, 0.15f, 0.2f));
            }
            if (showWarpedGrid) {
                const glm::vec3 funnelColor(0.3f, 0.55f, 0.9f);
                if (binaryModeEnabled) {
                    // Capped well under the separation so the two funnels
                    // don't visually swallow each other at close range by
                    // default -- purely a display choice, not a physical
                    // cutoff (the real embedding keeps extending outward).
                    const double outerRadius = std::min(20.0, binarySystem.separation * 0.45);
                    renderer.DrawFlammParaboloid(binarySystem.Position1(), binarySystem.SchwarzschildRadius1(),
                                                  outerRadius, funnelColor);
                    renderer.DrawFlammParaboloid(binarySystem.Position2(), binarySystem.SchwarzschildRadius2(),
                                                  outerRadius, funnelColor);
                } else {
                    renderer.DrawFlammParaboloid(glm::dvec3(0.0), BlackHole::SchwarzschildRadiusSim(), 20.0,
                                                  funnelColor);
                }
                // Extra black holes (see Physics/MultiBlackHoleSystem.h)
                // each get their own funnel too, same approximation/caveat
                // as the primary's -- additive to whichever of the two
                // blocks above already ran, not a replacement for them.
                for (const Physics::ExtraBlackHole& extra : extraBlackHoles) {
                    renderer.DrawFlammParaboloid(extra.position, extra.SchwarzschildRadiusSim(), 20.0,
                                                  glm::vec3(0.7f, 0.25f, 0.8f));
                }
            }
            if (showHorizon) {
                if (binaryModeEnabled) {
                    const glm::dvec3 pos1 = binarySystem.Position1();
                    const glm::dvec3 pos2 = binarySystem.Position2();
                    renderer.DrawSphere(pos1, binarySystem.SchwarzschildRadius1(), glm::vec3(0.0f, 0.0f, 0.0f));
                    renderer.DrawWireSphere(pos1, binarySystem.SchwarzschildRadius1(), glm::vec3(0.6f, 0.1f, 0.1f));
                    renderer.DrawSphere(pos2, binarySystem.SchwarzschildRadius2(), glm::vec3(0.0f, 0.0f, 0.0f));
                    renderer.DrawWireSphere(pos2, binarySystem.SchwarzschildRadius2(), glm::vec3(0.1f, 0.3f, 0.8f));

                    // Orbit paths, so the motion is visible at a glance
                    // (and even while paused) -- sampled circles at each
                    // body's own orbital radius, not scripted decoration.
                    constexpr int kOrbitSamples = 96;
                    std::vector<glm::dvec3> orbit1, orbit2;
                    orbit1.reserve(kOrbitSamples + 1);
                    orbit2.reserve(kOrbitSamples + 1);
                    for (int i = 0; i <= kOrbitSamples; ++i) {
                        const double t = 2.0 * glm::pi<double>() * i / kOrbitSamples;
                        orbit1.push_back(binarySystem.Radius1() * glm::dvec3(std::cos(t), std::sin(t), 0.0));
                        orbit2.push_back(-binarySystem.Radius2() * glm::dvec3(std::cos(t), std::sin(t), 0.0));
                    }
                    renderer.DrawPath(orbit1, glm::vec3(0.6f, 0.1f, 0.1f), 1.0f);
                    renderer.DrawPath(orbit2, glm::vec3(0.1f, 0.3f, 0.8f), 1.0f);
                } else {
                    renderer.DrawSphere(BlackHole::SchwarzschildRadiusSim(), glm::vec3(0.0f, 0.0f, 0.0f));
                    renderer.DrawWireSphere(BlackHole::SchwarzschildRadiusSim(), glm::vec3(0.6f, 0.1f, 0.1f));
                }
                // Extra black holes -- additive, same reasoning as the
                // Flamm's-paraboloid loop just above.
                for (const Physics::ExtraBlackHole& extra : extraBlackHoles) {
                    const double rs = extra.SchwarzschildRadiusSim();
                    renderer.DrawSphere(extra.position, rs, glm::vec3(0.0f, 0.0f, 0.0f));
                    renderer.DrawWireSphere(extra.position, rs, glm::vec3(0.7f, 0.25f, 0.8f));
                }
            }
            if (showPhotonSphere && !binaryModeEnabled && extraBlackHoles.empty()) {
                // No exact photon-sphere formula for binary/multi-body mode
                // (see docs/physics.md) -- skipped there rather than drawn
                // at a misleading single-body value.
                renderer.DrawWireSphere(BlackHole::PhotonSphereRadiusSim(), glm::vec3(0.9f, 0.75f, 0.2f), 12, 24);
            }

            if (showAnalyticCurve) {
                // Draw each unique impact parameter's exact Kepler curve once
                // (reuse the RK4 particle's elements; all three methods share
                // the same initial condition and therefore the same analytic orbit).
                for (const auto& p : simulation.Particles()) {
                    if (p.method != IntegratorMethod::RK4) continue;
                    std::vector<glm::dvec3> curve;
                    curve.reserve(200);
                    for (int i = 0; i <= 200; ++i) {
                        const double theta = -3.0 + 6.0 * i / 200.0; // sample a wide angular range
                        const double r = Physics::KeplerRadiusAtTrueAnomaly(p.analytic, theta);
                        if (r <= 0.0) continue;
                        const glm::dvec3 dir = std::cos(theta) * p.analytic.periapsisDir +
                                                std::sin(theta) * glm::cross(p.analytic.orbitNormal, p.analytic.periapsisDir);
                        curve.push_back(r * dir);
                    }
                    renderer.DrawPath(curve, glm::vec3(0.5f, 0.5f, 0.5f), 1.0f);
                }
            }

            if (showNewtonianTrajectories) {
                for (const auto& p : simulation.Particles()) {
                    renderer.DrawPath(p.path, ColorForMethod(p.method), 2.0f);
                    renderer.DrawPoint(p.state.position, ColorForMethod(p.method), 5.0f);
                }
            }

            if (showPhotonGeodesics) {
                for (const auto& p : photonTraces) {
                    renderer.DrawPath(p.path, ColorForPhotonFate(p.fate), 2.5f);
                }
            }

            if (showCameraDebugRays) {
                for (const auto& p : cameraDebugRays) {
                    renderer.DrawPath(p.path, ColorForPhotonFate(p.fate), 2.0f);
                }
                renderer.DrawPoint(camera.Position(), glm::vec3(1.0f, 1.0f, 1.0f), 8.0f);

                if (useEinsteinRingSource) {
                    // Visualize the Stage 4 alignment: a line from the black
                    // hole out along the source direction (camera.forward),
                    // far past the horizon, showing camera/hole/source are collinear.
                    const glm::dvec3 sourceDirection = glm::normalize(-camera.Position());
                    std::vector<glm::dvec3> alignmentLine = {glm::dvec3(0.0), sourceDirection * 60.0};
                    renderer.DrawPath(alignmentLine, glm::vec3(1.0f, 1.0f, 0.4f), 1.5f);
                }
            }

            if (showPhotonInspector && inspectorShowPath && !inspectorResult.path.empty()) {
                // Bright magenta, thicker than the other debug-ray colors
                // (which are fate-colored, same as everything else) so
                // the one specifically-targeted ray stands out from any
                // sweep of debug rays also being shown at the same time.
                renderer.DrawPath(inspectorResult.path, glm::vec3(1.0f, 0.2f, 0.9f), 3.0f);
                if (!inspectorResult.path.empty()) {
                    renderer.DrawPoint(inspectorResult.path.front(), glm::vec3(1.0f, 1.0f, 1.0f), 7.0f);
                }
            }
        }

        // --- Star System View info overlay: real catalog facts about the
        // visited star plus any real exoplanets around it (see the Star
        // System View render branch above and Universe/ExoplanetCatalog.h)
        // shown directly in the scene, so arriving somewhere feels like
        // learning something real about it rather than only being a
        // changed background.
        if (inStarSystemView && visitedStarIndex >= 0 && visitedStarIndex < static_cast<int>(universeCatalog.size())) {
            const Universe::CatalogObject& star = universeCatalog[static_cast<size_t>(visitedStarIndex)];
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Pos.x + 16.0f, ImGui::GetMainViewport()->Pos.y + 70.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.55f);
            if (ImGui::Begin("Star System", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
                ImGui::Text("%s", star.name);
                ImGui::Text("%s", star.notes);
                ImGui::Text("Mass: %.3g M_sun   Surface temp: %.0f K   Distance from Sol: %.4g ly",
                            star.massSolar, star.temperatureK, star.distanceLy);
                bool anyPlanets = false;
                for (const Universe::ExoplanetEntry& planet : exoplanetCatalog) {
                    if (std::string(planet.hostStarName) != star.name) continue;
                    anyPlanets = true;
                    const double periodDays = Universe::OrbitalPeriodDays(planet.semiMajorAxisAu, star.massSolar);
                    ImGui::Separator();
                    ImGui::Text("%s%s", planet.name, planet.detectionDisputed ? " (disputed detection)" : "");
                    ImGui::Text("  %.3g AU, %.2g Earth masses, real orbital period %.3g days",
                                planet.semiMajorAxisAu, planet.massEarth, periodDays);
                    ImGui::TextWrapped("  %s", planet.notes);
                }
                if (!anyPlanets) {
                    ImGui::Separator();
                    ImGui::TextDisabled("No exoplanets confirmed here (as cataloged in this app).");
                }
            }
            ImGui::End();
        }

        // --- HUD compass: a horizontal strip across the top of the screen
        // showing the relative bearing (left/right of straight ahead) to
        // nearby points of interest -- the black hole always, plus the
        // piloted jet while flying it. Bearing is computed by projecting
        // each POI's offset from the camera onto the camera's own forward/
        // right axes, so it tracks whichever camera mode is active (Orbit,
        // FreeFly, or Jet) without any mode-specific logic here.
        {
            struct PointOfInterest {
                const char* name;
                glm::dvec3 position;
                ImU32 color;
            };
            std::vector<PointOfInterest> pointsOfInterest;
            // In Star System View the origin is a visited star, not the
            // configured black hole (the ray tracer isn't even running --
            // see that render branch above) -- label it correctly instead
            // of calling an ordinary star "Black Hole". Multi-Star System
            // View has no single origin at all -- every member gets its
            // own compass entry at its own (moving) position instead.
            if (inMultiStarView) {
                for (size_t i = 0; i < multiStarPositions.size() && i < activeMultiStarSystem.members.size(); ++i) {
                    pointsOfInterest.push_back({activeMultiStarSystem.members[i].name.c_str(),
                                                 multiStarPositions[i], IM_COL32(255, 220, 140, 255)});
                }
            } else if (inStarSystemView && visitedStarIndex >= 0) {
                pointsOfInterest.push_back({universeCatalog[static_cast<size_t>(visitedStarIndex)].name,
                                             glm::dvec3(0.0), IM_COL32(255, 220, 140, 255)});
            } else {
                pointsOfInterest.push_back({"Black Hole", glm::dvec3(0.0), IM_COL32(255, 170, 60, 255)});
            }
            if (camera.mode == Rendering::Camera::Mode::Jet) {
                pointsOfInterest.push_back(
                    {"Jet", glm::dvec3(jetPhysics.position), IM_COL32(120, 220, 255, 255)});
            }

            const glm::dvec3 camPos = camera.Position();
            const Rendering::Camera::Basis basis = camera.OrthonormalBasis();
            const double metersPerSimUnit = Physics::Constants::GravitationalRadiusMeters(blackHole.MassKg());

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float barWidth = std::min(viewport->Size.x * 0.6f, 640.0f);
            const float barHeight = 48.0f;
            constexpr double kHalfFovDegrees = 70.0; // the strip's full width spans this many degrees either side of dead ahead

            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + (viewport->Size.x - barWidth) * 0.5f, viewport->Pos.y + 10.0f));
            ImGui::SetNextWindowSize(ImVec2(barWidth, barHeight));
            ImGui::SetNextWindowBgAlpha(0.3f);
            const ImGuiWindowFlags compassFlags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            if (ImGui::Begin("##Compass", nullptr, compassFlags)) {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 winPos = ImGui::GetWindowPos();
                const ImVec2 winSize = ImGui::GetWindowSize();
                const float centerX = winPos.x + winSize.x * 0.5f;
                const float midY = winPos.y + winSize.y * 0.5f;
                const float halfTrackWidth = winSize.x * 0.5f - 14.0f; // leaves room for edge arrows

                draw->AddLine(ImVec2(winPos.x, midY), ImVec2(winPos.x + winSize.x, midY), IM_COL32(255, 255, 255, 50), 1.0f);
                draw->AddTriangleFilled(ImVec2(centerX - 5.0f, winPos.y + 2.0f), ImVec2(centerX + 5.0f, winPos.y + 2.0f),
                                         ImVec2(centerX, winPos.y + 11.0f), IM_COL32(255, 255, 255, 170));

                for (const PointOfInterest& poi : pointsOfInterest) {
                    const glm::dvec3 relative = poi.position - camPos;
                    const double distanceSim = glm::length(relative);
                    if (distanceSim < 1e-9) continue; // POI coincides with the camera (e.g. jet in cockpit view) -- no bearing to show

                    const double forwardComponent = glm::dot(relative, basis.forward);
                    const double rightComponent = glm::dot(relative, basis.right);
                    const double bearingDegrees = glm::degrees(std::atan2(rightComponent, forwardComponent));
                    const bool behindOrWide = std::abs(bearingDegrees) > kHalfFovDegrees;
                    const double clampedBearing = std::clamp(bearingDegrees, -kHalfFovDegrees, kHalfFovDegrees);
                    const float x = centerX + static_cast<float>(clampedBearing / kHalfFovDegrees) * halfTrackWidth;

                    if (behindOrWide) {
                        const float dir = bearingDegrees > 0.0 ? 1.0f : -1.0f;
                        draw->AddTriangleFilled(ImVec2(x, midY - 5.0f), ImVec2(x, midY + 5.0f),
                                                 ImVec2(x + dir * 7.0f, midY), poi.color);
                    } else {
                        draw->AddLine(ImVec2(x, winPos.y + 8.0f), ImVec2(x, winPos.y + winSize.y - 16.0f), poi.color, 2.0f);
                    }

                    // Star System View's sim units are an arbitrary fixed
                    // visual scale (see that render branch's comment), not
                    // tied to blackHole's mass -- a real-meters conversion
                    // there would be meaningless, so the label stays in
                    // plain sim units instead of pretending otherwise.
                    char distanceLabelBuffer[64];
                    if (inStarSystemView || inMultiStarView) {
                        std::snprintf(distanceLabelBuffer, sizeof(distanceLabelBuffer), "%.1f (visual scale)", distanceSim);
                    } else {
                        const std::string formatted = FormatDistance(distanceSim * metersPerSimUnit);
                        std::snprintf(distanceLabelBuffer, sizeof(distanceLabelBuffer), "%s", formatted.c_str());
                    }
                    const std::string label = std::string(poi.name) + "  " + distanceLabelBuffer;
                    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                    const float labelX = std::clamp(x - textSize.x * 0.5f, winPos.x, winPos.x + winSize.x - textSize.x);
                    draw->AddText(ImVec2(labelX, winPos.y + winSize.y - 15.0f), poi.color, label.c_str());
                }
            }
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);
    }

    return 0;
}

} // namespace Application
