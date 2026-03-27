set(SHADER_FILES
    ${PROJECT_SOURCE_DIR}/shaders/pbr.vert
    ${PROJECT_SOURCE_DIR}/shaders/pbr.frag
    ${PROJECT_SOURCE_DIR}/shaders/skybox.vert
    ${PROJECT_SOURCE_DIR}/shaders/skybox.frag
    ${PROJECT_SOURCE_DIR}/shaders/shadow_depth.vert
    ${PROJECT_SOURCE_DIR}/shaders/shadow_depth.frag
    ${PROJECT_SOURCE_DIR}/shaders/equirect_to_cube.vert
    ${PROJECT_SOURCE_DIR}/shaders/equirect_to_cube.frag
)

set(GENERATED_SHADER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/keys_shaders.hpp)
add_custom_command(
    OUTPUT ${GENERATED_SHADER_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/cmake/embed_shaders.py ${GENERATED_SHADER_HEADER} ${SHADER_FILES}
    DEPENDS ${SHADER_FILES} ${PROJECT_SOURCE_DIR}/cmake/embed_shaders.py
    COMMENT "Embedding shader sources into header"
)
add_custom_target(keys_shaders DEPENDS ${GENERATED_SHADER_HEADER})

add_library(keys STATIC
    ${PROJECT_SOURCE_DIR}/src/renderer.cpp
    ${GENERATED_SHADER_HEADER}
)

add_dependencies(keys keys_shaders)

target_include_directories(keys
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/vendor
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(keys
    PUBLIC
        glm::glm
        fastgltf::fastgltf
        OpenGL::GL
    PRIVATE
        glad
)

if (MSVC)
    target_compile_options(keys PRIVATE /W4)
else()
    target_compile_options(keys PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_compile_features(keys PRIVATE cxx_std_20)
