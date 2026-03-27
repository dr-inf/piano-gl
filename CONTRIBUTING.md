# Contributing to this project

Thank you for considering contributing to Keys! This document provides guidelines and instructions for contributing.

## Code of Conduct

- Be respectful and constructive in discussions
- Focus on the technical merits of proposals
- Help create a welcoming environment for all contributors

## How to Contribute

### Reporting Bugs

If you find a bug, please open an issue with:

1. **Clear title** describing the problem
2. **Steps to reproduce** the issue
3. **Expected behavior** vs. actual behavior
4. **Environment details**:
   - OS and version (e.g., Ubuntu 22.04, macOS 14.2, Windows 11)
   - Compiler and version (e.g., GCC 11.4, Clang 15, MSVC 2022)
   - GPU and driver version
   - CMake version
5. **Relevant logs** (stderr output, CMake errors, etc.)

### Suggesting Features

Feature requests are welcome! Please:

1. Check existing issues to avoid duplicates
2. Describe the use case and motivation
3. Provide examples of the proposed API or behavior
4. Consider backwards compatibility

### Pull Requests

1. **Fork** the repository
2. **Create a branch** from `main` with a descriptive name:
   - `feature/your-feature-name`
   - `fix/issue-description`
   - `docs/what-you-documented`
3. **Make your changes** following the code style guidelines below
4. **Test your changes** thoroughly
5. **Commit** with clear, descriptive messages
6. **Push** to your fork
7. **Open a pull request** with:
   - Clear description of changes
   - Reference to related issues (e.g., "Fixes #42")
   - Screenshots/videos for visual changes

## Development Guidelines

### Code Style

We follow modern C++20 best practices:

#### Naming Conventions

```cpp
// Namespaces: lowercase
namespace keys {

// Classes/Structs: PascalCase
class Renderer { };
struct InitParams { };

// Functions/Methods: camelCase
void renderFrame();
bool init(const InitParams& params);

// Variables: camelCase
int shadowMapSize = 4096;
glm::vec3 camPos;

// Constants: UPPER_SNAKE_CASE
constexpr float SHADOW_BIAS = 0.00005f;
constexpr int MAX_LIGHTS = 4;

// Private members: camelCase (no prefix)
class Foo {
    int memberVar;  // Not: m_memberVar or memberVar_
};
```

#### Formatting

- **Indentation**: 4 spaces (no tabs)
- **Line length**: Soft limit of 120 characters
- **Braces**: Opening brace on same line (K&R style)
  ```cpp
  if (condition) {
      doSomething();
  }
  ```
- **Includes**: Group and order as:
  1. Corresponding header (for `.cpp` files)
  2. C++ standard library
  3. Third-party libraries
  4. Project headers

#### Best Practices

- **Use RAII** for resource management (no manual `new`/`delete`)
- **Prefer `const`** wherever possible
- **Use `std::optional`** for optional values instead of pointers
- **Document public APIs** with Doxygen-style comments:
  ```cpp
  /// Brief description
  ///
  /// Detailed description
  /// @param paramName Description
  /// @return Description of return value
  bool someFunction(int paramName);
  ```
- **Avoid magic numbers** - use named constants
- **Check bounds** before array/vector access
- **Validate OpenGL calls** in debug builds

### Building and Testing

```bash
# Clean build
rm -rf build
cmake -B build -S .
cmake --build build

# Run the example
./build/examples/keys_example/keys_example

# Test with different assets
./build/examples/keys_example/keys_example path/to/your/model.gltf
```

### Commit Messages

Follow the [Conventional Commits](https://www.conventionalcommits.org/) format:

```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, no logic change)
- `refactor`: Code refactoring
- `perf`: Performance improvements
- `test`: Adding or updating tests
- `chore`: Build process, dependencies, etc.

**Examples:**
```
feat(renderer): add support for KHR_materials_unlit

fix(midi): prevent race condition in playback thread

docs(readme): add macOS build instructions

refactor(renderer): extract shadow pass into separate function
```

## Project Structure

Understanding the codebase:

```
keys/
├── include/keys/          # Public API (stable interface)
│   └── renderer.hpp
├── src/                   # Implementation (can change)
│   ├── renderer.cpp       # Main rendering pipeline
│   ├── midi_player.cpp    # MIDI parsing and playback
│   ├── midi_player.h
│   └── keys.hpp           # Internal key mapping structures
├── shaders/               # GLSL shaders (embedded at build time)
├── examples/              # Example applications
├── cmake/                 # Build scripts
│   └── embed_shaders.py   # Shader embedding tool
└── vendor/                # Single-header dependencies
```

### Key Components

- **Renderer** (`renderer.cpp`):
  - glTF loading via fastgltf
  - OpenGL resource management
  - PBR rendering pipeline
  - Shadow mapping
  - Key animation

- **MIDI Player** (`midi_player.cpp`):
  - MIDI file parsing (Format 0)
  - Playback timing
  - Event callbacks

## Adding Features

### Adding a New glTF Extension

1. Check if fastgltf supports it
2. Extend `buildSceneGPU()` to handle the extension
3. Update shaders if needed
4. Document in README under "Known Limitations"

### Adding Shader Uniforms

1. Add uniform to shader files in `shaders/`
2. Add uniform location to appropriate program struct (e.g., `GLProgram`)
3. Retrieve location in `create*Program()` function
4. Set uniform value in `renderFrame()` or relevant function
5. Rebuild (CMake will re-embed shaders automatically)

### Improving Performance

Before optimizing:
1. **Profile first** - identify actual bottlenecks
2. Provide before/after measurements
3. Explain the optimization technique

Common bottlenecks:
- Shadow map resolution (too high)
- Texture uploads (not cached)
- Draw call count (batch where possible)
- Shader complexity (simplify if possible)

## Questions?

Feel free to open an issue for:
- Clarification on contributing guidelines
- Design decisions and architecture questions
- Feature proposals and discussions

## License

By contributing, you agree that your contributions will be licensed under the same MIT License that covers the project.

---

Thank you for contributing to Keys! 🎹
