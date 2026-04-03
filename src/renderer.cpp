// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#include "renderer_internal.hpp"
#include "keys_shaders.hpp"

namespace keys {

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
    bool initialized     = false;
    bool useKeyboardCamera = true;
    bool useMovingCamera   = false;
};

Renderer::Renderer() : impl(std::make_unique<Impl>()) {}
Renderer::~Renderer() { shutdown(); }

bool Renderer::init(const InitParams &params) {
    if (!impl) return false;

    impl->params           = params;
    impl->useKeyboardCamera = params.useKeyboardCamera;
    impl->useMovingCamera   = params.useMovingCamera && !params.useKeyboardCamera;

    impl->gltf = loadGltfAsset(params.gltfPath);
    if (!impl->gltf) return false;

    auto &asset = impl->gltf->asset;
    printAssetSummary(asset, params.gltfPath);
    if (asset.scenes.empty() || asset.meshes.empty()) {
        log::error("No scenes or meshes in asset.");
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);

    impl->texCache.white          = createFallbackTexture(glm::vec4(1.0f), true);
    impl->texCache.mrrDefault     = createFallbackTexture(glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), false);
    impl->texCache.normalDefault  = createFallbackTexture(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f), false);
    impl->texCache.emissiveDefault = createFallbackTexture(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), true);

    impl->skyboxVAO  = createSkyboxVAO();
    impl->captureProg = createCaptureProgram(shaders::equirect_to_cube_vert, shaders::equirect_to_cube_frag);

    if (std::filesystem::exists(params.envHdrPath)) {
        impl->texCache.envMap = loadEquirectangularAsCubemap(
            params.envHdrPath.string(), 2048, impl->skyboxVAO, impl->captureProg);
    }
    if (impl->texCache.envMap == 0) {
        log::warning("Falling back to solid-color cubemap.");
        impl->texCache.envMap = createFallbackCubemap(glm::vec3(0.05f, 0.07f, 0.09f));
    }

    impl->scene     = buildSceneGPU(asset, params.gltfPath.parent_path(), impl->texCache, impl->adapter);
    impl->pbrProg   = createPbrProgram(shaders::pbr_vert,          shaders::pbr_frag);
    impl->skyboxProg = createSkyboxProgram(shaders::skybox_vert,   shaders::skybox_frag);
    impl->shadowProg = createShadowProgram(shaders::shadow_depth_vert, shaders::shadow_depth_frag);
    impl->shadowMap  = createShadowMap(params.shadowMapSize);
    impl->initialized = true;
    return true;
}

void Renderer::render(const FrameParams &frame) {
    if (!impl || !impl->initialized) return;
    renderFrame(impl->scene, impl->pbrProg, impl->skyboxProg, impl->shadowProg,
                impl->shadowMap, impl->skyboxVAO, frame,
                impl->useMovingCamera, impl->useKeyboardCamera);
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
    for (auto &[idx, tex] : impl->texCache.imageTextures)
        glDeleteTextures(1, &tex);
    glDeleteTextures(1, &impl->texCache.white);
    glDeleteTextures(1, &impl->texCache.mrrDefault);
    glDeleteTextures(1, &impl->texCache.normalDefault);
    glDeleteTextures(1, &impl->texCache.emissiveDefault);
    glDeleteTextures(1, &impl->texCache.envMap);
    if (impl->scene.keyRotations.vbo)
        glDeleteBuffers(1, &impl->scene.keyRotations.vbo);
    for (auto &[meshIdx, inst] : impl->scene.instancedMeshes) {
        if (inst.vbo) glDeleteBuffers(1, &inst.vbo);
    }
    for (auto &mesh : impl->scene.meshes) {
        for (auto &prim : mesh.primitives) {
            if (prim.vbo) glDeleteBuffers(1, &prim.vbo);
            if (prim.ebo) glDeleteBuffers(1, &prim.ebo);
            if (prim.vao) glDeleteVertexArrays(1, &prim.vao);
        }
    }
    if (impl->shadowMap.depthTex) glDeleteTextures(1, &impl->shadowMap.depthTex);
    if (impl->shadowMap.fbo)      glDeleteFramebuffers(1, &impl->shadowMap.fbo);
    if (impl->skyboxVAO)          glDeleteVertexArrays(1, &impl->skyboxVAO);
    if (impl->pbrProg.id)         glDeleteProgram(impl->pbrProg.id);
    if (impl->skyboxProg.id)      glDeleteProgram(impl->skyboxProg.id);
    if (impl->shadowProg.id)      glDeleteProgram(impl->shadowProg.id);
    if (impl->captureProg.id)     glDeleteProgram(impl->captureProg.id);
    impl->initialized = false;
}

} // namespace keys
