cmake_minimum_required(VERSION 3.20)

# CUDA is added as a project language only once Stage 7 turns it on, so Stage 1
# can configure/build on a machine without nvcc wired into CMake yet.
option(BH_ENABLE_CUDA "Enable CUDA-accelerated ray tracer (Stage 7+)" OFF)

if(BH_ENABLE_CUDA)
    project(BlackHoleSimulator LANGUAGES CXX CUDA)
else()
    project(BlackHoleSimulator LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    # Scoped to CXX only (via generator expression) -- applying these
    # Windows-style flags unscoped also reaches nvcc's CUDA compilation of
    # cuda/geodesic_kernel.cu, where they aren't auto-wrapped in -Xcompiler
    # and break nvcc's argument parser ("a single input file is required...").
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies (fetched at configure time, not vendored in-repo)
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.9
)

# Jet piloting mode (src/Jet/): the only part of this app that needs modern
# (post-1.1) OpenGL entry points -- glGenVertexArrays, shader functions,
# glGenerateMipmap -- to draw the jet's GLB mesh via VAOs/shaders. Everything
# else (the CUDA/CPU ray tracer's fullscreen blit, Rendering::Renderer's
# debug grid/spheres/paths) stays legacy fixed-function GL, unaffected: the
# default GLFW context here is compatibility profile, not core, so both
# coexist in one context once GLEW loads the extra entry points (see
# App.cpp's glewInit call). Static linking (glew-cmake_BUILD_STATIC) avoids
# needing to ship/copy a glew32.dll next to the exe.
FetchContent_Declare(
    glew
    GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
    GIT_TAG        glew-cmake-2.2.0
)
set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ONLY_LIBS ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm imgui glew)

# Dear ImGui ships as loose sources with no CMakeLists of its own; build the
# core library plus the GLFW+OpenGL3 backends we need as a small static lib.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)

# ---------------------------------------------------------------------------
# Physics engine (pure C++, no GL/CUDA deps) — kept as its own library so it
# can be unit-tested and reused verbatim from the CUDA tracer later.
# ---------------------------------------------------------------------------
file(GLOB BH_PHYSICS_SOURCES CONFIGURE_DEPENDS src/Physics/*.cpp)
add_library(bh_physics STATIC ${BH_PHYSICS_SOURCES})
target_include_directories(bh_physics PUBLIC src)
target_link_libraries(bh_physics PUBLIC glm::glm)

# Astronomy (StarField, AccretionDisk): depends only on Physics + glm, kept
# as its own library (like bh_physics) so tests can link it directly instead
# of only being reachable through the full GL application.
file(GLOB BH_ASTRONOMY_SOURCES CONFIGURE_DEPENDS src/Astronomy/*.cpp)
add_library(bh_astronomy STATIC ${BH_ASTRONOMY_SOURCES})
target_include_directories(bh_astronomy PUBLIC src)
target_link_libraries(bh_astronomy PUBLIC bh_physics glm::glm)

# ---------------------------------------------------------------------------
# Stage 7: CUDA-accelerated ray tracer (optional — off by default so Stages
# 1-6 build without the CUDA toolkit installed). geodesic_kernel.cu is
# compiled by nvcc into its own library; RayTracing/CUDATracer.cpp (plain
# C++, MSVC-compiled) only ever sees cuda/GeodesicKernel.h's plain-C++
# declarations, never CUDA syntax directly.
# ---------------------------------------------------------------------------
if(BH_ENABLE_CUDA)
    add_library(bh_cuda_tracer STATIC cuda/geodesic_kernel.cu)
    target_include_directories(bh_cuda_tracer PRIVATE src cuda)
    target_link_libraries(bh_cuda_tracer PUBLIC bh_physics)
    # "native" (CMake >= 3.24) auto-detects the building machine's GPU.
    # CMake's project(... CUDA) call already assigns CMAKE_CUDA_ARCHITECTURES
    # a stale default (e.g. 52) even when the user never set it, so checking
    # "if NOT DEFINED" here would never trigger -- set the target property
    # directly; pass -DCMAKE_CUDA_ARCHITECTURES=<arch> at configure time to
    # override for a different GPU than the one that built this binary.
    set_target_properties(bh_cuda_tracer PROPERTIES CUDA_ARCHITECTURES "native")

    # GPU acceleration for the planetary destruction particle/spring
    # simulation (Astronomy/PlanetBody.cpp) -- an independent capability
    # from the ray tracer above, in its own target/compile-definition
    # (BH_PLANET_CUDA_AVAILABLE, not BH_CUDA_AVAILABLE) since the two are
    # conceptually separate even though both currently gate on the same
    # BH_ENABLE_CUDA option. See cuda/PlanetKernel.h for why this is a
    # genuinely different kind of GPU workload than ray tracing.
    add_library(bh_planet_cuda STATIC cuda/planet_kernel.cu)
    target_include_directories(bh_planet_cuda PRIVATE src cuda)
    target_link_libraries(bh_planet_cuda PUBLIC bh_physics)
    set_target_properties(bh_planet_cuda PROPERTIES CUDA_ARCHITECTURES "native")

    target_link_libraries(bh_astronomy PUBLIC bh_planet_cuda)
    target_compile_definitions(bh_astronomy PUBLIC BH_PLANET_CUDA_AVAILABLE)
endif()

# ---------------------------------------------------------------------------
# Ray tracing (CPUTracer always; CUDATracer only when BH_ENABLE_CUDA) — its
# own library, like bh_physics/bh_astronomy, so the headless test suite can
# link it directly (render + compare CPU vs GPU) without pulling in the
# windowed application's GL/ImGui/jet dependencies.
# ---------------------------------------------------------------------------
file(GLOB BH_RAYTRACING_SOURCES CONFIGURE_DEPENDS src/RayTracing/*.cpp)
if(NOT BH_ENABLE_CUDA)
    list(REMOVE_ITEM BH_RAYTRACING_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/RayTracing/CUDATracer.cpp)
endif()
add_library(bh_raytracing STATIC ${BH_RAYTRACING_SOURCES})
target_include_directories(bh_raytracing PUBLIC src)
target_link_libraries(bh_raytracing PUBLIC bh_physics bh_astronomy glm::glm)
if(BH_ENABLE_CUDA)
    target_link_libraries(bh_raytracing PUBLIC bh_cuda_tracer)
    target_compile_definitions(bh_raytracing PUBLIC BH_CUDA_AVAILABLE)
endif()

# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------
file(GLOB_RECURSE BH_APP_SOURCES CONFIGURE_DEPENDS
    src/Application/*.cpp
    src/Rendering/*.cpp
    src/UI/*.cpp
    src/Jet/*.cpp
)

add_executable(BlackHoleSimulator src/main.cpp ${BH_APP_SOURCES})
target_include_directories(BlackHoleSimulator PRIVATE src)
# stb_image.h (image loading) and cgltf.h (GLB/glTF parsing for the jet
# model) are single-header libraries vendored in-repo rather than
# FetchContent'd -- neither has (or needs) a build of its own.
target_include_directories(BlackHoleSimulator PRIVATE external/stb external/cgltf)
target_link_libraries(BlackHoleSimulator PRIVATE bh_physics bh_astronomy bh_raytracing glfw imgui libglew_static opengl32)

if(WIN32)
    target_compile_definitions(BlackHoleSimulator PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# The jet model/texture are loaded at runtime via the relative path
# "resources/jet/..." (see src/Jet/JetRenderer.cpp), which only resolves
# when the working directory is the source tree. Mirror resources/ next to
# the built exe so running BlackHoleSimulator.exe directly from the build
# directory (the common case) finds them too.
add_custom_command(TARGET BlackHoleSimulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:BlackHoleSimulator>/resources"
)

# ---------------------------------------------------------------------------
# Tests (Stage 1: a tiny header-only runner; swap for Catch2/GoogleTest later
# once the API surface stabilizes — not worth the dependency yet).
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
