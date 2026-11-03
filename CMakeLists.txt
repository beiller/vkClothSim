cmake_minimum_required(VERSION 3.24)
project(3dsim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
add_compile_options(-march=native)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
find_library(VULKAN_LOADER_PATH NAMES vulkan.1 vulkan)
add_subdirectory(lib/glfw)
# GLFW dlopen()s the loader itself for glfwCreateWindowSurface; on macOS the bare
# name (libvulkan.1.dylib) isn't resolvable, so point it at the loader's absolute path.
if(VULKAN_LOADER_PATH AND TARGET glfw)
    target_compile_definitions(glfw PRIVATE _GLFW_VULKAN_LIBRARY="${VULKAN_LOADER_PATH}")
endif()

find_package(Vulkan REQUIRED)
find_package(ZLIB REQUIRED)
find_program(XXD xxd REQUIRED)
find_program(GLSLC NAMES glslc)
find_program(SPIRVGLS NAMES glslangValidator)
if(GLSLC)
    set(GLSLC_BIN ${GLSLC})
    set(GLSLC_ARGS -O)
elseif(SPIRVGLS)
    set(GLSLC_BIN ${SPIRVGLS})
    set(GLSLC_ARGS -V -O)
else()
    message(FATAL_ERROR "neither glslc nor glslangValidator found")
endif()

set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN_DIR})

function(spirv base file)
    execute_process(COMMAND ${GLSLC_BIN} ${GLSLC_ARGS} ${file} -o ${GEN_DIR}/${base}.spv RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "glslc failed for ${base}")
    endif()
    execute_process(COMMAND ${XXD} -i -n ${base}_spv ${GEN_DIR}/${base}.spv
        OUTPUT_FILE ${GEN_DIR}/${base}_spv.hpp RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "xxd failed for ${base}")
    endif()
endfunction()

spirv(mesh_vert ${CMAKE_SOURCE_DIR}/shaders/mesh_vert.vert)
spirv(mesh_frag ${CMAKE_SOURCE_DIR}/shaders/mesh_frag.frag)
spirv(shadow_vert ${CMAKE_SOURCE_DIR}/shaders/shadow.vert)
spirv(shadow_frag ${CMAKE_SOURCE_DIR}/shaders/shadow.frag)
spirv(tonemap_vert ${CMAKE_SOURCE_DIR}/shaders/tonemap.vert)
spirv(tonemap_frag ${CMAKE_SOURCE_DIR}/shaders/tonemap.frag)
spirv(softbody ${CMAKE_SOURCE_DIR}/shaders/softbody.comp)
spirv(sss_blur ${CMAKE_SOURCE_DIR}/shaders/sss_blur.comp)
spirv(skeleton ${CMAKE_SOURCE_DIR}/shaders/skeleton.comp)
spirv(env_cube ${CMAKE_SOURCE_DIR}/shaders/env_cube.comp)
spirv(env_prefilter ${CMAKE_SOURCE_DIR}/shaders/env_prefilter.comp)
spirv(env_irradiance ${CMAKE_SOURCE_DIR}/shaders/env_irradiance.comp)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/lib/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)

set(PHYSICS_REPO_ROOT ${CMAKE_SOURCE_DIR}/lib/jolt)
set(JPH_BUILD_SHARED_LIBS OFF)
set(ENABLE_OBJECT_STREAM OFF)
set(JPH_USE_VK OFF)
set(JPH_USE_DX12 OFF)
set(JPH_USE_CPU_COMPUTE OFF)
include(${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake)

find_path(LUA_INCLUDE_DIR lua.hpp PATH_SUFFIXES lua5.4)
find_library(LUA_LIBRARY NAMES lua5.4 lua)
if(NOT LUA_INCLUDE_DIR OR NOT LUA_LIBRARY)
    message(FATAL_ERROR "Lua 5.4 not found (need lua5.4 dev headers + lib)")
endif()

# third-party (object/static)
add_library(tinygltf_v3 STATIC ${CMAKE_SOURCE_DIR}/lib/tinygltf/tiny_gltf_v3.c)
target_include_directories(tinygltf_v3 PUBLIC ${CMAKE_SOURCE_DIR}/lib/tinygltf)
add_library(tinyexr STATIC ${CMAKE_SOURCE_DIR}/lib/tinyexr/tinyexr.cc)
target_include_directories(tinyexr PUBLIC ${CMAKE_SOURCE_DIR}/lib/tinyexr)
add_library(miniz STATIC ${CMAKE_SOURCE_DIR}/lib/miniz/miniz.c)
target_include_directories(miniz PUBLIC ${CMAKE_SOURCE_DIR}/lib/miniz)
# CoACD (approximate convex decomposition, SIGGRAPH'22). Built in "OFF" mode: no boost/
# openvdb/spdlog/zlib - just its own sources + the vendored CDT headers + std::threads.
file(GLOB_RECURSE COACD_SRC ${CMAKE_SOURCE_DIR}/lib/coacd/src/*.cpp ${CMAKE_SOURCE_DIR}/lib/coacd/src/*.cc)
list(FILTER COACD_SRC EXCLUDE REGEX ".*/preprocess.*")
add_library(coacd STATIC ${COACD_SRC} ${CMAKE_SOURCE_DIR}/lib/coacd/public/coacd.cpp)
target_compile_features(coacd PRIVATE cxx_std_20)
target_include_directories(coacd PUBLIC ${CMAKE_SOURCE_DIR}/lib/coacd/public
                                     PRIVATE ${CMAKE_SOURCE_DIR}/lib/coacd/3rd/cdt/CDT/include)
target_compile_definitions(coacd PRIVATE WITH_3RD_PARTY_LIBS=0 DISABLE_SPDLOG WITH_STD_THREADS=1)
find_package(Threads REQUIRED)
target_link_libraries(coacd PUBLIC Threads::Threads)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PRIVATE glfw)
target_compile_features(imgui PRIVATE cxx_std_23)

# subsystem libraries
add_library(3dsim_core STATIC
    src/core/components.cpp
    src/core/url.cpp)
target_compile_features(3dsim_core PRIVATE cxx_std_23)
target_include_directories(3dsim_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/lib/entt/single_include
    ${CMAKE_SOURCE_DIR}/lib/glm
    ${CMAKE_SOURCE_DIR}/lib/json)

add_library(3dsim_io STATIC
    src/io/gltf.cpp
    src/io/assets.cpp)
target_compile_features(3dsim_io PRIVATE cxx_std_23)
target_include_directories(3dsim_io PUBLIC ${CMAKE_SOURCE_DIR}/src/io PRIVATE ${CMAKE_SOURCE_DIR}/lib/stb)
target_link_libraries(3dsim_io PUBLIC 3dsim_core PRIVATE tinygltf_v3 tinyexr miniz ZLIB::ZLIB)

add_library(3dsim_solvers STATIC solvers/collision_shape_solver.cpp)
target_compile_features(3dsim_solvers PRIVATE cxx_std_23)
target_include_directories(3dsim_solvers PUBLIC ${CMAKE_SOURCE_DIR}/solvers)

add_library(3dsim_physics STATIC
    src/physics/rigid.cpp
    src/physics/physics_server.cpp
    src/physics/mesh_decomp.cpp)
target_compile_features(3dsim_physics PRIVATE cxx_std_23)
target_include_directories(3dsim_physics PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(3dsim_physics PUBLIC 3dsim_core PRIVATE Jolt::Jolt coacd)

set(XR_SOURCES src/xr/openxr_loader.cpp src/xr/xr.cpp)
set(XR_INCLUDES ${CMAKE_SOURCE_DIR}/lib/openxr)
add_library(3dsim_xr STATIC ${XR_SOURCES})
target_compile_features(3dsim_xr PRIVATE cxx_std_23)
target_include_directories(3dsim_xr PRIVATE ${CMAKE_SOURCE_DIR}/src/xr ${CMAKE_SOURCE_DIR}/src/io ${XR_INCLUDES})
target_link_libraries(3dsim_xr PUBLIC 3dsim_core PRIVATE Vulkan::Vulkan glfw)

add_library(3dsim_ui STATIC
    src/ui/ui.cpp)
target_compile_features(3dsim_ui PRIVATE cxx_std_23)
target_include_directories(3dsim_ui PRIVATE ${CMAKE_SOURCE_DIR}/src/ui ${CMAKE_SOURCE_DIR}/src/io ${GEN_DIR})
target_link_libraries(3dsim_ui PUBLIC 3dsim_core imgui PRIVATE Vulkan::Vulkan glfw)

add_library(3dsim_graphics STATIC
    src/vk/vkapp.cpp
    src/vk/renderer.cpp
    src/vk/renderer_env.cpp
    src/vk/renderer_shadow.cpp
    src/vk/graphics_server.cpp
    src/gpu_sim/soft.cpp
    src/gpu_sim/dualquat.cpp
    src/gpu_sim/skin.cpp)
target_compile_features(3dsim_graphics PRIVATE cxx_std_23)
target_include_directories(3dsim_graphics PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/vk ${CMAKE_SOURCE_DIR}/src/io ${CMAKE_SOURCE_DIR}/src/ui ${GEN_DIR})
target_link_libraries(3dsim_graphics PUBLIC 3dsim_core 3dsim_ui PRIVATE glfw Vulkan::Vulkan)

add_library(3dsim_script STATIC
    src/script/lua_host.cpp
    src/script/api_math.cpp
    src/script/api_components.cpp
    src/script/api_scene.cpp
    src/script/api_ui.cpp)
target_compile_features(3dsim_script PRIVATE cxx_std_23)
target_include_directories(3dsim_script PRIVATE ${CMAKE_SOURCE_DIR}/src/vk ${CMAKE_SOURCE_DIR}/src/ui ${LUA_INCLUDE_DIR})
target_link_libraries(3dsim_script PUBLIC 3dsim_core 3dsim_io 3dsim_graphics 3dsim_ui 3dsim_physics PRIVATE Jolt::Jolt ${LUA_LIBRARY})

# the app: glue (main loop, systems, scene loading) + all subsystems
add_executable(vksim
    src/main.cpp
    src/systems.cpp
    src/scene.cpp)
target_compile_features(vksim PRIVATE cxx_std_23)
target_include_directories(vksim PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/vk ${CMAKE_SOURCE_DIR}/src/gpu_sim ${CMAKE_SOURCE_DIR}/src/ui ${XR_INCLUDES} ${GEN_DIR})
target_link_libraries(vksim PRIVATE
    3dsim_script 3dsim_physics 3dsim_graphics 3dsim_xr 3dsim_io
    glfw Vulkan::Vulkan Jolt::Jolt ZLIB::ZLIB dl ${LUA_LIBRARY})

# CPU self-test for the ported dual-quaternion math (Blender ground truth). Build with -Dvksim's
# default; run ./build/dualquat_test (no GPU needed).
add_executable(dualquat_test tests/dualquat_test.cpp src/gpu_sim/dualquat.cpp)
target_compile_features(dualquat_test PRIVATE cxx_std_23)
target_include_directories(dualquat_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/core ${CMAKE_SOURCE_DIR}/lib/glm)

# CPU self-test: the eyeVP (projFov * poseView) pipeline maps known points to expected NDC.
add_executable(view_proj_test tests/view_proj_test.cpp)
target_compile_features(view_proj_test PRIVATE cxx_std_23)
target_include_directories(view_proj_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/core ${CMAKE_SOURCE_DIR}/lib/glm)

# CPU self-test for the tapered-capsule collision shape fitter.
add_executable(collision_shape_solver_test solvers/collision_shape_solver_test.cpp)
target_compile_features(collision_shape_solver_test PRIVATE cxx_std_23)
target_link_libraries(collision_shape_solver_test PRIVATE 3dsim_solvers)

# GPU-free probe: core + IO (GLTF loader) + collision shape solver (no Vulkan/GLFW/Jolt/ImGui).
add_executable(bone_probe solvers/bone_probe.cpp)
target_compile_features(bone_probe PRIVATE cxx_std_23)
target_link_libraries(bone_probe PRIVATE 3dsim_io 3dsim_solvers)


