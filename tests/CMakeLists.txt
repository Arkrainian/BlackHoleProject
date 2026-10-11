add_executable(BlackHoleTests
    test_main.cpp
    test_schwarzschild.cpp
    test_integrator.cpp
    test_geodesic.cpp
    test_deflection.cpp
    test_shadow.cpp
    test_disk.cpp
    test_kerr.cpp
    test_binary.cpp
    test_planet.cpp
)
target_link_libraries(BlackHoleTests PRIVATE bh_physics bh_astronomy)

# GPU correctness was previously only spot-checked interactively (the
# Advanced-mode "Validate: Compare CPU vs GPU" button) -- this gives it
# automated coverage too, renders a known scene on both tracers and checks
# per-pixel agreement. Only buildable/meaningful when CUDA is enabled.
if(BH_ENABLE_CUDA)
    target_sources(BlackHoleTests PRIVATE test_gpu_render.cpp test_planet_gpu.cpp)
    target_link_libraries(BlackHoleTests PRIVATE bh_raytracing)
endif()

add_test(NAME BlackHoleTests COMMAND BlackHoleTests)
