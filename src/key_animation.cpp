// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#include "renderer_internal.hpp"

namespace keys {

InstanceInfo createWhiteKeyInstanceBuffer(GLuint rotationVbo, GLintptr rotationOffsetBytes) {
    InstanceInfo info{};
    glGenBuffers(1, &info.vbo);
    info.count       = static_cast<GLsizei>(kWhiteKeys.size());
    info.strideBytes = static_cast<GLsizei>(3 * sizeof(float));
    info.rotationVbo = rotationVbo;
    info.rotationOffset = rotationOffsetBytes;

    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter(-0.4f, 0.4f);

    std::vector<float> data;
    data.reserve(kWhiteKeys.size() * 3);
    for (const auto &k : kWhiteKeys) {
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
    info.count       = static_cast<GLsizei>(kBlackKeys.size());
    info.strideBytes = static_cast<GLsizei>(1 * sizeof(float));
    info.rotationVbo = rotationVbo;
    info.rotationOffset = rotationOffsetBytes;
    glBindBuffer(GL_ARRAY_BUFFER, info.vbo);
    glBufferData(GL_ARRAY_BUFFER, kBlackKeys.size() * sizeof(float), kBlackKeys.data(), GL_STATIC_DRAW);
    return info;
}

void bindInstanceAttributes(GLuint vao, const InstanceInfo &inst) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, inst.vbo);
    if (inst.strideBytes >= static_cast<GLsizei>(3 * sizeof(float))) {
        // White keys: vec2 (cut widths) + float (center)
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, inst.strideBytes, reinterpret_cast<void *>(0));
        glVertexAttribDivisor(5, 1);
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, inst.strideBytes,
                              reinterpret_cast<void *>(2 * sizeof(float)));
        glVertexAttribDivisor(6, 1);
    } else {
        // Black keys: center only
        glDisableVertexAttribArray(5);
        glVertexAttribDivisor(5, 0);
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, inst.strideBytes, reinterpret_cast<void *>(0));
        glVertexAttribDivisor(6, 1);
    }
    if (inst.rotationVbo != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, inst.rotationVbo);
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(float),
                              reinterpret_cast<void *>(inst.rotationOffset));
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
    constexpr int kFirstMidi = 21;
    constexpr int kLastMidi  = kFirstMidi + 87;
    for (int pitch = kFirstMidi; pitch <= kLastMidi && pitch < static_cast<int>(map.size()); ++pitch) {
        int mod     = pitch % 12;
        bool isBlack = (mod == 1 || mod == 3 || mod == 6 || mod == 8 || mod == 10);
        if (isBlack) {
            if (blackIdx < static_cast<int>(kBlackKeys.size()))
                map[pitch] = KeyMapping{false, blackIdx++};
        } else {
            if (whiteIdx < static_cast<int>(kWhiteKeys.size()))
                map[pitch] = KeyMapping{true, whiteIdx++};
        }
    }
    if (whiteIdx != static_cast<int>(kWhiteKeys.size()) || blackIdx != static_cast<int>(kBlackKeys.size())) {
        log::warning(log::format("[KeyMap] Warning: mismatched key counts. white=", whiteIdx, "/",
                                 kWhiteKeys.size(), " black=", blackIdx, "/", kBlackKeys.size()));
    }
    return map;
}

int rotationIndexForKey(bool isWhite, int keyIndex) {
    return isWhite ? keyIndex : static_cast<int>(kWhiteKeys.size()) + keyIndex;
}

void setKeyRotation(SceneGPU &scene, int midiPitch, float radians) {
    if (midiPitch < 0 || midiPitch >= static_cast<int>(scene.pitchToKey.size()))
        return;
    const auto &ref = scene.pitchToKey[midiPitch];
    if (!ref)
        return;
    int idx = rotationIndexForKey(ref->isWhite, ref->index);
    if (idx < 0 || idx >= static_cast<int>(scene.keyRotations.data.size()))
        return;
    scene.keyRotations.data[static_cast<std::size_t>(idx)] = radians;
    scene.keyRotationsDirty = true;
}

void uploadKeyRotations(SceneGPU &scene) {
    if (scene.keyRotations.vbo == 0 || scene.keyRotations.data.empty())
        return;
    glBindBuffer(GL_ARRAY_BUFFER, scene.keyRotations.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    scene.keyRotations.data.size() * sizeof(float),
                    scene.keyRotations.data.data());
    scene.keyRotationsDirty = false;
}

float getKeyRotation(const SceneGPU &scene, int midiPitch) {
    if (midiPitch < 0 || midiPitch >= static_cast<int>(scene.pitchToKey.size()))
        return 0.0f;
    const auto &ref = scene.pitchToKey[midiPitch];
    if (!ref)
        return 0.0f;
    int idx = rotationIndexForKey(ref->isWhite, ref->index);
    if (idx < 0 || idx >= static_cast<int>(scene.keyRotations.data.size()))
        return 0.0f;
    return scene.keyRotations.data[static_cast<std::size_t>(idx)];
}

KeyboardBounds computeKeyboardBounds() {
    float minCenter = std::numeric_limits<float>::max();
    float maxCenter = std::numeric_limits<float>::lowest();
    for (const auto &k : kWhiteKeys) {
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
    b.spanZ   = 0.01f * (maxCenter - minCenter) + 0.5f;
    return b;
}

static float easeOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float inv = t - 1.0f;
    return 1.0f + c3 * inv * inv * inv + c1 * inv * inv;
}

static float easeOutElastic(float t) {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    const float c4 = (2.0f * 3.14159265f) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

static void startAnimation(SceneGPU &scene, int midiPitch, float target, float duration, EaseType ease) {
    if (midiPitch < 0 || midiPitch >= static_cast<int>(scene.pitchToKey.size()))
        return;
    if (!scene.pitchToKey[midiPitch])
        return;

    scene.animations.erase(
        std::remove_if(scene.animations.begin(), scene.animations.end(),
                       [&](const KeyAnimation &a) { return a.midiPitch == midiPitch; }),
        scene.animations.end());

    KeyAnimation anim{};
    anim.midiPitch = midiPitch;
    anim.start     = getKeyRotation(scene, midiPitch);
    anim.target    = target;
    anim.duration  = duration;
    anim.elapsed   = 0.0f;
    anim.ease      = ease;
    scene.animations.push_back(anim);
}

void midiNoteOn(SceneGPU &scene, int midiPitch) {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter(-0.1f, 0.1f);
    float pressAngle = glm::radians(1.65f + jitter(rng));
    constexpr float pressDuration = 0.12f;
    startAnimation(scene, midiPitch, pressAngle, pressDuration, EaseType::Back);
}

void midiNoteOff(SceneGPU &scene, int midiPitch) {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter(-0.05f, 0.05f);
    float releaseAngle = glm::radians(jitter(rng));
    constexpr float releaseDuration = 0.6f;
    startAnimation(scene, midiPitch, releaseAngle, releaseDuration, EaseType::Elastic);
}

void updateAnimations(SceneGPU &scene, float dt) {
    if (scene.animations.empty())
        return;
    for (auto &anim : scene.animations) {
        anim.elapsed += dt;
        float t     = anim.duration > 0.0f ? glm::clamp(anim.elapsed / anim.duration, 0.0f, 1.0f) : 1.0f;
        float eased = (anim.ease == EaseType::Back) ? easeOutBack(t) : easeOutElastic(t);
        setKeyRotation(scene, anim.midiPitch, anim.start + (anim.target - anim.start) * eased);
    }
    scene.animations.erase(
        std::remove_if(scene.animations.begin(), scene.animations.end(),
                       [](const KeyAnimation &a) { return a.elapsed >= a.duration; }),
        scene.animations.end());
}

} // namespace keys
