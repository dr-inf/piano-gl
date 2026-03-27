include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.9
)

set(GLAD_PROFILE "core" CACHE STRING "" FORCE)
set(GLAD_API "gl=4.6" CACHE STRING "" FORCE)
set(GLAD_GENERATOR "c" CACHE STRING "" FORCE)
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v0.1.36
)

FetchContent_MakeAvailable(glfw glad)

add_executable(keys_example
    main.cpp
    midi_player.cpp
)

target_link_libraries(keys_example
    PRIVATE
        keys
        glad
        glfw
)

target_include_directories(keys_example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}  # For midi_player.h
)

target_compile_options(keys_example PRIVATE -Wall -Wextra -Wpedantic)
target_compile_features(keys_example PRIVATE cxx_std_20)
