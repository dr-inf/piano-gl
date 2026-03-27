// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/glcorearb.h>
#include <GL/glext.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

#include "keys/renderer.hpp"
#include "keys.hpp"
#include "keys_shaders.hpp"
#include "log.hpp"

namespace fs = std::filesystem;

namespace keys {
namespace {

// Rendering constants
constexpr float CAMERA_DISTANCE_SCALE = 0.6f;        // Camera distance from keyboard (multiplier)
constexpr float CAMERA_HEIGHT_RATIO = 0.35f;         // Camera height relative to keyboard span
constexpr float KEYBOARD_SPATIAL_PADDING = 1.05f;    // Padding around keyboard for FOV calculation
constexpr float SHADOW_BIAS = 0.00005f;              // Depth bias to prevent shadow acne
constexpr float LIGHT_INTENSITY = 5.0f;              // Directional light intensity
constexpr float ENVIRONMENT_INTENSITY = 0.5f;        // IBL environment map intensity

struct TextureCache {
    std::unordered_map<std::size_t, GLuint> imageTextures; // fastgltf image index -> GL texture
    GLuint white = 0;
    GLuint mrrDefault = 0;   // occlusion=1, roughness=1, metallic=0
    GLuint normalDefault = 0;
    GLuint emissiveDefault = 0;
    GLuint envMap = 0;
};

struct MaterialGPU {
    GLuint baseColorTex = 0;
    GLuint mrTex = 0;
    GLuint normalTex = 0;
    GLuint emissiveTex = 0;
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    glm::vec3 emissiveFactor{0.0f};
    bool doubleSided = false;
    bool unlit = false;
};

struct PrimitiveGPU {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
    GLenum mode = GL_TRIANGLES;
    MaterialGPU material;
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
};

struct MeshGPU {
    std::vector<PrimitiveGPU> primitives;
};

struct DrawCommand {
    glm::mat4 model{1.0f};
    std::size_t meshIndex = 0;
};

struct SceneCamera {
    glm::mat4 world{1.0f};   // camera transform (world space)
    glm::mat4 view{1.0f};
    float yfov = glm::radians(50.0f);
    float znear = 0.01f;
    float zfar = 1000.0f;
    std::optional<float> aspect; // if set, use this; otherwise framebuffer aspect
    bool valid = false;
};

struct InstanceInfo {
    GLuint vbo = 0;
    GLuint rotationVbo = 0;
    GLintptr rotationOffset = 0;
    GLsizei count = 0;
    GLsizei strideBytes = 0;
};

enum class EaseType {
    Back,
    Elastic
};

struct KeyRotationBuffer {
    GLuint vbo = 0;
    std::vector<float> data;
};

struct KeyAnimation {
    int midiPitch = -1;
    float start = 0.0f;
    float target = 0.0f;
    float duration = 0.0f;
    float elapsed = 0.0f;
    EaseType ease = EaseType::Back;
};

struct KeyMapping {
    bool isWhite = true;
    int index = 0;
};

struct GLProgram {
    GLuint id = 0;
    GLint locModel = -1;
    GLint locView = -1;
    GLint locProj = -1;
    GLint locLightVP = -1;
    GLint locNormalMat = -1;
    GLint locCamPos = -1;
    GLint locBaseColor = -1;
    GLint locMetallic = -1;
    GLint locRoughness = -1;
    GLint locEmissive = -1;
    GLint locUnlit = -1;
    GLint locDarken = -1;
    GLint locEnvMap = -1;
    GLint locEnvIntensity = -1;
    GLint locEnvRot = -1;
    GLint locShadowMap = -1;
    GLint locShadowBias = -1;
    GLint locShadowDebug = -1;
    GLint locGroundNormal = -1;
    GLint locGroundPoint = -1;
    GLint locGroundParams = -1; // x=radius, y=intensity
};

struct SkyboxProgram {
    GLuint id = 0;
    GLint locView = -1;
    GLint locProj = -1;
    GLint locCubemap = -1;
    GLint locEnvRot = -1;
};
struct CaptureProgram {
    GLuint id = 0;
    GLint locView = -1;
    GLint locProj = -1;
    GLint locEquirect = -1;
};
struct ShadowProgram {
    GLuint id = 0;
    GLint locModel = -1;
    GLint locLightVP = -1;
};
struct ShadowMap {
    GLuint fbo = 0;
    GLuint depthTex = 0;
    int size = 4096;
    glm::mat4 lightVP{1.0f};
};

struct LightViewData {
    glm::mat4 vp{1.0f};
    glm::vec3 eye{0.0f};
    glm::vec3 dir{0.0f};
    float orthoRadius = 1.0f;
};

struct SceneGPU {
    std::vector<MeshGPU> meshes;
    std::vector<DrawCommand> drawCommands;
    glm::vec3 sceneCenter{0.0f};
    float sceneRadius = 1.0f;
    glm::vec3 sceneMin{0.0f};
    glm::vec3 sceneMax{0.0f};
    SceneCamera camera{};
    std::optional<std::size_t> backPlaneMeshIndex;
    std::optional<glm::mat4> backPlaneModel;
    std::optional<glm::vec3> backPlaneNormalWS;
    std::optional<glm::vec3> backPlanePointWS;
    std::optional<std::size_t> baseMeshIndex;
    GLuint envMap = 0;
    std::unordered_map<std::size_t, InstanceInfo> instancedMeshes; // meshIndex -> instance data
    KeyRotationBuffer keyRotations;
    std::array<std::optional<KeyMapping>, 128> pitchToKey{};
    bool keyRotationsDirty = false;
    std::vector<KeyAnimation> animations;
    double lastTime = 0.0;
};

struct KeyboardBounds {
    float centerZ = 0.0f;
    float spanZ = 1.0f;
};

struct LoadedGltf {
    fastgltf::GltfDataBuffer buffer; // keep raw glTF bytes alive while the asset references them
    fastgltf::Asset asset;
};

// Instance helpers
InstanceInfo createWhiteKeyInstanceBuffer(GLuint rotationVbo, GLintptr rotationOffsetBytes);
InstanceInfo createBlackKeyInstanceBuffer(GLuint rotationVbo, GLintptr rotationOffsetBytes);
void bindInstanceAttributes(GLuint vao, const InstanceInfo& inst);
KeyRotationBuffer createKeyRotationBuffer();
std::array<std::optional<KeyMapping>, 128> buildPitchToKeyMapping();
inline int rotationIndexForKey(bool isWhite, int keyIndex);
void setKeyRotation(SceneGPU& scene, int midiPitch, float radians);
void uploadKeyRotations(SceneGPU& scene);
float getKeyRotation(const SceneGPU& scene, int midiPitch);
void midiNoteOn(SceneGPU& scene, int midiPitch);
void midiNoteOff(SceneGPU& scene, int midiPitch);
void updateAnimations(SceneGPU& scene, float dt);
KeyboardBounds computeKeyboardBounds();


GLuint compileShader(GLenum type, std::string_view src, std::string_view name) {
    GLuint shader = glCreateShader(type);
    const char* csrc = src.data();
    GLint len = static_cast<GLint>(src.size());
    glShaderSource(shader, 1, &csrc, &len);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(logLen, '\0');
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        throw std::runtime_error("Shader Compile Failed (" + std::string(name) + "): " + log);
    }
    return shader;
}

GLuint createProgram(std::string_view vertSrc, std::string_view fragSrc, std::string_view vertName, std::string_view fragName) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertSrc, vertName);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc, fragName);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint status = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(logLen, '\0');
        glGetProgramInfoLog(prog, logLen, nullptr, log.data());
        throw std::runtime_error("Shader Link Failed: " + log);
    }
    return prog;
}

GLProgram createPbrProgram(std::string_view vertSrc, std::string_view fragSrc) {
    GLProgram p{};
    p.id = createProgram(vertSrc, fragSrc, "pbr.vert", "pbr.frag");
    p.locModel = glGetUniformLocation(p.id, "uModel");
    p.locView = glGetUniformLocation(p.id, "uView");
    p.locProj = glGetUniformLocation(p.id, "uProj");
    p.locLightVP = glGetUniformLocation(p.id, "uLightVP");
    p.locNormalMat = glGetUniformLocation(p.id, "uNormalMat");
    p.locCamPos = glGetUniformLocation(p.id, "uCamPos");
    p.locBaseColor = glGetUniformLocation(p.id, "uBaseColorFactor");
    p.locMetallic = glGetUniformLocation(p.id, "uMetallicFactor");
    p.locRoughness = glGetUniformLocation(p.id, "uRoughnessFactor");
    p.locEmissive = glGetUniformLocation(p.id, "uEmissiveFactor");
    p.locUnlit = glGetUniformLocation(p.id, "uUnlit");
    p.locDarken = glGetUniformLocation(p.id, "uDarkenFactor");
    p.locEnvMap = glGetUniformLocation(p.id, "uEnvMap");
    p.locEnvIntensity = glGetUniformLocation(p.id, "uEnvIntensity");
    p.locEnvRot = glGetUniformLocation(p.id, "uEnvRot");
    p.locShadowMap = glGetUniformLocation(p.id, "uShadowMap");
    p.locShadowBias = glGetUniformLocation(p.id, "uShadowBias");
    p.locShadowDebug = glGetUniformLocation(p.id, "uShadowDebug");
    p.locGroundNormal = glGetUniformLocation(p.id, "uGroundNormal");
    p.locGroundPoint = glGetUniformLocation(p.id, "uGroundPoint");
    p.locGroundParams = glGetUniformLocation(p.id, "uGroundParams");
    return p;
}

SkyboxProgram createSkyboxProgram(std::string_view vertSrc, std::string_view fragSrc) {
    SkyboxProgram s{};
    s.id = createProgram(vertSrc, fragSrc, "skybox.vert", "skybox.frag");
    s.locView = glGetUniformLocation(s.id, "uView");
    s.locProj = glGetUniformLocation(s.id, "uProj");
    s.locCubemap = glGetUniformLocation(s.id, "uCubemap");
    s.locEnvRot = glGetUniformLocation(s.id, "uEnvRot");
    return s;
}

CaptureProgram createCaptureProgram(std::string_view vertSrc, std::string_view fragSrc) {
    CaptureProgram c{};
    c.id = createProgram(vertSrc, fragSrc, "equirect_to_cube.vert", "equirect_to_cube.frag");
    c.locView = glGetUniformLocation(c.id, "uView");
    c.locProj = glGetUniformLocation(c.id, "uProj");
    c.locEquirect = glGetUniformLocation(c.id, "uEquirect");
    return c;
}

ShadowProgram createShadowProgram(std::string_view vertSrc, std::string_view fragSrc) {
    ShadowProgram s{};
    s.id = createProgram(vertSrc, fragSrc, "shadow_depth.vert", "shadow_depth.frag");
    s.locModel = glGetUniformLocation(s.id, "uModel");
    s.locLightVP = glGetUniformLocation(s.id, "uLightVP");
    return s;
}

InstanceInfo createWhiteKeyInstanceBuffer(GLuint rotationVbo, GLintptr rotationOffsetBytes) {
    InstanceInfo info{};
    glGenBuffers(1, &info.vbo);
    info.count = static_cast<GLsizei>(kWhiteKeys.size());
    info.strideBytes = static_cast<GLsizei>(3 * sizeof(float));
    info.rotationVbo = rotationVbo;
    info.rotationOffset = rotationOffsetBytes;

    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter(-0.4f, 0.4f);

    std::vector<float> data;
    data.reserve(kWhiteKeys.size() * 3);
    for (const auto& k : kWhiteKeys) {
        data.push_back(k.leftCut);
        data.push_back(k.rightCut);
        data.push_back(k.center + jitter(rng));
    }
    glBindBuffer(GL_ARRAY_BUFFER, info.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);

    return info;
}

InstanceInfo createBlackKeyInstanceBuffer(GLuint rotationVbo, GLintptr rotationOffsetBytes) {
    InstanceInfo info{};
    glGenBuffers(1, &info.vbo);
    info.count = static_cast<GLsizei>(kBlackKeys.size());
    info.strideBytes = static_cast<GLsizei>(1 * sizeof(float));
    info.rotationVbo = rotationVbo;
    info.rotationOffset = rotationOffsetBytes;
    glBindBuffer(GL_ARRAY_BUFFER, info.vbo);
    glBufferData(GL_ARRAY_BUFFER, kBlackKeys.size() * sizeof(float), kBlackKeys.data(), GL_STATIC_DRAW);
    return info;
}

void bindInstanceAttributes(GLuint vao, const InstanceInfo& inst) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, inst.vbo);
    if (inst.strideBytes >= static_cast<GLsizei>(3 * sizeof(float))) {
        // White keys: vec2 (cut widths) + float (center)
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, inst.strideBytes, reinterpret_cast<void*>(0));
        glVertexAttribDivisor(5, 1);
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, inst.strideBytes, reinterpret_cast<void*>(2 * sizeof(float)));
        glVertexAttribDivisor(6, 1);
    } else {
        // Black keys: only center, keep cut width disabled/zero.
        glDisableVertexAttribArray(5);
        glVertexAttribDivisor(5, 0);
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, inst.strideBytes, reinterpret_cast<void*>(0));
        glVertexAttribDivisor(6, 1);
    }
    if (inst.rotationVbo != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, inst.rotationVbo);
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(float), reinterpret_cast<void*>(inst.rotationOffset));
        glVertexAttribDivisor(7, 1);
    } else {
        glDisableVertexAttribArray(7);
        glVertexAttribDivisor(7, 0);
    }
}

KeyRotationBuffer createKeyRotationBuffer() {
    KeyRotationBuffer buf{};
    buf.data.assign(kWhiteKeys.size() + kBlackKeys.size(), 0.0f);    
    glGenBuffers(1, &buf.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, buf.vbo);
    glBufferData(GL_ARRAY_BUFFER, buf.data.size() * sizeof(float), buf.data.data(), GL_DYNAMIC_DRAW);
    return buf;
}

std::array<std::optional<KeyMapping>, 128> buildPitchToKeyMapping() {
    std::array<std::optional<KeyMapping>, 128> map{};
    int whiteIdx = 0;
    int blackIdx = 0;
    constexpr int kFirstMidi = 21;  // A0
    constexpr int kLastMidi = kFirstMidi + 87; // inclusive, 88 keys
    for (int pitch = kFirstMidi; pitch <= kLastMidi && pitch < static_cast<int>(map.size()); ++pitch) {
        int mod = pitch % 12;
        bool isBlack = (mod == 1 || mod == 3 || mod == 6 || mod == 8 || mod == 10);
        if (isBlack) {
            if (blackIdx < static_cast<int>(kBlackKeys.size())) {
                map[pitch] = KeyMapping{false, blackIdx++};
            }
        } else {
            if (whiteIdx < static_cast<int>(kWhiteKeys.size())) {
                map[pitch] = KeyMapping{true, whiteIdx++};
            }
        }
    }
    if (whiteIdx != static_cast<int>(kWhiteKeys.size()) || blackIdx != static_cast<int>(kBlackKeys.size())) {
        log::warning(log::format("[KeyMap] Warning: mismatched key counts. white=", whiteIdx, "/", kWhiteKeys.size(),
                  " black=", blackIdx, "/", kBlackKeys.size()));
    }
    return map;
}

inline int rotationIndexForKey(bool isWhite, int keyIndex) {
    return isWhite ? keyIndex : static_cast<int>(kWhiteKeys.size()) + keyIndex;
}

void setKeyRotation(SceneGPU& scene, int midiPitch, float radians) {
    if (midiPitch < 0 || midiPitch >= static_cast<int>(scene.pitchToKey.size())) return;
    const auto& ref = scene.pitchToKey[midiPitch];
    if (!ref) return;
    int idx = rotationIndexForKey(ref->isWhite, ref->index);
    if (idx < 0 || idx >= static_cast<int>(scene.keyRotations.data.size())) return;
    scene.keyRotations.data[static_cast<std::size_t>(idx)] = radians;
    scene.keyRotationsDirty = true;
}

void uploadKeyRotations(SceneGPU& scene) {
    if (scene.keyRotations.vbo == 0 || scene.keyRotations.data.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, scene.keyRotations.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, scene.keyRotations.data.size() * sizeof(float), scene.keyRotations.data.data());
    scene.keyRotationsDirty = false;
}

float easeOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float inv = t - 1.0f;
    return 1.0f + c3 * inv * inv * inv + c1 * inv * inv;
}

float easeOutElastic(float t) {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    const float c4 = (2.0f * 3.14159265f) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

float getKeyRotation(const SceneGPU& scene, int midiPitch) {
    if (midiPitch < 0 || midiPitch >= static_cast<int>(scene.pitchToKey.size())) return 0.0f;
    const auto& ref = scene.pitchToKey[midiPitch];
    if (!ref) return 0.0f;
    int idx = rotationIndexForKey(ref->isWhite, ref->index);
    if (idx < 0 || idx >= static_cast<int>(scene.keyRotations.data.size())) return 0.0f;
    return scene.keyRotations.data[static_cast<std::size_t>(idx)];
}

KeyboardBounds computeKeyboardBounds() {
    float minCenter = std::numeric_limits<float>::max();
    float maxCenter = std::numeric_limits<float>::lowest();
    for (const auto& k : kWhiteKeys) {
        minCenter = std::min(minCenter, k.center);
        maxCenter = std::max(maxCenter, k.center);
    }
    for (float c : kBlackKeys) {
        minCenter = std::min(minCenter, c);
        maxCenter = std::max(maxCenter, c);
    }
    KeyboardBounds b{};
    float center = (minCenter + maxCenter) * 0.5f;
    b.centerZ = -0.01f * center;
    b.spanZ = 0.01f * (maxCenter - minCenter) + 0.5f; // small padding
    return b;
}

void startAnimation(SceneGPU& scene, int midiPitch, float target, float duration, EaseType ease) {
    if (midiPitch < 0 || midiPitch >= static_cast<int>(scene.pitchToKey.size())) return;
    const auto& ref = scene.pitchToKey[midiPitch];
    if (!ref) return;
    // Remove existing animation for this pitch.
    scene.animations.erase(std::remove_if(scene.animations.begin(), scene.animations.end(), [&](const KeyAnimation& a) {
        return a.midiPitch == midiPitch;
    }), scene.animations.end());

    KeyAnimation anim{};
    anim.midiPitch = midiPitch;
    anim.start = getKeyRotation(scene, midiPitch);
    anim.target = target;
    anim.duration = duration;
    anim.elapsed = 0.0f;
    anim.ease = ease;
    scene.animations.push_back(anim);
}

void midiNoteOn(SceneGPU& scene, int midiPitch) {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter(-0.1f, 0.1f);
    float pressAngle = glm::radians(1.65f + jitter(rng));
    constexpr float pressDuration = 0.12f;
    startAnimation(scene, midiPitch, pressAngle, pressDuration, EaseType::Back);
}

void midiNoteOff(SceneGPU& scene, int midiPitch) {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter(-0.05f, 0.05f);
    float releaseAngle = glm::radians(jitter(rng));
    constexpr float releaseDuration = 0.6f;
    startAnimation(scene, midiPitch, releaseAngle, releaseDuration, EaseType::Elastic);
}

void updateAnimations(SceneGPU& scene, float dt) {
    if (scene.animations.empty()) return;
    for (auto& anim : scene.animations) {
        anim.elapsed += dt;
        float t = anim.duration > 0.0f ? glm::clamp(anim.elapsed / anim.duration, 0.0f, 1.0f) : 1.0f;
        float eased = 0.0f;
        if (anim.ease == EaseType::Back) {
            eased = easeOutBack(t);
        } else {
            eased = easeOutElastic(t);
        }
        float value = anim.start + (anim.target - anim.start) * eased;
        setKeyRotation(scene, anim.midiPitch, value);
    }
    scene.animations.erase(std::remove_if(scene.animations.begin(), scene.animations.end(), [](const KeyAnimation& a) {
        return a.elapsed >= a.duration;
    }), scene.animations.end());
}

GLuint createTexture2D(int width, int height, GLint internalFormat, GLenum format, const void* data, bool generateMips = true) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, generateMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    if (generateMips) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    return tex;
}

GLuint createFallbackTexture(glm::vec4 color, bool srgb = false) {
    std::array<unsigned char, 4> px{
        static_cast<unsigned char>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color.a, 0.0f, 1.0f) * 255.0f)};

    GLint internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    return createTexture2D(1, 1, internalFormat, GL_RGBA, px.data(), false);
}

GLuint createFallbackCubemap(glm::vec3 color) {
    std::array<unsigned char, 3> px{
        static_cast<unsigned char>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f)};
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    for (int face = 0; face < 6; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_SRGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return tex;
}

GLuint loadEquirectangularAsCubemap(const std::string& hdrPath, int size, GLuint skyboxVAO, const CaptureProgram& captureProg) {
    stbi_set_flip_vertically_on_load(false);
    int w = 0, h = 0, comp = 0;
    float* data = stbi_loadf(hdrPath.c_str(), &w, &h, &comp, 3);
    if (!data) {
        log::error(log::format("Equirect HDR load failed: ", hdrPath));
        return 0;
    }
    GLuint hdrTex = 0;
    glGenTextures(1, &hdrTex);
    glBindTexture(GL_TEXTURE_2D, hdrTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);

    GLuint cubeTex = 0;
    glGenTextures(1, &cubeTex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTex);
    for (int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, size, size, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    GLuint fbo = 0, rbo = 0;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

    // Check framebuffer completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        log::error(log::format("Framebuffer incomplete for equirect conversion: 0x", std::hex, status, std::dec));
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &hdrTex);
        glDeleteTextures(1, &cubeTex);
        glDeleteRenderbuffers(1, &rbo);
        glDeleteFramebuffers(1, &fbo);
        return 0;
    }

    glm::mat4 captureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[6] = {
        glm::lookAt(glm::vec3(0,0,0), glm::vec3(1,0,0), glm::vec3(0,-1,0)),
        glm::lookAt(glm::vec3(0,0,0), glm::vec3(-1,0,0), glm::vec3(0,-1,0)),
        glm::lookAt(glm::vec3(0,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1)),
        glm::lookAt(glm::vec3(0,0,0), glm::vec3(0,-1,0), glm::vec3(0,0,-1)),
        glm::lookAt(glm::vec3(0,0,0), glm::vec3(0,0,1), glm::vec3(0,-1,0)),
        glm::lookAt(glm::vec3(0,0,0), glm::vec3(0,0,-1), glm::vec3(0,-1,0)),
    };

    glUseProgram(captureProg.id);
    glUniform1i(captureProg.locEquirect, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTex);
    glBindVertexArray(skyboxVAO);
    glViewport(0, 0, size, size);
    for (int i = 0; i < 6; ++i) {
        glUniformMatrix4fv(captureProg.locView, 1, GL_FALSE, glm::value_ptr(captureViews[i]));
        glUniformMatrix4fv(captureProg.locProj, 1, GL_FALSE, glm::value_ptr(captureProj));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubeTex, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteTextures(1, &hdrTex);
    glDeleteRenderbuffers(1, &rbo);
    glDeleteFramebuffers(1, &fbo);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTex);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    log::info(log::format("Equirect -> cubemap: ", hdrPath, " -> ", size, "x", size));
    return cubeTex;
}

std::optional<std::vector<std::byte>> getImageBytes(const fastgltf::Asset& asset, const fastgltf::Image& image, const fs::path& basePath, fastgltf::DefaultBufferDataAdapter& adapter) {
    return std::visit(fastgltf::visitor{
        [&](const fastgltf::sources::Array& array) -> std::optional<std::vector<std::byte>> {
            std::vector<std::byte> out(array.bytes.size());
            std::memcpy(out.data(), array.bytes.data(), array.bytes.size());
            return out;
        },
        [&](const fastgltf::sources::ByteView& view) -> std::optional<std::vector<std::byte>> {
            std::vector<std::byte> out(view.bytes.size());
            std::memcpy(out.data(), view.bytes.data(), view.bytes.size());
            return out;
        },
        [&](const fastgltf::sources::BufferView& viewRef) -> std::optional<std::vector<std::byte>> {
            auto view = adapter(asset, viewRef.bufferViewIndex);
            std::vector<std::byte> out(view.size());
            std::memcpy(out.data(), view.data(), view.size());
            return out;
        },
        [&](const fastgltf::sources::URI& uri) -> std::optional<std::vector<std::byte>> {
            // Only handle local file paths here.
            if (!uri.uri.isLocalPath()) {
                log::error(log::format("Image URI not a local path: ", uri.uri.string()));
                return std::nullopt;
            }
            auto path = basePath / std::string(uri.uri.path().begin(), uri.uri.path().end());
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                log::error(log::format("Image URI not found: ", path));
                return std::nullopt;
            }
            std::vector<char> tmp((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::vector<std::byte> bytes(tmp.size());
            std::memcpy(bytes.data(), tmp.data(), tmp.size());
            return bytes;
        },
        [](auto&) -> std::optional<std::vector<std::byte>> {
            return std::nullopt;
        }},
        image.data);
}

GLuint uploadImageTexture(const fastgltf::Asset& asset, std::size_t imageIndex, const fs::path& basePath, TextureCache& cache, bool srgb, fastgltf::DefaultBufferDataAdapter& adapter) {
    if (cache.imageTextures.contains(imageIndex)) {
        return cache.imageTextures[imageIndex];
    }

    // Validate image index bounds
    if (imageIndex >= asset.images.size()) {
        log::error(log::format("Image index ", imageIndex, " out of bounds (max: ", asset.images.size(), ")"));
        return cache.white;
    }

    const auto& image = asset.images[imageIndex];
    auto bytesOpt = getImageBytes(asset, image, basePath, adapter);
    int w = 1, h = 1, comp = 4;
    stbi_uc* data = nullptr;
    if (bytesOpt) {
        data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytesOpt->data()),
                                     static_cast<int>(bytesOpt->size()), &w, &h, &comp, 0);
    }
    if (!data) {
        log::warning(log::format("Warning: failed to load texture ", imageIndex, ", fallback."));
        return cache.white;
    }

    GLenum format = GL_RGBA;
    GLint internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    if (comp == 1) { format = GL_RED; internalFormat = GL_R8; }
    else if (comp == 3) { format = GL_RGB; internalFormat = srgb ? GL_SRGB8 : GL_RGB8; }

    GLuint tex = createTexture2D(w, h, internalFormat, format, data, true);
    stbi_image_free(data);
    cache.imageTextures[imageIndex] = tex;
    return tex;
}

GLuint resolveTexture(const fastgltf::Asset& asset, const fastgltf::TextureInfo& texInfo, const fs::path& basePath, TextureCache& cache, bool srgb, fastgltf::DefaultBufferDataAdapter& adapter, GLuint fallback) {
    if (texInfo.textureIndex >= asset.textures.size()) {
        return fallback;
    }
    const auto& tex = asset.textures[texInfo.textureIndex];
    if (!tex.imageIndex) {
        return fallback;
    }
    return uploadImageTexture(asset, *tex.imageIndex, basePath, cache, srgb, adapter);
}

glm::mat4 toMat4(const fastgltf::TRS& trs) {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(trs.translation[0], trs.translation[1], trs.translation[2]));
    glm::quat q(trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]); // wxyz
    glm::mat4 R = glm::toMat4(q);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(trs.scale[0], trs.scale[1], trs.scale[2]));
    return T * R * S;
}

glm::mat4 nodeTransform(const fastgltf::Node& node) {
    if (std::holds_alternative<fastgltf::TRS>(node.transform)) {
        return toMat4(std::get<fastgltf::TRS>(node.transform));
    }
    const auto& m = std::get<fastgltf::math::fmat4x4>(node.transform);
    // fastgltf uses column-major matrix layout
    return glm::make_mat4(m.data());
}

PrimitiveGPU uploadPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& prim, const fs::path& basePath, TextureCache& cache, fastgltf::DefaultBufferDataAdapter& adapter) {
    PrimitiveGPU gpuPrim{};

    std::ostringstream attrLog;
    attrLog << "Prim attributes: ";
    for (const auto& attr : prim.attributes) {
        attrLog << attr.name << " ";
    }

    auto posIt = prim.findAttribute("POSITION");
    if (posIt == prim.attributes.end()) {
        throw std::runtime_error("Primitive ohne POSITION Attribut.");
    }
    const auto& posAcc = asset.accessors[posIt->accessorIndex];

    std::vector<glm::vec3> positions;
    positions.reserve(posAcc.count);
    fastgltf::iterateAccessor<fastgltf::math::fvec3>(asset, posAcc, [&](auto v) {
        positions.emplace_back(v[0], v[1], v[2]);
    }, adapter);

    auto normIt = prim.findAttribute("NORMAL");
    std::vector<glm::vec3> normals;
    if (normIt != prim.attributes.end()) {
        const auto& normAcc = asset.accessors[normIt->accessorIndex];
        normals.reserve(normAcc.count);
        fastgltf::iterateAccessor<fastgltf::math::fvec3>(asset, normAcc, [&](auto v) {
            normals.emplace_back(v[0], v[1], v[2]);
        }, adapter);
    } else {
        normals.assign(positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    auto uvIt = prim.findAttribute("TEXCOORD_0");
    std::vector<glm::vec2> uvs;
    if (uvIt != prim.attributes.end()) {
        const auto& uvAcc = asset.accessors[uvIt->accessorIndex];
        uvs.reserve(uvAcc.count);
        fastgltf::iterateAccessor<fastgltf::math::fvec2>(asset, uvAcc, [&](auto v) {
            uvs.emplace_back(v[0], v[1]);
        }, adapter);
    } else {
        uvs.assign(positions.size(), glm::vec2(0.0f));
    }

    auto tanIt = prim.findAttribute("TANGENT");
    std::vector<glm::vec4> tangents;
    if (tanIt != prim.attributes.end()) {
        const auto& tanAcc = asset.accessors[tanIt->accessorIndex];
        tangents.reserve(tanAcc.count);
        fastgltf::iterateAccessor<fastgltf::math::fvec4>(asset, tanAcc, [&](auto v) {
            tangents.emplace_back(v[0], v[1], v[2], v[3]);
        }, adapter);
    } else {
        tangents.assign(positions.size(), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    }

    // Custom attribute _CUT (scalar expected)
    std::vector<float> cutValues(positions.size(), 0.0f);
    if (auto cutIt = prim.findAttribute("_CUT"); cutIt != prim.attributes.end()) {
        const auto& cutAcc = asset.accessors[cutIt->accessorIndex];
        if (cutAcc.type != fastgltf::AccessorType::Scalar) {
            log::warning(log::format("Warning: _CUT accessor is not a Scalar (Type=", static_cast<int>(cutAcc.type), ")"));
        } else {
            std::size_t idx = 0;
            fastgltf::iterateAccessor<float>(asset, cutAcc, [&](auto v) {
                if (idx < cutValues.size()) {
                    cutValues[idx] = v;
                }
                ++idx;
            }, adapter);
            if (cutAcc.count != positions.size()) {
                log::warning(log::format("Warning: _CUT count (", cutAcc.count, ") != positions count (", positions.size(), ")"));
            }
        }
    }

    std::vector<std::uint32_t> indices;
    if (prim.indicesAccessor.has_value()) {
        const auto& idxAcc = asset.accessors[*prim.indicesAccessor];
        indices.reserve(idxAcc.count);
        fastgltf::iterateAccessor<std::uint32_t>(asset, idxAcc, [&](auto v) {
            indices.push_back(static_cast<std::uint32_t>(v));
        }, adapter);
    } else {
        indices.resize(positions.size());
        std::iota(indices.begin(), indices.end(), 0);
    }

    // Interleave position/normal/uv/tangent/cut
    std::vector<float> vertexData;
    vertexData.reserve(positions.size() * (3 + 3 + 2 + 4 + 1));
    gpuPrim.aabbMin = glm::vec3(std::numeric_limits<float>::max());
    gpuPrim.aabbMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto& p = positions[i];
        const auto& n = normals[i];
        const auto& uv = uvs[i];
        const auto& t = tangents[i];
        float cut = cutValues[i];
        gpuPrim.aabbMin = glm::min(gpuPrim.aabbMin, p);
        gpuPrim.aabbMax = glm::max(gpuPrim.aabbMax, p);
        vertexData.insert(vertexData.end(), {p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y, t.x, t.y, t.z, t.w, cut});
    }

    glGenVertexArrays(1, &gpuPrim.vao);
    glGenBuffers(1, &gpuPrim.vbo);
    glGenBuffers(1, &gpuPrim.ebo);

    glBindVertexArray(gpuPrim.vao);

    glBindBuffer(GL_ARRAY_BUFFER, gpuPrim.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpuPrim.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(std::uint32_t), indices.data(), GL_STATIC_DRAW);

    GLsizei stride = static_cast<GLsizei>((3 + 3 + 2 + 4 + 1) * sizeof(float));
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8 * sizeof(float)));
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(12 * sizeof(float)));

    gpuPrim.indexCount = static_cast<GLsizei>(indices.size());
    switch (prim.type) {
        case fastgltf::PrimitiveType::Points: gpuPrim.mode = GL_POINTS; break;
        case fastgltf::PrimitiveType::Lines: gpuPrim.mode = GL_LINES; break;
        case fastgltf::PrimitiveType::LineLoop: gpuPrim.mode = GL_LINE_LOOP; break;
        case fastgltf::PrimitiveType::LineStrip: gpuPrim.mode = GL_LINE_STRIP; break;
        case fastgltf::PrimitiveType::TriangleStrip: gpuPrim.mode = GL_TRIANGLE_STRIP; break;
        case fastgltf::PrimitiveType::TriangleFan: gpuPrim.mode = GL_TRIANGLE_FAN; break;
        case fastgltf::PrimitiveType::Triangles:
        default: gpuPrim.mode = GL_TRIANGLES; break;
    }

    // Material handling
    const fastgltf::Material* matPtr = nullptr;
    if (!asset.materials.empty()) {
        if (prim.materialIndex && *prim.materialIndex < asset.materials.size()) {
            matPtr = &asset.materials[*prim.materialIndex];
        } else {
            matPtr = &asset.materials.front();
        }
    }

    MaterialGPU mgpu{};
    if (matPtr) {
        const auto& mat = *matPtr;
        mgpu.baseColorFactor = glm::vec4(mat.pbrData.baseColorFactor[0], mat.pbrData.baseColorFactor[1], mat.pbrData.baseColorFactor[2], mat.pbrData.baseColorFactor[3]);
        mgpu.metallicFactor = static_cast<float>(mat.pbrData.metallicFactor);
        mgpu.roughnessFactor = static_cast<float>(mat.pbrData.roughnessFactor);
        mgpu.emissiveFactor = glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]) * static_cast<float>(mat.emissiveStrength);
        mgpu.doubleSided = mat.doubleSided;
        mgpu.unlit = mat.unlit;

        mgpu.baseColorTex = mat.pbrData.baseColorTexture ? resolveTexture(asset, *mat.pbrData.baseColorTexture, basePath, cache, true, adapter, cache.white) : cache.white;
        mgpu.mrTex = mat.pbrData.metallicRoughnessTexture ? resolveTexture(asset, *mat.pbrData.metallicRoughnessTexture, basePath, cache, false, adapter, cache.mrrDefault) : cache.mrrDefault;
        mgpu.normalTex = mat.normalTexture ? resolveTexture(asset, *mat.normalTexture, basePath, cache, false, adapter, cache.normalDefault) : cache.normalDefault;
        mgpu.emissiveTex = mat.emissiveTexture ? resolveTexture(asset, *mat.emissiveTexture, basePath, cache, true, adapter, cache.emissiveDefault) : cache.emissiveDefault;

        log::info(log::format("Material: baseColorFactor=[", mgpu.baseColorFactor.r, ",", mgpu.baseColorFactor.g, ",", mgpu.baseColorFactor.b, ",", mgpu.baseColorFactor.a,
                  "] metallic=", mgpu.metallicFactor, " roughness=", mgpu.roughnessFactor,
                  " baseTex=", (mat.pbrData.baseColorTexture ? "yes" : "no"),
                  " mrTex=", (mat.pbrData.metallicRoughnessTexture ? "yes" : "no"),
                  " normalTex=", (mat.normalTexture ? "yes" : "no"),
                  " emissiveTex=", (mat.emissiveTexture ? "yes" : "no"),
                  " unlit=", (mgpu.unlit ? "yes" : "no")));
        if (!mat.pbrData.metallicRoughnessTexture) {
            // No MR texture: honor the material's scalar roughness/metallic factors directly.
            log::info(log::format("Note: No MetallicRoughness-Texture, using roughness=", mgpu.roughnessFactor, " metallic=", mgpu.metallicFactor));
        }
    } else {
        mgpu.baseColorFactor = glm::vec4(1.0f);
        mgpu.metallicFactor = 1.0f;
        mgpu.roughnessFactor = 1.0f;
        mgpu.emissiveFactor = glm::vec3(0.0f);
        mgpu.doubleSided = false;
        mgpu.unlit = false;
        mgpu.baseColorTex = cache.white;
        mgpu.mrTex = cache.mrrDefault;
        mgpu.normalTex = cache.normalDefault;
        mgpu.emissiveTex = cache.emissiveDefault;
    }

    gpuPrim.material = mgpu;

    log::info(log::format(attrLog.str(), " | indices=", indices.size(), " verts=", positions.size()));
    return gpuPrim;
}

void expandBounds(glm::vec3& minOut, glm::vec3& maxOut, const glm::mat4& m, const glm::vec3& localMin, const glm::vec3& localMax) {
    std::array<glm::vec3, 8> corners = {
        glm::vec3(localMin.x, localMin.y, localMin.z),
        glm::vec3(localMax.x, localMin.y, localMin.z),
        glm::vec3(localMin.x, localMax.y, localMin.z),
        glm::vec3(localMax.x, localMax.y, localMin.z),
        glm::vec3(localMin.x, localMin.y, localMax.z),
        glm::vec3(localMax.x, localMin.y, localMax.z),
        glm::vec3(localMin.x, localMax.y, localMax.z),
        glm::vec3(localMax.x, localMax.y, localMax.z),
    };
    for (auto& c : corners) {
        glm::vec3 wc = glm::vec3(m * glm::vec4(c, 1.0f));
        minOut = glm::min(minOut, wc);
        maxOut = glm::max(maxOut, wc);
    }
}

void gatherDrawCommands(const fastgltf::Asset& asset, std::size_t nodeIndex, const glm::mat4& parent, std::vector<DrawCommand>& out) {
    const auto& node = asset.nodes[nodeIndex];
    glm::mat4 model = parent * nodeTransform(node);
    if (node.meshIndex) {
        DrawCommand dc{};
        dc.model = model;
        dc.meshIndex = *node.meshIndex;
        out.push_back(dc);
    }
    for (auto child : node.children) {
        gatherDrawCommands(asset, child, model, out);
    }
}

std::optional<LoadedGltf> loadGltfAsset(const fs::path& gltfPath) {
    auto dataResult = fastgltf::GltfDataBuffer::FromPath(gltfPath);
    if (!dataResult) {
        log::error(log::format("Failed to load glTF: ", gltfPath, " (Error ", static_cast<int>(dataResult.error()), ")"));
        return std::nullopt;
    }

    LoadedGltf loaded;
    loaded.buffer = std::move(dataResult.get()); // keep buffer alive as long as asset is used

    fastgltf::Parser parser(
        fastgltf::Extensions::KHR_mesh_quantization |
        fastgltf::Extensions::KHR_texture_transform |
        fastgltf::Extensions::KHR_materials_variants);

    auto asset = parser.loadGltf(
        loaded.buffer,
        gltfPath.parent_path(),
        fastgltf::Options::LoadExternalBuffers |
            fastgltf::Options::LoadExternalImages |
            fastgltf::Options::GenerateMeshIndices);

    if (!asset) {
        log::error(log::format("fastgltf error code: ", static_cast<int>(asset.error())));
        return std::nullopt;
    }

    loaded.asset = std::move(asset.get());
    return loaded;
}

void printAssetSummary(const fastgltf::Asset& asset, const fs::path& gltfPath) {
    log::info(log::format("Loaded: ", gltfPath));
    log::info(log::format("Scenes:   ", asset.scenes.size()));
    log::info(log::format("Nodes:    ", asset.nodes.size()));
    log::info(log::format("Meshes:   ", asset.meshes.size()));
    log::info(log::format("Materials:", asset.materials.size()));
    log::info(log::format("Images:   ", asset.images.size()));
}

SceneGPU buildSceneGPU(const fastgltf::Asset& asset, const fs::path& basePath, TextureCache& texCache, fastgltf::DefaultBufferDataAdapter& adapter) {
    SceneGPU scene{};
    scene.meshes.reserve(asset.meshes.size());
    // Identify WhiteKey / BlackKey / BackPlane mesh indices and optional preferred camera.
    std::optional<std::size_t> whiteKeyMeshIndex;
    std::optional<std::size_t> blackKeyMeshIndex;
    std::optional<std::size_t> backPlaneMeshIndex;
    std::optional<std::size_t> baseMeshIndex;
    std::optional<glm::mat4> whiteKeyModel;
    std::optional<glm::mat4> blackKeyModel;
    std::optional<glm::mat4> backPlaneModel;
    std::optional<glm::mat4> baseModel;
    std::optional<std::size_t> preferredCameraIndex;
    for (std::size_t i = 0; i < asset.cameras.size(); ++i) {
        if (asset.cameras[i].name == "Camera") {
            preferredCameraIndex = i;
            break;
        }
    }
    for (std::size_t i = 0; i < asset.meshes.size(); ++i) {
        if (asset.meshes[i].name == "WhiteKey") {
            whiteKeyMeshIndex = i;
        }
        if (asset.meshes[i].name == "BlackKey") {
            blackKeyMeshIndex = i;
        }
        if (asset.meshes[i].name == "BackPlane") {
            backPlaneMeshIndex = i;
        }
        if (asset.meshes[i].name == "Base") {
            baseMeshIndex = i;
        }
    }

    for (const auto& mesh : asset.meshes) {
        MeshGPU gpuMesh{};
        gpuMesh.primitives.reserve(mesh.primitives.size());
        for (const auto& prim : mesh.primitives) {
            gpuMesh.primitives.push_back(uploadPrimitive(asset, prim, basePath, texCache, adapter));
        }
        scene.meshes.push_back(std::move(gpuMesh));
    }

    std::size_t sceneIndex = asset.defaultScene.value_or(0);
    if (sceneIndex >= asset.scenes.size()) sceneIndex = 0;
    // Skip nodes that reference the WhiteKey mesh; they will be drawn via instancing.
    std::unordered_set<std::size_t> skipMeshes;
    if (whiteKeyMeshIndex) skipMeshes.insert(*whiteKeyMeshIndex);
    if (blackKeyMeshIndex) skipMeshes.insert(*blackKeyMeshIndex);

    std::function<void(std::size_t, const glm::mat4&)> gatherFiltered = [&](std::size_t nodeIndex, const glm::mat4& parent) {
        const auto& node = asset.nodes[nodeIndex];
        glm::mat4 model = parent * nodeTransform(node);
        if (node.cameraIndex) {
            const bool isPreferred = preferredCameraIndex && (*node.cameraIndex == *preferredCameraIndex);
            if (!scene.camera.valid || isPreferred) {
                const auto& cam = asset.cameras[*node.cameraIndex];
                if (std::holds_alternative<fastgltf::Camera::Perspective>(cam.camera)) {
                    const auto& p = std::get<fastgltf::Camera::Perspective>(cam.camera);
                    scene.camera.world = model;
                    scene.camera.view = glm::inverse(model);
                    scene.camera.yfov = p.yfov;
                    scene.camera.znear = p.znear;
                    scene.camera.zfar = p.zfar.value_or(1000.0f);
                    if (p.aspectRatio.has_value()) scene.camera.aspect = *p.aspectRatio;
                    scene.camera.valid = true;
                    glm::vec3 eye = glm::vec3(scene.camera.world[3]);
                    glm::vec3 forward = glm::normalize(glm::vec3(scene.camera.world * glm::vec4(0,0,-1,0)));
                    glm::vec3 up = glm::normalize(glm::vec3(scene.camera.world * glm::vec4(0,1,0,0)));
                    log::info(log::format("Camera '", cam.name, "' pos=", eye.x, ",", eye.y, ",", eye.z,
                              " forward=", forward.x, ",", forward.y, ",", forward.z,
                              " up=", up.x, ",", up.y, ",", up.z,
                              " fovY(deg)=", glm::degrees(scene.camera.yfov),
                              " aspect=", (scene.camera.aspect ? std::to_string(*scene.camera.aspect) : std::string("fb"))));
                }
            }
        }
        if (node.meshIndex) {
            if (skipMeshes.count(*node.meshIndex)) {
                if (whiteKeyMeshIndex && *node.meshIndex == *whiteKeyMeshIndex && !whiteKeyModel) whiteKeyModel = model;
                if (blackKeyMeshIndex && *node.meshIndex == *blackKeyMeshIndex && !blackKeyModel) blackKeyModel = model;
            } else {
                if (backPlaneMeshIndex && *node.meshIndex == *backPlaneMeshIndex && !backPlaneModel) backPlaneModel = model;
                if (baseMeshIndex && *node.meshIndex == *baseMeshIndex && !baseModel) baseModel = model;
                scene.drawCommands.push_back(DrawCommand{model, *node.meshIndex});
            }
        }
        for (auto child : node.children) {
            gatherFiltered(child, model);
        }
    };

    for (auto root : asset.scenes[sceneIndex].nodeIndices) {
        gatherFiltered(root, glm::mat4(1.0f));
    }

    scene.keyRotations = createKeyRotationBuffer();
    scene.keyRotationsDirty = true;
    scene.pitchToKey = buildPitchToKeyMapping();
    const GLintptr whiteRotOffset = 0;
    const GLintptr blackRotOffset = static_cast<GLintptr>(kWhiteKeys.size() * sizeof(float));

    // If we have a WhiteKey mesh, set up instancing and add a draw command for it.
    if (whiteKeyMeshIndex) {
        InstanceInfo inst = createWhiteKeyInstanceBuffer(scene.keyRotations.vbo, whiteRotOffset);
        for (auto& prim : scene.meshes[*whiteKeyMeshIndex].primitives) {
            bindInstanceAttributes(prim.vao, inst);
        }
        scene.instancedMeshes[*whiteKeyMeshIndex] = inst;
        glm::mat4 baseModel = whiteKeyModel.value_or(glm::mat4(1.0f));
        scene.drawCommands.push_back(DrawCommand{baseModel, *whiteKeyMeshIndex});
    }
    // If we have a BlackKey mesh, set up instancing (center only) and add a draw command.
    if (blackKeyMeshIndex) {
        InstanceInfo inst = createBlackKeyInstanceBuffer(scene.keyRotations.vbo, blackRotOffset);
        for (auto& prim : scene.meshes[*blackKeyMeshIndex].primitives) {
            bindInstanceAttributes(prim.vao, inst);
        }
        scene.instancedMeshes[*blackKeyMeshIndex] = inst;
        glm::mat4 baseModel = blackKeyModel.value_or(glm::mat4(1.0f));
        scene.drawCommands.push_back(DrawCommand{baseModel, *blackKeyMeshIndex});
    }

    glm::vec3 sceneMin = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 sceneMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (const auto& dc : scene.drawCommands) {
        const auto& mesh = scene.meshes[dc.meshIndex];
        for (const auto& prim : mesh.primitives) {
            expandBounds(sceneMin, sceneMax, dc.model, prim.aabbMin, prim.aabbMax);
        }
    }
    if (!scene.drawCommands.empty()) {
        scene.sceneCenter = (sceneMin + sceneMax) * 0.5f;
        scene.sceneRadius = glm::length(sceneMax - sceneMin) * 0.5f;
        if (scene.sceneRadius < 0.001f) scene.sceneRadius = 1.0f;
    }
    scene.sceneMin = sceneMin;
    scene.sceneMax = sceneMax;
    scene.backPlaneMeshIndex = backPlaneMeshIndex;
    scene.backPlaneModel = backPlaneModel;
    if (backPlaneMeshIndex && backPlaneModel) {
        glm::vec3 localMin(std::numeric_limits<float>::max());
        glm::vec3 localMax(std::numeric_limits<float>::lowest());
        for (const auto& prim : scene.meshes[*backPlaneMeshIndex].primitives) {
            localMin = glm::min(localMin, prim.aabbMin);
            localMax = glm::max(localMax, prim.aabbMax);
        }
        glm::vec3 extent = localMax - localMin;
        glm::vec3 localNormal(0, 0, 1);
        float minExtent = std::numeric_limits<float>::max();
        int axis = 2;
        for (int i = 0; i < 3; ++i) {
            if (extent[i] < minExtent) {
                minExtent = extent[i];
                axis = i;
            }
        }
        if (axis == 0) localNormal = glm::vec3(1, 0, 0);
        else if (axis == 1) localNormal = glm::vec3(0, 1, 0);
        else localNormal = glm::vec3(0, 0, 1);
        glm::vec3 localPoint = (localMin + localMax) * 0.5f;
        glm::vec3 worldNormal = glm::normalize(glm::vec3(*backPlaneModel * glm::vec4(localNormal, 0.0f)));
        glm::vec3 worldPoint = glm::vec3(*backPlaneModel * glm::vec4(localPoint, 1.0f));
        scene.backPlaneNormalWS = worldNormal;
        scene.backPlanePointWS = worldPoint;
    }
    scene.baseMeshIndex = baseMeshIndex;
    scene.envMap = texCache.envMap;
    return scene;
}

glm::mat4 makeReflectionMatrix(const glm::vec3& normal, const glm::vec3& point) {
    glm::vec3 n = glm::normalize(normal);
    float d = -glm::dot(n, point);
    glm::mat4 R(1.0f);
    R[0][0] = 1 - 2 * n.x * n.x;
    R[0][1] = -2 * n.x * n.y;
    R[0][2] = -2 * n.x * n.z;
    R[1][0] = -2 * n.y * n.x;
    R[1][1] = 1 - 2 * n.y * n.y;
    R[1][2] = -2 * n.y * n.z;
    R[2][0] = -2 * n.z * n.x;
    R[2][1] = -2 * n.z * n.y;
    R[2][2] = 1 - 2 * n.z * n.z;
    R[3][0] = -2 * d * n.x;
    R[3][1] = -2 * d * n.y;
    R[3][2] = -2 * d * n.z;
    return R;
}

// Skybox geometry (unit cube)
GLuint createSkyboxVAO() {
    static const float verts[] = {
        -1,  1, -1,  -1, -1, -1,   1, -1, -1,   1, -1, -1,   1,  1, -1,  -1,  1, -1, // back
        -1, -1,  1,  -1, -1, -1,  -1,  1, -1,  -1,  1, -1,  -1,  1,  1,  -1, -1,  1, // left
         1, -1, -1,   1, -1,  1,   1,  1,  1,   1,  1,  1,   1,  1, -1,   1, -1, -1, // right
        -1, -1,  1,  -1,  1,  1,   1,  1,  1,   1,  1,  1,   1, -1,  1,  -1, -1,  1, // front
        -1,  1, -1,   1,  1, -1,   1,  1,  1,   1,  1,  1,  -1,  1,  1,  -1,  1, -1, // top
        -1, -1, -1,  -1, -1,  1,   1, -1, -1,   1, -1, -1,  -1, -1,  1,   1, -1,  1  // bottom
    };
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    return vao;
}

ShadowMap createShadowMap(int size) {
    ShadowMap s{};
    s.size = size;
    glGenFramebuffers(1, &s.fbo);
    glGenTextures(1, &s.depthTex);
    glBindTexture(GL_TEXTURE_2D, s.depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s.depthTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::error("Shadow FBO incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return s;
}

LightViewData computeLightView(const SceneGPU& scene, const glm::vec3& lightDir, int shadowMapSize) {
    LightViewData out{};
    glm::vec3 dir = glm::normalize(lightDir);
    glm::vec3 target = scene.sceneCenter;
    float radius = std::max(scene.sceneRadius, 1.0f);
    float dist = radius * 3.5f;
    glm::vec3 eye = target - dir * dist;
    glm::vec3 up = glm::abs(glm::dot(dir, glm::vec3(0,1,0))) > 0.9f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
    glm::mat4 view = glm::lookAt(eye, target, up);

    // Project scene bounds into light space.
    std::array<glm::vec3, 8> corners = {
        glm::vec3(scene.sceneMin.x, scene.sceneMin.y, scene.sceneMin.z),
        glm::vec3(scene.sceneMax.x, scene.sceneMin.y, scene.sceneMin.z),
        glm::vec3(scene.sceneMin.x, scene.sceneMax.y, scene.sceneMin.z),
        glm::vec3(scene.sceneMax.x, scene.sceneMax.y, scene.sceneMin.z),
        glm::vec3(scene.sceneMin.x, scene.sceneMin.y, scene.sceneMax.z),
        glm::vec3(scene.sceneMax.x, scene.sceneMin.y, scene.sceneMax.z),
        glm::vec3(scene.sceneMin.x, scene.sceneMax.y, scene.sceneMax.z),
        glm::vec3(scene.sceneMax.x, scene.sceneMax.y, scene.sceneMax.z),
    };
    glm::vec3 minLS(std::numeric_limits<float>::max());
    glm::vec3 maxLS(std::numeric_limits<float>::lowest());
    for (const auto& c : corners) {
        glm::vec3 ls = glm::vec3(view * glm::vec4(c, 1.0f));
        minLS = glm::min(minLS, ls);
        maxLS = glm::max(maxLS, ls);
    }

    // Add a small padding and ensure non-zero extents.
    glm::vec3 extent = (maxLS - minLS) * 0.5f;
    extent = glm::max(extent, glm::vec3(0.5f));
    extent *= 1.05f;
    glm::vec3 center = (maxLS + minLS) * 0.5f;

    // Snap to texel size in light space (xy) to stabilize shadows.
    float texelSizeX = (extent.x * 2.0f) / static_cast<float>(std::max(1, shadowMapSize));
    float texelSizeY = (extent.y * 2.0f) / static_cast<float>(std::max(1, shadowMapSize));
    center.x = std::floor(center.x / texelSizeX) * texelSizeX;
    center.y = std::floor(center.y / texelSizeY) * texelSizeY;

    glm::vec3 minSnap = center - extent;
    glm::vec3 maxSnap = center + extent;

    float nearPlane = std::max(0.1f, -maxSnap.z);
    float farPlane = -minSnap.z + 20.0f; // small padding along light depth
    if (farPlane <= nearPlane + 0.1f) {
        farPlane = nearPlane + 50.0f;
    }

    glm::mat4 proj = glm::ortho(minSnap.x, maxSnap.x, minSnap.y, maxSnap.y, nearPlane, farPlane);

    out.vp = proj * view;
    out.eye = eye;
    out.dir = dir;
    out.orthoRadius = std::max(extent.x, extent.y);
    return out;
}

void renderFrame(SceneGPU& scene, const GLProgram& pbrProgram, const SkyboxProgram& skyboxProgram, const ShadowProgram& shadowProgram, ShadowMap& shadowMap, GLuint skyboxVAO, const FrameParams& frame, bool useMovingCamera, bool useKeyboardCamera) {
    int fbWidth = frame.fbWidth;
    int fbHeight = frame.fbHeight;
    if (fbWidth <= 0) fbWidth = 1;
    if (fbHeight <= 0) fbHeight = 1;
    glViewport(0, 0, fbWidth, fbHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    double now = frame.timeSeconds;
    if (scene.lastTime == 0.0) scene.lastTime = now;
    float dt = static_cast<float>(now - scene.lastTime);
    scene.lastTime = now;
    float t = static_cast<float>(now);
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 camPos;
    float aspect = fbHeight > 0 ? static_cast<float>(fbWidth) / static_cast<float>(fbHeight) : 1.0f;
    float fov = glm::radians(30.0f);
    if (useKeyboardCamera) {
        KeyboardBounds kb = computeKeyboardBounds();
        float halfWidth = kb.spanZ * 0.5f * KEYBOARD_SPATIAL_PADDING;
        float hFov = 2.0f * std::atan(std::tan(fov * 0.5f) * aspect);
        float dist = halfWidth / std::tan(hFov * 0.5f);
        camPos = glm::vec3(dist * CAMERA_DISTANCE_SCALE, kb.spanZ * CAMERA_HEIGHT_RATIO, kb.centerZ);
        view = glm::lookAt(camPos, glm::vec3(0.0f, -1.2f, kb.centerZ), glm::vec3(0.0f, 1.0f, 0.0f));
        proj = glm::perspective(fov, aspect, 0.01f, dist + kb.spanZ * 3.0f);
    } else if (useMovingCamera) {
        // Animated camera that orbits and bobs around the scene center.
        float camDist = scene.sceneRadius * 1.1f;
        float az = t * 0.35f;
        float radial = camDist + std::sin(t * 0.4f) * scene.sceneRadius * 0.1f;
        float height = scene.sceneRadius * (0.35f + 0.15f * std::sin(t * 0.25f));
        camPos = scene.sceneCenter + glm::vec3(std::cos(az) * radial, height, std::sin(az) * radial);
        view = glm::lookAt(camPos, scene.sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));
        proj = glm::perspective(glm::radians(50.0f), aspect, 0.01f, camDist * 6.0f);
    } else if (scene.camera.valid) {
        view = scene.camera.view;
        camPos = glm::vec3(scene.camera.world[3]);
        float aspectCam = scene.camera.aspect.value_or(fbHeight > 0 ? static_cast<float>(fbWidth) / static_cast<float>(fbHeight) : 1.0f);
        proj = glm::perspective(scene.camera.yfov, aspectCam, scene.camera.znear, scene.camera.zfar);
    } else {
        float camDist = scene.sceneRadius * 0.85f;
        camPos = scene.sceneCenter + glm::vec3(std::sin(t * 0.3f) * camDist, scene.sceneRadius * 0.6f, std::cos(t * 0.3f) * camDist);
        view = glm::lookAt(camPos, scene.sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));
        proj = glm::perspective(glm::radians(50.0f), aspect, 0.01f, camDist * 6.0f);
    }

    updateAnimations(scene, dt);
    if (scene.keyRotationsDirty) {
        uploadKeyRotations(scene);
    }

    // Directional light setup (shared between shadow and shading) with gentle sway.
    float swayYaw = glm::radians(4.0f) * std::sin(t * 0.25f);    // slow horizontal sway
    float swayPitch = glm::radians(3.0f) * std::sin(t * 0.18f);  // slow vertical sway
    glm::vec3 baseLightDir = glm::normalize(glm::vec3(-7.3f, 9.0f, 4.6f)); // fragment -> light
    glm::mat4 sway = glm::rotate(glm::mat4(1.0f), swayYaw, glm::vec3(0,1,0));
    sway = glm::rotate(sway, swayPitch, glm::vec3(1,0,0));
    glm::vec3 lightDir = glm::normalize(glm::vec3(sway * glm::vec4(baseLightDir, 0.0f)));
    LightViewData lightView = computeLightView(scene, -lightDir, shadowMap.size);       // light looks toward target along -lightDir
    shadowMap.lightVP = lightView.vp;
    static double lastLog = 0.0;
    double logNow = now;
    if (logNow - lastLog > 2.0) {
        log::info(log::format("[ShadowDbg] lightDir(fragment->light)=", lightDir.x, ",", lightDir.y, ",", lightDir.z,
                  " lightEye=", lightView.eye.x, ",", lightView.eye.y, ",", lightView.eye.z,
                  " orthoRadius=", lightView.orthoRadius));
        lastLog = logNow;
    }

    // Shadow map pass (depth only).
    glViewport(0, 0, shadowMap.size, shadowMap.size);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_CULL_FACE); // include both sides in shadow pass
    glUseProgram(shadowProgram.id);
    glUniformMatrix4fv(shadowProgram.locLightVP, 1, GL_FALSE, glm::value_ptr(shadowMap.lightVP));
    auto drawDepth = [&](const DrawCommand& dc) {
        auto instIt = scene.instancedMeshes.find(dc.meshIndex);
        const InstanceInfo* instInfo = (instIt != scene.instancedMeshes.end()) ? &instIt->second : nullptr;
        for (const auto& prim : scene.meshes[dc.meshIndex].primitives) {
            glBindVertexArray(prim.vao);
            glUniformMatrix4fv(shadowProgram.locModel, 1, GL_FALSE, glm::value_ptr(dc.model));
            if (instInfo) {                
                glDrawElementsInstanced(prim.mode, prim.indexCount, GL_UNSIGNED_INT, nullptr, instInfo->count);
            } else {
                glDrawElements(prim.mode, prim.indexCount, GL_UNSIGNED_INT, nullptr);
            }
        }
    };
    for (const auto& dc : scene.drawCommands) {
        drawDepth(dc);
    }
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbWidth, fbHeight);

    // Skybox first (stencil off)
    glDisable(GL_STENCIL_TEST);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glUseProgram(skyboxProgram.id);
    glm::mat4 viewNoTrans = glm::mat4(glm::mat3(view));
    glUniformMatrix4fv(skyboxProgram.locView, 1, GL_FALSE, glm::value_ptr(viewNoTrans));
    glUniformMatrix4fv(skyboxProgram.locProj, 1, GL_FALSE, glm::value_ptr(proj));
    // Apply an environment rotation (tweak yaw/pitch/roll if the cubemap feels misaligned).
    // Align cubemap to world with Y+ up; adjust yaw/pitch/roll only if your HDRI is authored differently.
    float yaw = glm::radians(0.0f);    // Y axis
    float pitch = glm::radians(0.0f);   // X axis (use ±90 to map Z-up cube to Y-up world if needed)
    float roll = glm::radians(0.0f);   // Z axis
    glm::mat4 envRotM = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0,1,0));
    envRotM = glm::rotate(envRotM, pitch, glm::vec3(1,0,0));
    envRotM = glm::rotate(envRotM, roll, glm::vec3(0,0,1));
    glm::mat3 envRot = glm::mat3(envRotM);
    glUniformMatrix3fv(skyboxProgram.locEnvRot, 1, GL_FALSE, glm::value_ptr(envRot));
    glUniform1i(skyboxProgram.locCubemap, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, scene.envMap);
    glBindVertexArray(skyboxVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // Clear stencil for the reflective passes
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClear(GL_STENCIL_BUFFER_BIT);

    glUseProgram(pbrProgram.id);
    glUniformMatrix4fv(pbrProgram.locView, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(pbrProgram.locProj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix4fv(pbrProgram.locLightVP, 1, GL_FALSE, glm::value_ptr(shadowMap.lightVP));
    glUniform3fv(pbrProgram.locCamPos, 1, glm::value_ptr(camPos));
    glUniform3fv(glGetUniformLocation(pbrProgram.id, "uLightDir"), 1, glm::value_ptr(lightDir));
    glUniform1i(glGetUniformLocation(pbrProgram.id, "uBaseColorTex"), 0);
    glUniform1i(glGetUniformLocation(pbrProgram.id, "uMRRTex"), 1);
    glUniform1i(glGetUniformLocation(pbrProgram.id, "uNormalTex"), 2);
    glUniform1i(glGetUniformLocation(pbrProgram.id, "uEmissiveTex"), 3);
    glUniform1i(pbrProgram.locEnvMap, 4);
    glUniform1i(pbrProgram.locShadowMap, 5);
    glUniform1f(pbrProgram.locEnvIntensity, ENVIRONMENT_INTENSITY);
    glUniform1f(pbrProgram.locShadowBias, SHADOW_BIAS);
    const bool debugShadow = false;
    glUniform1i(pbrProgram.locShadowDebug, debugShadow ? 1 : 0);
    glUniformMatrix3fv(pbrProgram.locEnvRot, 1, GL_FALSE, glm::value_ptr(envRot));
    glUniform3f(glGetUniformLocation(pbrProgram.id, "uAmbientColor"), 0.15f, 0.13f, 0.12f);
    glUniform1f(glGetUniformLocation(pbrProgram.id, "uLightIntensity"), LIGHT_INTENSITY);
    glUniform1f(pbrProgram.locDarken, 1.0f);
    if (scene.backPlaneNormalWS && scene.backPlanePointWS) {
        glUniform3fv(pbrProgram.locGroundNormal, 1, glm::value_ptr(*scene.backPlaneNormalWS));
        glUniform3fv(pbrProgram.locGroundPoint, 1, glm::value_ptr(*scene.backPlanePointWS));
        glUniform2f(pbrProgram.locGroundParams, scene.sceneRadius * 0.03f, 0.35f);
    } else {
        glUniform3f(pbrProgram.locGroundNormal, 0.0f, 0.0f, 0.0f);
        glUniform3f(pbrProgram.locGroundPoint, 0.0f, 0.0f, 0.0f);
        glUniform2f(pbrProgram.locGroundParams, 0.0f, 0.0f);
    }
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, shadowMap.depthTex);

    auto drawDC = [&](const DrawCommand& dc, const glm::mat4& modelOverride, bool enableStencilWrite, bool disableCull) {
        auto instIt = scene.instancedMeshes.find(dc.meshIndex);
        const InstanceInfo* instInfo = (instIt != scene.instancedMeshes.end()) ? &instIt->second : nullptr;

        for (const auto& prim : scene.meshes[dc.meshIndex].primitives) {
            if (disableCull) {
                glDisable(GL_CULL_FACE);
            } else if (prim.material.doubleSided) {
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            glStencilMask(enableStencilWrite ? 0xFF : 0x00);
            glStencilOp(GL_KEEP, GL_KEEP, enableStencilWrite ? GL_REPLACE : GL_KEEP);

            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, prim.material.baseColorTex);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, prim.material.mrTex);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, prim.material.normalTex);
            glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, prim.material.emissiveTex);
            glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_CUBE_MAP, scene.envMap);

            glUniform4fv(pbrProgram.locBaseColor, 1, glm::value_ptr(prim.material.baseColorFactor));
            glUniform1f(pbrProgram.locMetallic, prim.material.metallicFactor);
            glUniform1f(pbrProgram.locRoughness, prim.material.roughnessFactor);
            glUniform3fv(pbrProgram.locEmissive, 1, glm::value_ptr(prim.material.emissiveFactor));
            glUniform1i(pbrProgram.locUnlit, prim.material.unlit ? 1 : 0);

            glm::mat4 model = modelOverride;
            glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));
            glUniformMatrix4fv(pbrProgram.locModel, 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(pbrProgram.locNormalMat, 1, GL_FALSE, glm::value_ptr(normalMat));

            glBindVertexArray(prim.vao);
            if (instInfo) {
                glDrawElementsInstanced(prim.mode, prim.indexCount, GL_UNSIGNED_INT, nullptr, instInfo->count);
            } else {
                glDrawElements(prim.mode, prim.indexCount, GL_UNSIGNED_INT, nullptr);
            }
        }
    };

    // Pass 1: draw BackPlane to stencil (and color), avoid writing depth.
    if (scene.backPlaneMeshIndex) {
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glDepthMask(GL_FALSE);
        for (const auto& dc : scene.drawCommands) {
            if (dc.meshIndex == *scene.backPlaneMeshIndex) {
                drawDC(dc, dc.model, true, false);
            }
        }
        glDepthMask(GL_TRUE);
    }

    // Pass 2: mirrored instanced keys inside stencil.
    if (scene.backPlaneMeshIndex && scene.backPlaneModel) {
        glm::vec3 planePoint = scene.backPlanePointWS.value_or(glm::vec3(*scene.backPlaneModel * glm::vec4(0, 0, 0, 1)));
        glm::vec3 planeNormal = scene.backPlaneNormalWS.value_or(glm::normalize(glm::vec3(*scene.backPlaneModel * glm::vec4(0, 0, 1, 0))));
        glm::mat4 reflect = makeReflectionMatrix(planeNormal, planePoint);
        // Reflect the camera position so view-dependent terms (env reflection/Fresnel) match the mirrored world.
        float camDist = glm::dot(planeNormal, camPos - planePoint);
        glm::vec3 camPosReflected = camPos - 2.0f * camDist * planeNormal;
        glStencilFunc(GL_EQUAL, 1, 0xFF);
        glStencilMask(0x00);
        glEnable(GL_DEPTH_TEST);
        glUniform1f(pbrProgram.locDarken, 0.0025f);
        glUniform3fv(pbrProgram.locCamPos, 1, glm::value_ptr(camPosReflected));
        for (const auto& dc : scene.drawCommands) {
            bool isInstanced = scene.instancedMeshes.find(dc.meshIndex) != scene.instancedMeshes.end();
            bool isBase = scene.baseMeshIndex && dc.meshIndex == *scene.baseMeshIndex;
            if (!isInstanced && !isBase) continue;
            if (scene.backPlaneMeshIndex && dc.meshIndex == *scene.backPlaneMeshIndex) continue;
            drawDC(dc, reflect * dc.model, false, true);
        }
        // Restore original camera position for subsequent passes.
        glUniform3fv(pbrProgram.locCamPos, 1, glm::value_ptr(camPos));
    }

    // Pass 3: normal scene (no stencil test)
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0x00);
    glUniform1f(pbrProgram.locDarken, 1.0f);
    for (const auto& dc : scene.drawCommands) {
        if (scene.backPlaneMeshIndex && dc.meshIndex == *scene.backPlaneMeshIndex) continue;
        drawDC(dc, dc.model, false, false);
    }
}

} // namespace

struct Renderer::Impl {
    InitParams params{};
    TextureCache texCache{};
    SceneGPU scene{};
    GLProgram pbrProg{};
    SkyboxProgram skyboxProg{};
    ShadowProgram shadowProg{};
    CaptureProgram captureProg{};
    ShadowMap shadowMap{};
    GLuint skyboxVAO = 0;
    fastgltf::DefaultBufferDataAdapter adapter{};
    std::optional<LoadedGltf> gltf;
    bool initialized = false;
    bool useKeyboardCamera = true;
    bool useMovingCamera = false;
};

Renderer::Renderer() : impl(std::make_unique<Impl>()) {}
Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(const InitParams& params) {
    using namespace shaders;  // For embedded shader source strings

    if (!impl) return false;
    impl->params = params;
    impl->useKeyboardCamera = params.useKeyboardCamera;
    impl->useMovingCamera = params.useMovingCamera && !params.useKeyboardCamera;

    impl->gltf = loadGltfAsset(params.gltfPath);
    if (!impl->gltf) {
        return false;
    }
    auto& asset = impl->gltf->asset;
    printAssetSummary(asset, params.gltfPath);
    if (asset.scenes.empty() || asset.meshes.empty()) {
        log::error("No scenes or meshes in asset.");
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);

    impl->texCache.white = createFallbackTexture(glm::vec4(1.0f), true);
    impl->texCache.mrrDefault = createFallbackTexture(glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), false);
    impl->texCache.normalDefault = createFallbackTexture(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f), false);
    impl->texCache.emissiveDefault = createFallbackTexture(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), true);

    impl->skyboxVAO = createSkyboxVAO();
    impl->captureProg = createCaptureProgram(shaders::equirect_to_cube_vert, shaders::equirect_to_cube_frag);

    // Load HDR environment map for image-based lighting
    if (std::filesystem::exists(params.envHdrPath)) {
        impl->texCache.envMap = loadEquirectangularAsCubemap(params.envHdrPath.string(), 2048, impl->skyboxVAO, impl->captureProg);
    }

    // Fallback to solid-color cubemap if HDR loading failed
    if (impl->texCache.envMap == 0) {
        log::warning("Falling back to solid-color cubemap.");
        impl->texCache.envMap = createFallbackCubemap(glm::vec3(0.05f, 0.07f, 0.09f));
    }

    impl->scene = buildSceneGPU(asset, params.gltfPath.parent_path(), impl->texCache, impl->adapter);
    impl->pbrProg = createPbrProgram(shaders::pbr_vert, shaders::pbr_frag);
    impl->skyboxProg = createSkyboxProgram(shaders::skybox_vert, shaders::skybox_frag);
    impl->shadowProg = createShadowProgram(shaders::shadow_depth_vert, shaders::shadow_depth_frag);
    impl->shadowMap = createShadowMap(params.shadowMapSize);
    impl->initialized = true;
    return true;
}

void Renderer::render(const FrameParams& frame) {
    if (!impl || !impl->initialized) return;
    renderFrame(impl->scene, impl->pbrProg, impl->skyboxProg, impl->shadowProg, impl->shadowMap, impl->skyboxVAO, frame, impl->useMovingCamera, impl->useKeyboardCamera);
}

void Renderer::noteOn(int midiPitch) {
    if (!impl || !impl->initialized) return;
    midiNoteOn(impl->scene, midiPitch);
}

void Renderer::noteOff(int midiPitch) {
    if (!impl || !impl->initialized) return;
    midiNoteOff(impl->scene, midiPitch);
}

void Renderer::shutdown() {
    if (!impl || !impl->initialized) return;
    for (auto& [idx, tex] : impl->texCache.imageTextures) {
        glDeleteTextures(1, &tex);
    }
    glDeleteTextures(1, &impl->texCache.white);
    glDeleteTextures(1, &impl->texCache.mrrDefault);
    glDeleteTextures(1, &impl->texCache.normalDefault);
    glDeleteTextures(1, &impl->texCache.emissiveDefault);
    glDeleteTextures(1, &impl->texCache.envMap);
    if (impl->scene.keyRotations.vbo) glDeleteBuffers(1, &impl->scene.keyRotations.vbo);
    for (auto& [meshIdx, inst] : impl->scene.instancedMeshes) {
        if (inst.vbo) glDeleteBuffers(1, &inst.vbo);
    }
    for (auto& mesh : impl->scene.meshes) {
        for (auto& prim : mesh.primitives) {
            if (prim.vbo) glDeleteBuffers(1, &prim.vbo);
            if (prim.ebo) glDeleteBuffers(1, &prim.ebo);
            if (prim.vao) glDeleteVertexArrays(1, &prim.vao);
        }
    }
    if (impl->shadowMap.depthTex) glDeleteTextures(1, &impl->shadowMap.depthTex);
    if (impl->shadowMap.fbo) glDeleteFramebuffers(1, &impl->shadowMap.fbo);
    if (impl->skyboxVAO) glDeleteVertexArrays(1, &impl->skyboxVAO);
    if (impl->pbrProg.id) glDeleteProgram(impl->pbrProg.id);
    if (impl->skyboxProg.id) glDeleteProgram(impl->skyboxProg.id);
    if (impl->shadowProg.id) glDeleteProgram(impl->shadowProg.id);
    if (impl->captureProg.id) glDeleteProgram(impl->captureProg.id);
    impl->initialized = false;
}

} // namespace keys
