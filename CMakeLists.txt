cmake_minimum_required(VERSION 3.24)
project(3dsim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/glfw)

find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)
find_program(XXD xxd REQUIRED)

set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN_DIR})

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

spirv(mesh_vert ${CMAKE_SOURCE_DIR}/shaders/mesh_vert.vert)
spirv(mesh_frag ${CMAKE_SOURCE_DIR}/shaders/mesh_frag.frag)
spirv(softbody ${CMAKE_SOURCE_DIR}/shaders/softbody.comp)

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

add_executable(vksim
    src/main.cpp
    src/app/rigid.cpp
    src/app/ui.cpp
    src/sim/softsim.cpp
    src/vk/vkapp.cpp
    src/vk/renderer.cpp
    ${IMGUI_SOURCES})
target_link_libraries(vksim PRIVATE glfw Vulkan::Vulkan Jolt::Jolt)
target_compile_features(vksim PRIVATE cxx_std_23)
target_include_directories(vksim PRIVATE
    ${GEN_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/lib/entt/single_include
    ${CMAKE_SOURCE_DIR}/src)


