// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <memory>

namespace keys {

/// Configuration parameters for Renderer initialization
struct InitParams {
    /// Path to the glTF/GLB model file (e.g., "assets/scene.gltf")
    std::filesystem::path gltfPath{"assets/scene.gltf"};

    /// Path to HDR environment map for image-based lighting (e.g., "assets/env/mirrored_hall_2k.hdr")
    std::filesystem::path envHdrPath{"assets/env/mirrored_hall_2k.hdr"};

    /// Size of the shadow map depth texture (default: 4096x4096)
    /// Higher values improve shadow quality but increase memory usage
    int shadowMapSize = 4096;

    /// Enable keyboard-aligned camera positioning for optimal piano view
    bool useKeyboardCamera = true;

    /// Enable animated orbiting camera (incompatible with useKeyboardCamera)
    bool useMovingCamera = false;
};

/// Frame rendering parameters
struct FrameParams {
    /// Framebuffer width in pixels
    int fbWidth = 0;

    /// Framebuffer height in pixels
    int fbHeight = 0;

    /// Current time in seconds (for animations and camera movement)
    double timeSeconds = 0.0;
};

/// 3D Piano Keys Renderer with physically-based rendering (PBR) and shadow mapping
///
/// This renderer loads piano key models from glTF files and provides real-time
/// visualization with:
/// - PBR materials (metallic-roughness workflow)
/// - Dynamic shadow mapping
/// - HDR environment mapping for realistic lighting
/// - Per-key animation for MIDI note visualization
///
/// Example usage:
/// @code
///   keys::Renderer renderer;
///   keys::InitParams params;
///   params.gltfPath = "assets/piano.gltf";
///
///   if (!renderer.init(params)) {
///       // Handle initialization error
///       return;
///   }
///
///   // In render loop:
///   keys::FrameParams frame;
///   frame.fbWidth = windowWidth;
///   frame.fbHeight = windowHeight;
///   frame.timeSeconds = glfwGetTime();
///   renderer.render(frame);
///
///   // On MIDI events:
///   renderer.noteOn(60);  // Middle C
/// @endcode
class Renderer {
public:
    /// Constructs an uninitialized renderer
    /// Call init() before rendering
    Renderer();

    /// Destructor - automatically releases all GPU resources
    ~Renderer();

    /// Initializes the renderer with the given parameters
    ///
    /// This function:
    /// - Loads the glTF model and extracts geometry
    /// - Compiles and links all required shaders
    /// - Loads textures and environment maps
    /// - Sets up framebuffers for shadow mapping
    ///
    /// @param params Configuration parameters (paths, shadow quality, camera mode)
    /// @return true if initialization succeeded, false on error
    /// @note Error details are printed to stderr
    /// @note Must be called with an active OpenGL context
    bool init(const InitParams& params);

    /// Renders a single frame to the current framebuffer
    ///
    /// This function performs multiple rendering passes:
    /// 1. Shadow map generation from light's perspective
    /// 2. Skybox rendering
    /// 3. Reflection rendering (if mirror present)
    /// 4. Main scene rendering with PBR lighting
    ///
    /// @param frame Frame parameters (size and time)
    /// @note Assumes the OpenGL context is current
    /// @note Does not swap buffers - caller must handle buffer swap
    void render(const FrameParams& frame);

    /// Triggers a key press animation for the given MIDI note
    ///
    /// @param midiPitch MIDI pitch number (0-127)
    ///                  Valid piano range is typically 21 (A0) to 108 (C8)
    /// @note Out-of-range pitches are silently ignored
    void noteOn(int midiPitch);

    /// Triggers a key release animation for the given MIDI note
    ///
    /// @param midiPitch MIDI pitch number (0-127)
    /// @note Out-of-range pitches are silently ignored
    void noteOff(int midiPitch);

    /// Releases all GPU resources (textures, buffers, shaders, etc.)
    ///
    /// @note Called automatically by the destructor
    /// @note After calling shutdown(), init() must be called again before rendering
    /// @note Must be called with the same OpenGL context that was used for init()
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace keys
