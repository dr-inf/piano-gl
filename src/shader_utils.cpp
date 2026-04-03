// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#include "renderer_internal.hpp"

namespace keys {

GLuint compileShader(GLenum type, std::string_view src, std::string_view name) {
    GLuint shader = glCreateShader(type);
    const char *csrc = src.data();
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

GLuint createProgram(std::string_view vertSrc, std::string_view fragSrc, std::string_view vertName,
                     std::string_view fragName) {
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

} // namespace keys
