// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "renderer_internal.hpp"

namespace keys {

GLuint createTexture2D(int width, int height, GLint internalFormat, GLenum format,
                       const void *data, bool generateMips) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    generateMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    if (generateMips)
        glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}

GLuint createFallbackTexture(glm::vec4 color, bool srgb) {
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
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_SRGB8, 1, 1, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, px.data());
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return tex;
}

GLuint loadEquirectangularAsCubemap(const std::string &hdrPath, int size,
                                    GLuint skyboxVAO, const CaptureProgram &captureProg) {
    stbi_set_flip_vertically_on_load(false);
    int w = 0, h = 0, comp = 0;
    float *data = stbi_loadf(hdrPath.c_str(), &w, &h, &comp, 3);
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
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, size, size, 0,
                     GL_RGB, GL_FLOAT, nullptr);
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

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        log::error(log::format("Framebuffer incomplete for equirect conversion: 0x",
                               std::hex, status, std::dec));
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &hdrTex);
        glDeleteTextures(1, &cubeTex);
        glDeleteRenderbuffers(1, &rbo);
        glDeleteFramebuffers(1, &fbo);
        return 0;
    }

    glm::mat4 captureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[6] = {
        glm::lookAt(glm::vec3(0, 0, 0), glm::vec3( 1,  0,  0), glm::vec3(0, -1,  0)),
        glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(-1,  0,  0), glm::vec3(0, -1,  0)),
        glm::lookAt(glm::vec3(0, 0, 0), glm::vec3( 0,  1,  0), glm::vec3(0,  0,  1)),
        glm::lookAt(glm::vec3(0, 0, 0), glm::vec3( 0, -1,  0), glm::vec3(0,  0, -1)),
        glm::lookAt(glm::vec3(0, 0, 0), glm::vec3( 0,  0,  1), glm::vec3(0, -1,  0)),
        glm::lookAt(glm::vec3(0, 0, 0), glm::vec3( 0,  0, -1), glm::vec3(0, -1,  0)),
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
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubeTex, 0);
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

} // namespace keys
