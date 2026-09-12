cmake_minimum_required(VERSION 3.24)
project(3dsim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- standalone GLFW (window + VkSurface) for vksim ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/glfw)

# --- Vulkan ---
find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)
find_program(XXD xxd REQUIRED)

set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN_DIR})

# spirv(base file) -> ${GEN_DIR}/base.spv + ${GEN_DIR}/base_spv.hpp (baked at configure
# time so generated headers are always present before any compile; avoids -j races).
# Re-run `cmake -S . -B build` after editing a shader.
function(spirv base file)
    execute_process(COMMAND ${GLSLC} ${file} -o ${GEN_DIR}/${base}.spv RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "glslc failed for ${base}")
    endif()
    execute_process(COMMAND ${XXD} -i -n ${base}_spv ${GEN_DIR}/${base}.spv
        OUTPUT_FILE ${GEN_DIR}/${base}_spv.hpp RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "xxd failed for ${base}")
    endif()
endfunction()

spirv(vk_bg_vert ${CMAKE_SOURCE_DIR}/shaders/vk_bg_vert.vert)
spirv(vk_bg_frag ${CMAKE_SOURCE_DIR}/shaders/vk_bg_frag.frag)
spirv(caps_vert ${CMAKE_SOURCE_DIR}/shaders/caps_vert.vert)
spirv(caps_frag ${CMAKE_SOURCE_DIR}/shaders/caps_frag.frag)
spirv(cloth_vert ${CMAKE_SOURCE_DIR}/shaders/cloth_vert.vert)
spirv(cloth_frag ${CMAKE_SOURCE_DIR}/shaders/cloth_frag.frag)
spirv(softbody ${CMAKE_SOURCE_DIR}/shaders/softbody.comp)
spirv(ball_vert ${CMAKE_SOURCE_DIR}/shaders/ball_vert.vert)
spirv(ball_frag ${CMAKE_SOURCE_DIR}/shaders/ball_frag.frag)

# --- Dear ImGui (core + vulkan + glfw backends) ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/lib/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)

# --- Jolt Physics (vendored, C++): sets CMAKE_CXX_STANDARD 17; Jolt target stays C++17 ---
set(PHYSICS_REPO_ROOT ${CMAKE_SOURCE_DIR}/lib/jolt)
set(JPH_BUILD_SHARED_LIBS OFF)
set(ENABLE_OBJECT_STREAM OFF)
set(JPH_USE_VK OFF)
set(JPH_USE_DX12 OFF)
set(JPH_USE_CPU_COMPUTE OFF)
include(${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake)

# --- vksim : the app (Vulkan + GLFW + ImGui + Jolt + CPU soft-body sim) ---
#   Split by concern:
#     sim/sim.hpp            - universal soft-body solver (no Vulkan/Jolt/ImGui)
#     app/params.hpp         – the user-tunable soft-body sim params (SimParams)
#     app/rigid.{hpp,cpp}    - rigid-body physics (Jolt: ground + 16 clumping capsules)
#     app/scene.{hpp,cpp}    - the simulation side: steps the rigid + the soft bodies
#     app/ui.{hpp,cpp}       - the Dear ImGui overlay (HUD + controls)
#     vk/vkapp.{hpp,cpp}     - the window (GLFW) + the Vulkan core (device/swapchain/...)
#     vk/renderer.{hpp,cpp}  - the GPU side: pipelines + buffers + the per-frame draw
#     main.cpp               - the app: ties the pieces together + the frame loop
add_executable(vksim
    src/main.cpp
    src/app/scene.cpp
    src/app/rigid.cpp
    src/app/ui.cpp
    src/vk/vkapp.cpp
    src/vk/renderer.cpp
    ${IMGUI_SOURCES})
target_link_libraries(vksim PRIVATE glfw Vulkan::Vulkan Jolt::Jolt)
target_compile_features(vksim PRIVATE cxx_std_23)
target_include_directories(vksim PRIVATE
    ${GEN_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/src)

# --- simtest : headless soft-body test (pure sim/, no Vulkan/Jolt/ImGui) ---
add_executable(simtest src/simtest.cpp)
target_compile_features(simtest PRIVATE cxx_std_23)
target_include_directories(simtest PRIVATE ${CMAKE_SOURCE_DIR}/src)

# --- penprobe : headless one-way-collision probe (Jolt + the sim, no Vulkan) ---
add_executable(penprobe src/penprobe.cpp src/app/scene.cpp src/app/rigid.cpp)
target_link_libraries(penprobe PRIVATE Jolt::Jolt)
target_compile_features(penprobe PRIVATE cxx_std_23)
target_include_directories(penprobe PRIVATE ${CMAKE_SOURCE_DIR}/src)

# --- official ImGui GLFW+Vulkan example (diagnostic reference) ---
add_executable(imgui_example
    ${IMGUI_DIR}/examples/example_glfw_vulkan/main.cpp
    ${IMGUI_SOURCES})
target_link_libraries(imgui_example PRIVATE glfw Vulkan::Vulkan)
target_include_directories(imgui_example PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui_example PRIVATE VK_PROTOTYPES)

# --- headless Vulkan SDF smoke test (sim3d) ---
add_executable(sim3d src/smoke.cpp)
target_link_libraries(sim3d PRIVATE Vulkan::Vulkan)
target_include_directories(sim3d PRIVATE ${GEN_DIR})
spirv(smoke ${CMAKE_SOURCE_DIR}/shaders/smoke.comp)

# --- raylib/GL capsule viewer (optional; pure-Vulkan is the main path) ---
option(BUILD_RAYLIB_VIEWER "Build caps3d (raylib/GL viewer)" OFF)
if(BUILD_RAYLIB_VIEWER)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(OPENGL_VERSION "4.3" CACHE STRING "" FORCE)
    add_subdirectory(lib/raylib)
    add_executable(caps3d src/caps3d.cpp)
    target_link_libraries(caps3d PRIVATE raylib)
    target_include_directories(caps3d PRIVATE ${CMAKE_SOURCE_DIR}/lib/raylib/src)
endif()
