// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <vector>

#include "keys/renderer.hpp"
#include "midi_player.h"

namespace fs = std::filesystem;

void glfwErrorCallback(int code, const char* description) {
    std::cerr << "GLFW error (" << code << "): " << description << "\n";
}

GLFWwindow* createWindowWithFallback(int width, int height, const char* title) {
    struct Hint {
        int major;
        int minor;
        int profile;   // GLFW_OPENGL_CORE_PROFILE or 0 for default
        bool forwardCompat;
    };

    constexpr std::array<Hint, 6> options{{
        {4, 6, GLFW_OPENGL_CORE_PROFILE, true},
        {4, 5, GLFW_OPENGL_CORE_PROFILE, true},
        {4, 3, GLFW_OPENGL_CORE_PROFILE, true},
        {3, 3, GLFW_OPENGL_CORE_PROFILE, true},
        {3, 2, GLFW_OPENGL_CORE_PROFILE, true},
        {0, 0, 0, false},
    }};

    for (const auto& opt : options) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_SAMPLES, 4); // enable multisampling
        if (opt.major > 0) {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, opt.major);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, opt.minor);
        }
        if (opt.profile != 0) {
            glfwWindowHint(GLFW_OPENGL_PROFILE, opt.profile);
        }
        glfwWindowHint(GLFW_STENCIL_BITS, 8);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, opt.forwardCompat ? GL_TRUE : GL_FALSE);
#endif

        GLFWwindow* win = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (win) {
            std::cout << "GLFW context created with " << (opt.major > 0 ? std::to_string(opt.major) + "." + std::to_string(opt.minor) : std::string("default")) << "\n";
            return win;
        }
    }

    return nullptr;
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  --help              Show this help message\n"
              << "  --model <path>      Path to glTF/GLB model (default: assets/scene.gltf)\n"
              << "  --camera <mode>     Camera mode: static|orbit (default: static)\n"
              << "                      static - Fixed view optimized for piano\n"
              << "                      orbit  - Animated orbiting camera\n"
              << "\nExamples:\n"
              << "  " << progName << "\n"
              << "  " << progName << " --camera orbit\n"
              << "  " << progName << " --model custom.gltf --camera orbit\n";
}

int main(int argc, char** argv) {
    fs::path gltfPath = "assets/scene.gltf";
    bool useOrbitCamera = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--model" && i + 1 < argc) {
            gltfPath = argv[++i];
        } else if (arg == "--camera" && i + 1 < argc) {
            std::string mode = argv[++i];
            if (mode == "orbit") {
                useOrbitCamera = true;
            } else if (mode == "static") {
                useOrbitCamera = false;
            } else {
                std::cerr << "Unknown camera mode: " << mode << " (use 'static' or 'orbit')\n";
                return 1;
            }
        } else if (arg[0] != '-') {
            // Legacy: first positional arg is model path
            gltfPath = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "GLFW initialization failed.\n";
        return 1;
    }

    GLFWwindow* window = createWindowWithFallback(2400, 440, "Keys Example");
    if (!window) {
        std::cerr << "Window creation failed.\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "GLAD initialization failed.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    keys::Renderer renderer;
    keys::InitParams init{};
    init.gltfPath = gltfPath;
    init.envHdrPath = "assets/env/mirrored_hall_2k.hdr";
    init.shadowMapSize = 4096;
    init.useKeyboardCamera = !useOrbitCamera;
    init.useMovingCamera = useOrbitCamera;
    if (!renderer.init(init)) {
        std::cerr << "Renderer initialization failed.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    enum class MidiEventType { NoteOn, NoteOff, Sustain };
    struct PendingMidiEvent {
        MidiEventType type = MidiEventType::NoteOn;
        int pitch = 0;
        int velocity = 0;
        bool sustainDown = false;
    };
    std::mutex midiMutex;
    std::vector<PendingMidiEvent> midiQueue;

    midi::MidiPlaybackOptions midiOpts;
    midiOpts.path = "assets/demo.mid";
    midiOpts.loop = true;
    midi::launchMidiPlayback(midiOpts,
        [&midiMutex, &midiQueue](int pitch, int velocity){
            std::lock_guard<std::mutex> lock(midiMutex);
            midiQueue.push_back(PendingMidiEvent{MidiEventType::NoteOn, pitch, velocity, false});
        },
        [&midiMutex, &midiQueue](int pitch, int velocity){
            std::lock_guard<std::mutex> lock(midiMutex);
            midiQueue.push_back(PendingMidiEvent{MidiEventType::NoteOff, pitch, velocity, false});
        },
        [&midiMutex, &midiQueue](bool down){
            std::lock_guard<std::mutex> lock(midiMutex);
            midiQueue.push_back(PendingMidiEvent{MidiEventType::Sustain, 0, 0, down});
        });

    while (!glfwWindowShouldClose(window)) {
        std::vector<PendingMidiEvent> events;
        {
            std::lock_guard<std::mutex> lock(midiMutex);
            events.swap(midiQueue);
        }
        for (const auto& ev : events) {
            switch (ev.type) {
                case MidiEventType::NoteOn:
                    renderer.noteOn(ev.pitch);
                    break;
                case MidiEventType::NoteOff:
                    renderer.noteOff(ev.pitch);
                    break;
                case MidiEventType::Sustain:
                    // Sustain could be used for visual effects if needed
                    break;
            }
        }

        double now = glfwGetTime();
        int fbWidth = 0, fbHeight = 0;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        renderer.render(keys::FrameParams{fbWidth, fbHeight, now});
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
