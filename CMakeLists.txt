cmake_minimum_required(VERSION 3.24)
project(3dsim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Vulkan REQUIRED)

# --- GLSL -> SPIR-V -> C header (built at compile time) ---
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN_DIR})

function(vulkan_shader name)
    set(spv ${GEN_DIR}/${name}.spv)
    set(hdr ${GEN_DIR}/${name}_spv.hpp)
    add_custom_command(
        OUTPUT ${hdr}
        COMMAND ${CMAKE_COMMAND} -E env glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.comp -o ${spv}
        COMMAND xxd -i -n ${name}_spv ${spv} > ${hdr}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.comp
        COMMENT "spirv ${name}.comp")
    add_custom_target(${name}_spv DEPENDS ${hdr})
endfunction()

vulkan_shader(smoke)

add_executable(sim3d src/main.cpp)
add_dependencies(sim3d smoke_spv)
target_include_directories(sim3d PRIVATE ${GEN_DIR})
target_link_libraries(sim3d PRIVATE Vulkan::Vulkan)
