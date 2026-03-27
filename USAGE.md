# Building and Using the Keys Library

This guide explains how to build the Keys library and integrate it into your own projects.

## Building the Library

### Build Everything (Library + Example)

```bash
cmake -B build -S .
cmake --build build

# Outputs:
# build/keys/libkeys.a                      # Static library
# build/examples/keys_example/keys_example # Example executable
```

### Build Only the Library (No Example)

```bash
cmake -B build -S .
cmake --build build --target keys

# Output:
# build/keys/libkeys.a  # Static library only
```

### Build Modes

```bash
# Debug build (default)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Release build (optimized)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Release with debug symbols
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

## Using the Library in Your Project

### Method 1: CMake Subdirectory (Recommended)

This is the easiest method - CMake handles all dependencies automatically.

**Project Structure:**
```
your_project/
├── CMakeLists.txt
├── main.cpp
└── external/
    └── keys/          # Clone or copy the keys repository here
```

**Your CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.21)
project(my_piano_app)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the keys library
add_subdirectory(external/keys)

# Your executable
add_executable(my_app main.cpp)

# Link the library
target_link_libraries(my_app PRIVATE keys)

# That's it! CMake automatically handles:
# - Include paths (keys/renderer.hpp)
# - Dependencies (fastgltf, GLM, OpenGL)
# - Shader compilation
```

**Your main.cpp:**
```cpp
#include <keys/renderer.hpp>
#include <iostream>

int main() {
    keys::Renderer renderer;
    keys::InitParams params;
    params.gltfPath = "path/to/piano.gltf";

    if (!renderer.init(params)) {
        std::cerr << "Failed to initialize renderer\n";
        return 1;
    }

    // Your render loop here
    return 0;
}
```

**Building Your Project:**
```bash
cmake -B build -S .
cmake --build build
./build/my_app
```

### Method 2: CMake FetchContent

Download and build the library automatically from GitHub:

```cmake
cmake_minimum_required(VERSION 3.21)
project(my_piano_app)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

FetchContent_Declare(
    keys
    GIT_REPOSITORY https://github.com/dr-inf/piano-gl.git
    GIT_TAG main  # or specific tag/commit
)

FetchContent_MakeAvailable(keys)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE keys)
```

### Method 3: Pre-built Library (Advanced)

If you prefer to use a pre-built library:

**Build and "install" the library:**
```bash
cd piano-gl
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --target keys

# Copy files manually:
# - build/keys/libkeys.a         → your_project/lib/
# - include/keys/                → your_project/include/
# - All dependencies (GLM, fastgltf, etc.) must also be available
```

**Your CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.21)
project(my_piano_app)

set(CMAKE_CXX_STANDARD 20)
find_package(OpenGL REQUIRED)

# Manually specify paths
add_library(keys STATIC IMPORTED)
set_target_properties(keys PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/lib/libkeys.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/include"
)

# You must also link dependencies manually
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE
    keys
    OpenGL::GL
    # Add GLM, fastgltf, etc. manually
)
```

⚠️ **Not recommended** - Method 1 or 2 is much easier!

## Library Details

### What's Included in the Library

The `keys` library provides:
- **Public API**: `include/keys/renderer.hpp`
- **Implementation**: `src/renderer.cpp`
- **Embedded Shaders**: GLSL shaders (compiled into the library)
- **Dependencies**: Automatically fetched via CMake
  - fastgltf (glTF loading)
  - GLM (mathematics)
  - OpenGL (rendering)

### What's NOT Included

The library does **not** include:
- ❌ Window management (GLFW) - you provide your own window
- ❌ OpenGL function loader (GLAD) - you provide your own loader
- ❌ MIDI playback - that's only in the example

You need to:
1. Create an OpenGL context (using GLFW, SDL, Qt, or any other library)
2. Load OpenGL functions (using GLAD, GLEW, or similar)
3. Call `renderer.init()` and `renderer.render()` in your render loop

### Required OpenGL Context

The library requires:
- **OpenGL 3.2 Core Profile** or newer
- **Active OpenGL context** before calling `renderer.init()`

Example with GLFW:
```cpp
glfwInit();
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

GLFWwindow* window = glfwCreateWindow(1920, 1080, "Piano", nullptr, nullptr);
glfwMakeContextCurrent(window);

// Load OpenGL functions (GLAD, GLEW, etc.)
gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

// NOW you can use the renderer
keys::Renderer renderer;
renderer.init(params);
```

## Minimal Example

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.21)
project(minimal_piano)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add keys library
add_subdirectory(external/keys)

# Fetch GLFW and GLAD (for window and OpenGL loading)
include(FetchContent)

FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.3.9)
FetchContent_Declare(glad GIT_REPOSITORY https://github.com/Dav1dde/glad.git GIT_TAG v0.1.36)
FetchContent_MakeAvailable(glfw glad)

add_executable(minimal_piano main.cpp)
target_link_libraries(minimal_piano PRIVATE keys glad glfw)
```

**main.cpp:**
```cpp
#include <keys/renderer.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

int main() {
    // Initialize GLFW and create window
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1920, 1080, "Piano", nullptr, nullptr);
    if (!window) return -1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Load OpenGL functions
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to load OpenGL\n";
        return -1;
    }

    // Initialize renderer
    keys::Renderer renderer;
    keys::InitParams params;
    params.gltfPath = "assets/scene.gltf";
    params.envHdrPath = "assets/env/mirrored_hall_2k.hdr";

    if (!renderer.init(params)) {
        std::cerr << "Renderer init failed\n";
        return -1;
    }

    // Trigger some notes (example)
    renderer.noteOn(60);  // Middle C
    renderer.noteOn(64);  // E
    renderer.noteOn(67);  // G

    // Render loop
    while (!glfwWindowShouldClose(window)) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        keys::FrameParams frame;
        frame.fbWidth = width;
        frame.fbHeight = height;
        frame.timeSeconds = glfwGetTime();

        renderer.render(frame);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    renderer.shutdown();
    glfwTerminate();
    return 0;
}
```

Build and run:
```bash
cmake -B build -S .
cmake --build build
./build/minimal_piano
```

## Troubleshooting

### "Cannot find keys/renderer.hpp"

Make sure you're using `add_subdirectory(path/to/keys)` in your CMakeLists.txt. The include path is set up automatically.

### "Undefined reference to keys::Renderer::init"

Make sure you're linking the library: `target_link_libraries(your_app PRIVATE keys)`

### "OpenGL functions are null pointers"

You must create an OpenGL context and load functions BEFORE calling `renderer.init()`:
```cpp
glfwMakeContextCurrent(window);
gladLoadGLLoader(...);  // ← Required!
renderer.init(params);  // ← Now safe
```

### Build fails with "C++20 required"

Ensure your compiler supports C++20:
- GCC 11+
- Clang 14+
- MSVC 2019+

Set it in your CMakeLists.txt:
```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

## Advanced Usage

### Controlling Camera

```cpp
// Use keyboard-optimized camera (default)
params.useKeyboardCamera = true;
params.useMovingCamera = false;

// Or use orbiting animated camera
params.useKeyboardCamera = false;
params.useMovingCamera = true;
```

### Adjusting Shadow Quality

```cpp
params.shadowMapSize = 2048;  // Lower quality, better performance
params.shadowMapSize = 4096;  // Default
params.shadowMapSize = 8192;  // Higher quality, worse performance
```

### Custom Assets

```cpp
params.gltfPath = "path/to/your/model.gltf";
params.envHdrPath = "path/to/your/environment.hdr";
```

The glTF model should contain piano key geometry. The library automatically maps MIDI notes 21-108 (A0-C8) to keys.

## License

Commercial use is allowed under MIT.

If you redistribute the Keys library as part of your product, make sure to:
1. Include the MIT license text from this project
2. Preserve this project's copyright notice
3. Include any required third-party notices for redistributed dependencies

A visible credit line is appreciated, but not required under MIT. The actual obligation is to retain the copyright notice and license text when redistributing the software.

Example in your notices or third-party licenses section:
```markdown
This product includes the Keys library:
- Source: https://github.com/dr-inf/piano-gl
- License: MIT
- See keys/THIRD_PARTY_LICENSES.md for dependency notices
```
