// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#include "renderer_internal.hpp"

namespace keys {

CameraState computeViewProj(const SceneGPU &scene, float aspect, float t, bool useKeyboardCamera,
                            bool useMovingCamera) {
    const float fov = glm::radians(30.0f);
    CameraState cam;

    if (useKeyboardCamera) {
        KeyboardBounds kb = computeKeyboardBounds();
        float halfWidth = kb.spanZ * 0.5f * KEYBOARD_SPATIAL_PADDING;
        float hFov = 2.0f * std::atan(std::tan(fov * 0.5f) * aspect);
        float dist = halfWidth / std::tan(hFov * 0.5f);
        cam.pos = glm::vec3(dist * CAMERA_DISTANCE_SCALE, kb.spanZ * CAMERA_HEIGHT_RATIO, kb.centerZ);
        cam.view = glm::lookAt(cam.pos, glm::vec3(0.0f, -1.2f, kb.centerZ), glm::vec3(0.0f, 1.0f, 0.0f));
        cam.proj = glm::perspective(fov, aspect, 0.01f, dist + kb.spanZ * 3.0f);
    } else if (useMovingCamera) {
        float camDist = scene.sceneRadius * 1.1f;
        float radial = camDist + std::sin(t * 0.4f) * scene.sceneRadius * 0.1f;
        float height = scene.sceneRadius * (0.35f + 0.15f * std::sin(t * 0.25f));
        cam.pos = scene.sceneCenter + glm::vec3(std::cos(t * 0.35f) * radial, height, std::sin(t * 0.35f) * radial);
        cam.view = glm::lookAt(cam.pos, scene.sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));
        cam.proj = glm::perspective(glm::radians(50.0f), aspect, 0.01f, camDist * 6.0f);
    } else if (scene.camera.valid) {
        cam.view = scene.camera.view;
        cam.pos = glm::vec3(scene.camera.world[3]);
        float a = scene.camera.aspect.value_or(aspect);
        cam.proj = glm::perspective(scene.camera.yfov, a, scene.camera.znear, scene.camera.zfar);
    } else {
        float camDist = scene.sceneRadius * 0.85f;
        cam.pos = scene.sceneCenter +
                  glm::vec3(std::sin(t * 0.3f) * camDist, scene.sceneRadius * 0.6f, std::cos(t * 0.3f) * camDist);
        cam.view = glm::lookAt(cam.pos, scene.sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));
        cam.proj = glm::perspective(glm::radians(50.0f), aspect, 0.01f, camDist * 6.0f);
    }
    return cam;
}

glm::vec3 computeSwayedLightDir(float t) {
    float swayYaw = glm::radians(4.0f) * std::sin(t * 0.25f);
    float swayPitch = glm::radians(3.0f) * std::sin(t * 0.18f);
    glm::vec3 base = glm::normalize(glm::vec3(-7.3f, 9.0f, 4.6f));
    glm::mat4 sway = glm::rotate(glm::mat4(1.0f), swayYaw, glm::vec3(0, 1, 0));
    sway = glm::rotate(sway, swayPitch, glm::vec3(1, 0, 0));
    return glm::normalize(glm::vec3(sway * glm::vec4(base, 0.0f)));
}

GLuint createSkyboxVAO() {
    static const float verts[] = {
        -1, 1,  -1, -1, -1, -1, 1,  -1, -1, 1,  -1, -1, 1,  1,  -1, -1, 1,  -1, // back
        -1, -1, 1,  -1, -1, -1, -1, 1,  -1, -1, 1,  -1, -1, 1,  1,  -1, -1, 1,  // left
        1,  -1, -1, 1,  -1, 1,  1,  1,  1,  1,  1,  1,  1,  1,  -1, 1,  -1, -1, // right
        -1, -1, 1,  -1, 1,  1,  1,  1,  1,  1,  1,  1,  1,  -1, 1,  -1, -1, 1,  // front
        -1, 1,  -1, 1,  1,  -1, 1,  1,  1,  1,  1,  1,  -1, 1,  1,  -1, 1,  -1, // top
        -1, -1, -1, -1, -1, 1,  1,  -1, -1, 1,  -1, -1, -1, -1, 1,  1,  -1, 1   // bottom
    };
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void *>(0));
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
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        log::error("Shadow FBO incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return s;
}

LightViewData computeLightView(const SceneGPU &scene, const glm::vec3 &lightDir, int shadowMapSize) {
    LightViewData out{};
    glm::vec3 dir = glm::normalize(lightDir);
    glm::vec3 target = scene.sceneCenter;
    float radius = std::max(scene.sceneRadius, 1.0f);
    float dist = radius * 3.5f;
    glm::vec3 eye = target - dir * dist;
    glm::vec3 up = glm::abs(glm::dot(dir, glm::vec3(0, 1, 0))) > 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    glm::mat4 view = glm::lookAt(eye, target, up);

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
    for (const auto &c : corners) {
        glm::vec3 ls = glm::vec3(view * glm::vec4(c, 1.0f));
        minLS = glm::min(minLS, ls);
        maxLS = glm::max(maxLS, ls);
    }

    glm::vec3 extent = (maxLS - minLS) * 0.5f;
    extent = glm::max(extent, glm::vec3(0.5f));
    extent *= 1.05f;
    glm::vec3 center = (maxLS + minLS) * 0.5f;

    // Snap to texel size to reduce shadow shimmer.
    float texelSizeX = (extent.x * 2.0f) / static_cast<float>(std::max(1, shadowMapSize));
    float texelSizeY = (extent.y * 2.0f) / static_cast<float>(std::max(1, shadowMapSize));
    center.x = std::floor(center.x / texelSizeX) * texelSizeX;
    center.y = std::floor(center.y / texelSizeY) * texelSizeY;

    glm::vec3 minSnap = center - extent;
    glm::vec3 maxSnap = center + extent;
    float nearPlane = std::max(0.1f, -maxSnap.z);
    float farPlane = -minSnap.z + 20.0f;
    if (farPlane <= nearPlane + 0.1f)
        farPlane = nearPlane + 50.0f;

    glm::mat4 proj = glm::ortho(minSnap.x, maxSnap.x, minSnap.y, maxSnap.y, nearPlane, farPlane);
    out.vp = proj * view;
    out.eye = eye;
    out.dir = dir;
    out.orthoRadius = std::max(extent.x, extent.y);
    return out;
}

static void renderShadowPass(const SceneGPU &scene, const ShadowProgram &prog, const ShadowMap &shadowMap) {
    glViewport(0, 0, shadowMap.size, shadowMap.size);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_CULL_FACE);
    glUseProgram(prog.id);
    glUniformMatrix4fv(prog.locLightVP, 1, GL_FALSE, glm::value_ptr(shadowMap.lightVP));

    for (const auto &dc : scene.drawCommands) {
        auto instIt = scene.instancedMeshes.find(dc.meshIndex);
        const InstanceInfo *inst = (instIt != scene.instancedMeshes.end()) ? &instIt->second : nullptr;
        for (const auto &prim : scene.meshes[dc.meshIndex].primitives) {
            glBindVertexArray(prim.vao);
            glUniformMatrix4fv(prog.locModel, 1, GL_FALSE, glm::value_ptr(dc.model));
            if (inst)
                glDrawElementsInstanced(prim.mode, prim.indexCount, GL_UNSIGNED_INT, nullptr, inst->count);
            else
                glDrawElements(prim.mode, prim.indexCount, GL_UNSIGNED_INT, nullptr);
        }
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void renderSkybox(const SkyboxProgram &prog, GLuint skyboxVAO, GLuint envMap, const glm::mat4 &view,
                         const glm::mat4 &proj) {
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glUseProgram(prog.id);
    glUniformMatrix4fv(prog.locView, 1, GL_FALSE, glm::value_ptr(glm::mat4(glm::mat3(view))));
    glUniformMatrix4fv(prog.locProj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix3fv(prog.locEnvRot, 1, GL_FALSE, glm::value_ptr(glm::mat3(1.0f)));
    glUniform1i(prog.locCubemap, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envMap);
    glBindVertexArray(skyboxVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

static glm::mat4 makeReflectionMatrix(const glm::vec3 &normal, const glm::vec3 &point) {
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

void renderFrame(SceneGPU &scene, const GLProgram &pbrProgram, const SkyboxProgram &skyboxProgram,
                 const ShadowProgram &shadowProgram, ShadowMap &shadowMap, GLuint skyboxVAO, const FrameParams &frame,
                 bool useMovingCamera, bool useKeyboardCamera) {
    int fbWidth = std::max(frame.fbWidth, 1);
    int fbHeight = std::max(frame.fbHeight, 1);

    double now = frame.timeSeconds;
    if (scene.lastTime == 0.0)
        scene.lastTime = now;
    float dt = static_cast<float>(now - scene.lastTime);
    scene.lastTime = now;
    float t = static_cast<float>(now);

    float aspect = static_cast<float>(fbWidth) / static_cast<float>(fbHeight);
    CameraState cam = computeViewProj(scene, aspect, t, useKeyboardCamera, useMovingCamera);

    updateAnimations(scene, dt);
    if (scene.keyRotationsDirty)
        uploadKeyRotations(scene);

    glm::vec3 lightDir = computeSwayedLightDir(t);
    LightViewData lv = computeLightView(scene, -lightDir, shadowMap.size);
    shadowMap.lightVP = lv.vp;

    renderShadowPass(scene, shadowProgram, shadowMap);

    glViewport(0, 0, fbWidth, fbHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glDisable(GL_STENCIL_TEST);
    renderSkybox(skyboxProgram, skyboxVAO, scene.envMap, cam.view, cam.proj);

    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClear(GL_STENCIL_BUFFER_BIT);

    glUseProgram(pbrProgram.id);
    glUniformMatrix4fv(pbrProgram.locView, 1, GL_FALSE, glm::value_ptr(cam.view));
    glUniformMatrix4fv(pbrProgram.locProj, 1, GL_FALSE, glm::value_ptr(cam.proj));
    glUniformMatrix4fv(pbrProgram.locLightVP, 1, GL_FALSE, glm::value_ptr(shadowMap.lightVP));
    glUniform3fv(pbrProgram.locCamPos, 1, glm::value_ptr(cam.pos));
    glUniform3fv(glGetUniformLocation(pbrProgram.id, "uLightDir"), 1, glm::value_ptr(lightDir));
    glUniform1i(glGetUniformLocation(pbrProgram.id, "uBaseColorTex"), 0);
    glUniform1i(glGetUniformLocation(pbrProgram.id, "uMRRTex"), 1);
    glUniform1i(glGetUniformLocation(pbrProgram.id, "uNormalTex"), 2);
    glUniform1i(glGetUniformLocation(pbrProgram.id, "uEmissiveTex"), 3);
    glUniform1i(pbrProgram.locEnvMap, 4);
    glUniform1i(pbrProgram.locShadowMap, 5);
    glUniform1f(pbrProgram.locEnvIntensity, ENVIRONMENT_INTENSITY);
    glUniform1f(pbrProgram.locShadowBias, SHADOW_BIAS);
    glUniform1i(pbrProgram.locShadowDebug, 0);
    glUniformMatrix3fv(pbrProgram.locEnvRot, 1, GL_FALSE, glm::value_ptr(glm::mat3(1.0f)));
    glUniform3f(glGetUniformLocation(pbrProgram.id, "uAmbientColor"), 0.15f, 0.13f, 0.12f);
    glUniform1f(glGetUniformLocation(pbrProgram.id, "uLightIntensity"), LIGHT_INTENSITY);
    glUniform1f(pbrProgram.locDarken, 1.0f);
    if (scene.backPlaneNormalWS && scene.backPlanePointWS) {
        glUniform3fv(pbrProgram.locGroundNormal, 1, glm::value_ptr(*scene.backPlaneNormalWS));
        glUniform3fv(pbrProgram.locGroundPoint, 1, glm::value_ptr(*scene.backPlanePointWS));
        glUniform2f(pbrProgram.locGroundParams, scene.sceneRadius * 0.03f, 0.35f);
    } else {
        glUniform3f(pbrProgram.locGroundNormal, 0, 0, 0);
        glUniform3f(pbrProgram.locGroundPoint, 0, 0, 0);
        glUniform2f(pbrProgram.locGroundParams, 0, 0);
    }
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, shadowMap.depthTex);

    auto drawDC = [&](const DrawCommand &dc, const glm::mat4 &modelOverride, bool writeStencil, bool disableCull) {
        auto instIt = scene.instancedMeshes.find(dc.meshIndex);
        const InstanceInfo *inst = (instIt != scene.instancedMeshes.end()) ? &instIt->second : nullptr;

        for (const auto &prim : scene.meshes[dc.meshIndex].primitives) {
            if (disableCull || prim.material.doubleSided)
                glDisable(GL_CULL_FACE);
            else {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            glStencilMask(writeStencil ? 0xFF : 0x00);
            glStencilOp(GL_KEEP, GL_KEEP, writeStencil ? GL_REPLACE : GL_KEEP);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, prim.material.baseColorTex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, prim.material.mrTex);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, prim.material.normalTex);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, prim.material.emissiveTex);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_CUBE_MAP, scene.envMap);

            glUniform4fv(pbrProgram.locBaseColor, 1, glm::value_ptr(prim.material.baseColorFactor));
            glUniform1f(pbrProgram.locMetallic, prim.material.metallicFactor);
            glUniform1f(pbrProgram.locRoughness, prim.material.roughnessFactor);
            glUniform3fv(pbrProgram.locEmissive, 1, glm::value_ptr(prim.material.emissiveFactor));
            glUniform1i(pbrProgram.locUnlit, prim.material.unlit ? 1 : 0);

            glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(modelOverride)));
            glUniformMatrix4fv(pbrProgram.locModel, 1, GL_FALSE, glm::value_ptr(modelOverride));
            glUniformMatrix3fv(pbrProgram.locNormalMat, 1, GL_FALSE, glm::value_ptr(normalMat));

            glBindVertexArray(prim.vao);
            if (inst)
                glDrawElementsInstanced(prim.mode, prim.indexCount, GL_UNSIGNED_INT, nullptr, inst->count);
            else
                glDrawElements(prim.mode, prim.indexCount, GL_UNSIGNED_INT, nullptr);
        }
    };

    // Pass 1: BackPlane to stencil.
    if (scene.backPlaneMeshIndex) {
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glDepthMask(GL_FALSE);
        for (const auto &dc : scene.drawCommands) {
            if (dc.meshIndex == *scene.backPlaneMeshIndex)
                drawDC(dc, dc.model, true, false);
        }
        glDepthMask(GL_TRUE);
    }

    // Pass 2: mirrored reflection where stencil == 1.
    if (scene.backPlaneMeshIndex && scene.backPlaneModel) {
        glm::vec3 planePoint =
            scene.backPlanePointWS.value_or(glm::vec3(*scene.backPlaneModel * glm::vec4(0, 0, 0, 1)));
        glm::vec3 planeNormal =
            scene.backPlaneNormalWS.value_or(glm::normalize(glm::vec3(*scene.backPlaneModel * glm::vec4(0, 0, 1, 0))));
        glm::mat4 reflect = makeReflectionMatrix(planeNormal, planePoint);
        float planeDist = glm::dot(planeNormal, cam.pos - planePoint);
        glm::vec3 camReflected = cam.pos - 2.0f * planeDist * planeNormal;

        glStencilFunc(GL_EQUAL, 1, 0xFF);
        glStencilMask(0x00);
        glEnable(GL_DEPTH_TEST);
        glUniform1f(pbrProgram.locDarken, 0.0025f);
        glUniform3fv(pbrProgram.locCamPos, 1, glm::value_ptr(camReflected));
        for (const auto &dc : scene.drawCommands) {
            bool isInstanced = scene.instancedMeshes.count(dc.meshIndex) > 0;
            bool isBase = scene.baseMeshIndex && dc.meshIndex == *scene.baseMeshIndex;
            if (!isInstanced && !isBase)
                continue;
            if (scene.backPlaneMeshIndex && dc.meshIndex == *scene.backPlaneMeshIndex)
                continue;
            drawDC(dc, reflect * dc.model, false, true);
        }
        glUniform3fv(pbrProgram.locCamPos, 1, glm::value_ptr(cam.pos));
    }

    // Pass 3: normal scene geometry.
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0x00);
    glUniform1f(pbrProgram.locDarken, 1.0f);
    for (const auto &dc : scene.drawCommands) {
        if (scene.backPlaneMeshIndex && dc.meshIndex == *scene.backPlaneMeshIndex)
            continue;
        drawDC(dc, dc.model, false, false);
    }
}

} // namespace keys
