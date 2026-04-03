// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#pragma once

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wdeprecated-volatile"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wredundant-move"
#pragma GCC diagnostic ignored "-Wvolatile"
#endif

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#include <glad/glad.h>
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

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

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
#include "log.hpp"

namespace fs = std::filesystem;

namespace keys {

constexpr float CAMERA_DISTANCE_SCALE = 0.6f;
constexpr float CAMERA_HEIGHT_RATIO = 0.35f;
constexpr float KEYBOARD_SPATIAL_PADDING = 1.05f;
constexpr float SHADOW_BIAS = 0.00005f;
constexpr float LIGHT_INTENSITY = 5.0f;
constexpr float ENVIRONMENT_INTENSITY = 0.5f;

struct TextureCache {
    std::unordered_map<std::size_t, GLuint> imageTextures;
    GLuint white = 0;
    GLuint mrrDefault = 0;
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
    glm::mat4 world{1.0f};
    glm::mat4 view{1.0f};
    float yfov = glm::radians(50.0f);
    float znear = 0.01f;
    float zfar = 1000.0f;
    std::optional<float> aspect;
    bool valid = false;
};

struct InstanceInfo {
    GLuint vbo = 0;
    GLuint rotationVbo = 0;
    GLintptr rotationOffset = 0;
    GLsizei count = 0;
    GLsizei strideBytes = 0;
};

enum class EaseType { Back, Elastic };

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
    GLint locGroundParams = -1;
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
    std::unordered_map<std::size_t, InstanceInfo> instancedMeshes;
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
    fastgltf::GltfDataBuffer buffer;
    fastgltf::Asset asset;
};

struct CameraState {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec3 pos{0.0f};
};

GLuint compileShader(GLenum type, std::string_view src, std::string_view name);
GLuint createProgram(std::string_view vertSrc, std::string_view fragSrc, std::string_view vertName,
                     std::string_view fragName);
GLProgram createPbrProgram(std::string_view vertSrc, std::string_view fragSrc);
SkyboxProgram createSkyboxProgram(std::string_view vertSrc, std::string_view fragSrc);
CaptureProgram createCaptureProgram(std::string_view vertSrc, std::string_view fragSrc);
ShadowProgram createShadowProgram(std::string_view vertSrc, std::string_view fragSrc);

InstanceInfo createWhiteKeyInstanceBuffer(GLuint rotationVbo, GLintptr rotationOffsetBytes);
InstanceInfo createBlackKeyInstanceBuffer(GLuint rotationVbo, GLintptr rotationOffsetBytes);
void bindInstanceAttributes(GLuint vao, const InstanceInfo &inst);
KeyRotationBuffer createKeyRotationBuffer();
std::array<std::optional<KeyMapping>, 128> buildPitchToKeyMapping();
int rotationIndexForKey(bool isWhite, int keyIndex);
void setKeyRotation(SceneGPU &scene, int midiPitch, float radians);
void uploadKeyRotations(SceneGPU &scene);
float getKeyRotation(const SceneGPU &scene, int midiPitch);
KeyboardBounds computeKeyboardBounds();
void midiNoteOn(SceneGPU &scene, int midiPitch);
void midiNoteOff(SceneGPU &scene, int midiPitch);
void updateAnimations(SceneGPU &scene, float dt);

GLuint createTexture2D(int width, int height, GLint internalFormat, GLenum format, const void *data,
                       bool generateMips = true);
GLuint createFallbackTexture(glm::vec4 color, bool srgb = false);
GLuint createFallbackCubemap(glm::vec3 color);
GLuint loadEquirectangularAsCubemap(const std::string &hdrPath, int size, GLuint skyboxVAO,
                                    const CaptureProgram &captureProg);

std::optional<LoadedGltf> loadGltfAsset(const fs::path &gltfPath);
void printAssetSummary(const fastgltf::Asset &asset, const fs::path &gltfPath);
SceneGPU buildSceneGPU(const fastgltf::Asset &asset, const fs::path &basePath, TextureCache &texCache,
                       fastgltf::DefaultBufferDataAdapter &adapter);

CameraState computeViewProj(const SceneGPU &scene, float aspect, float t, bool useKeyboardCamera, bool useMovingCamera);
glm::vec3 computeSwayedLightDir(float t);
GLuint createSkyboxVAO();
ShadowMap createShadowMap(int size);
LightViewData computeLightView(const SceneGPU &scene, const glm::vec3 &lightDir, int shadowMapSize);
void renderFrame(SceneGPU &scene, const GLProgram &pbrProgram, const SkyboxProgram &skyboxProgram,
                 const ShadowProgram &shadowProgram, ShadowMap &shadowMap, GLuint skyboxVAO, const FrameParams &frame,
                 bool useMovingCamera, bool useKeyboardCamera);

} // namespace keys
