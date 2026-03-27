cmake_minimum_required(VERSION 3.21)

project(keys_project
    VERSION 0.1.0
    DESCRIPTION "Keys renderer library and example"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(OpenGL REQUIRED)

set(FASTGLTF_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_SIMD ON CACHE BOOL "" FORCE)
set(FASTGLTF_COMPILE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG v0.8.0
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0.9.9.8
)

FetchContent_MakeAvailable(fastgltf glm)

add_subdirectory(keys)
add_subdirectory(examples/keys_example)
