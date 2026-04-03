// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#include "stb_image.h"
#include "renderer_internal.hpp"

namespace keys {

static std::optional<std::vector<std::byte>> getImageBytes(const fastgltf::Asset &asset,
                                                            const fastgltf::Image &image,
                                                            const fs::path &basePath,
                                                            fastgltf::DefaultBufferDataAdapter &adapter) {
    return std::visit(
        fastgltf::visitor{
            [&](const fastgltf::sources::Array &array) -> std::optional<std::vector<std::byte>> {
                std::vector<std::byte> out(array.bytes.size());
                std::memcpy(out.data(), array.bytes.data(), array.bytes.size());
                return out;
            },
            [&](const fastgltf::sources::ByteView &view) -> std::optional<std::vector<std::byte>> {
                std::vector<std::byte> out(view.bytes.size());
                std::memcpy(out.data(), view.bytes.data(), view.bytes.size());
                return out;
            },
            [&](const fastgltf::sources::BufferView &viewRef) -> std::optional<std::vector<std::byte>> {
                auto view = adapter(asset, viewRef.bufferViewIndex);
                std::vector<std::byte> out(view.size());
                std::memcpy(out.data(), view.data(), view.size());
                return out;
            },
            [&](const fastgltf::sources::URI &uri) -> std::optional<std::vector<std::byte>> {
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
                std::vector<char> tmp((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
                std::vector<std::byte> bytes(tmp.size());
                std::memcpy(bytes.data(), tmp.data(), tmp.size());
                return bytes;
            },
            [](auto &) -> std::optional<std::vector<std::byte>> { return std::nullopt; }},
        image.data);
}

static GLuint uploadImageTexture(const fastgltf::Asset &asset, std::size_t imageIndex,
                                  const fs::path &basePath, TextureCache &cache, bool srgb,
                                  fastgltf::DefaultBufferDataAdapter &adapter) {
    auto it = cache.imageTextures.find(imageIndex);
    if (it != cache.imageTextures.end())
        return it->second;

    if (imageIndex >= asset.images.size()) {
        log::error(log::format("Image index ", imageIndex, " out of bounds (max: ",
                               asset.images.size(), ")"));
        return cache.white;
    }

    const auto &image = asset.images[imageIndex];
    auto bytesOpt = getImageBytes(asset, image, basePath, adapter);
    int w = 1, h = 1, comp = 4;
    stbi_uc *data = nullptr;
    if (bytesOpt) {
        data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(bytesOpt->data()),
                                     static_cast<int>(bytesOpt->size()), &w, &h, &comp, 0);
    }
    if (!data) {
        log::warning(log::format("Warning: failed to load texture ", imageIndex, ", fallback."));
        return cache.white;
    }

    GLenum format        = GL_RGBA;
    GLint internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    if (comp == 1) {
        format        = GL_RED;
        internalFormat = GL_R8;
    } else if (comp == 3) {
        format        = GL_RGB;
        internalFormat = srgb ? GL_SRGB8 : GL_RGB8;
    }

    GLuint tex = createTexture2D(w, h, internalFormat, format, data, true);
    stbi_image_free(data);
    cache.imageTextures[imageIndex] = tex;
    return tex;
}

static GLuint resolveTexture(const fastgltf::Asset &asset, const fastgltf::TextureInfo &texInfo,
                              const fs::path &basePath, TextureCache &cache, bool srgb,
                              fastgltf::DefaultBufferDataAdapter &adapter, GLuint fallback) {
    if (texInfo.textureIndex >= asset.textures.size())
        return fallback;
    const auto &tex = asset.textures[texInfo.textureIndex];
    if (!tex.imageIndex)
        return fallback;
    return uploadImageTexture(asset, *tex.imageIndex, basePath, cache, srgb, adapter);
}

static glm::mat4 toMat4(const fastgltf::TRS &trs) {
    glm::mat4 T = glm::translate(glm::mat4(1.0f),
                                  glm::vec3(trs.translation[0], trs.translation[1], trs.translation[2]));
    glm::quat q(trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]);
    glm::mat4 R = glm::toMat4(q);
    glm::mat4 S = glm::scale(glm::mat4(1.0f),
                              glm::vec3(trs.scale[0], trs.scale[1], trs.scale[2]));
    return T * R * S;
}

static glm::mat4 nodeTransform(const fastgltf::Node &node) {
    if (std::holds_alternative<fastgltf::TRS>(node.transform))
        return toMat4(std::get<fastgltf::TRS>(node.transform));
    const auto &m = std::get<fastgltf::math::fmat4x4>(node.transform);
    return glm::make_mat4(m.data());
}

static PrimitiveGPU uploadPrimitive(const fastgltf::Asset &asset, const fastgltf::Primitive &prim,
                                     const fs::path &basePath, TextureCache &cache,
                                     fastgltf::DefaultBufferDataAdapter &adapter) {
    PrimitiveGPU gpuPrim{};

    std::ostringstream attrLog;
    attrLog << "Prim attributes: ";
    for (const auto &attr : prim.attributes)
        attrLog << attr.name << " ";

    auto posIt = prim.findAttribute("POSITION");
    if (posIt == prim.attributes.end())
        throw std::runtime_error("Primitive ohne POSITION Attribut.");
    const auto &posAcc = asset.accessors[posIt->accessorIndex];

    std::vector<glm::vec3> positions;
    positions.reserve(posAcc.count);
    fastgltf::iterateAccessor<fastgltf::math::fvec3>(
        asset, posAcc, [&](auto v) { positions.emplace_back(v[0], v[1], v[2]); }, adapter);

    auto normIt = prim.findAttribute("NORMAL");
    std::vector<glm::vec3> normals;
    if (normIt != prim.attributes.end()) {
        const auto &normAcc = asset.accessors[normIt->accessorIndex];
        normals.reserve(normAcc.count);
        fastgltf::iterateAccessor<fastgltf::math::fvec3>(
            asset, normAcc, [&](auto v) { normals.emplace_back(v[0], v[1], v[2]); }, adapter);
    } else {
        normals.assign(positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    auto uvIt = prim.findAttribute("TEXCOORD_0");
    std::vector<glm::vec2> uvs;
    if (uvIt != prim.attributes.end()) {
        const auto &uvAcc = asset.accessors[uvIt->accessorIndex];
        uvs.reserve(uvAcc.count);
        fastgltf::iterateAccessor<fastgltf::math::fvec2>(
            asset, uvAcc, [&](auto v) { uvs.emplace_back(v[0], v[1]); }, adapter);
    } else {
        uvs.assign(positions.size(), glm::vec2(0.0f));
    }

    auto tanIt = prim.findAttribute("TANGENT");
    std::vector<glm::vec4> tangents;
    if (tanIt != prim.attributes.end()) {
        const auto &tanAcc = asset.accessors[tanIt->accessorIndex];
        tangents.reserve(tanAcc.count);
        fastgltf::iterateAccessor<fastgltf::math::fvec4>(
            asset, tanAcc, [&](auto v) { tangents.emplace_back(v[0], v[1], v[2], v[3]); }, adapter);
    } else {
        tangents.assign(positions.size(), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    }

    std::vector<float> cutValues(positions.size(), 0.0f);
    if (auto cutIt = prim.findAttribute("_CUT"); cutIt != prim.attributes.end()) {
        const auto &cutAcc = asset.accessors[cutIt->accessorIndex];
        if (cutAcc.type != fastgltf::AccessorType::Scalar) {
            log::warning(log::format("Warning: _CUT accessor is not a Scalar (Type=",
                                     static_cast<int>(cutAcc.type), ")"));
        } else {
            std::size_t idx = 0;
            fastgltf::iterateAccessor<float>(
                asset, cutAcc,
                [&](auto v) { if (idx < cutValues.size()) cutValues[idx] = v; ++idx; },
                adapter);
            if (cutAcc.count != positions.size()) {
                log::warning(log::format("Warning: _CUT count (", cutAcc.count,
                                         ") != positions count (", positions.size(), ")"));
            }
        }
    }

    std::vector<std::uint32_t> indices;
    if (prim.indicesAccessor.has_value()) {
        const auto &idxAcc = asset.accessors[*prim.indicesAccessor];
        indices.reserve(idxAcc.count);
        fastgltf::iterateAccessor<std::uint32_t>(
            asset, idxAcc, [&](auto v) { indices.push_back(static_cast<std::uint32_t>(v)); }, adapter);
    } else {
        indices.resize(positions.size());
        std::iota(indices.begin(), indices.end(), 0);
    }

    std::vector<float> vertexData;
    vertexData.reserve(positions.size() * (3 + 3 + 2 + 4 + 1));
    gpuPrim.aabbMin = glm::vec3(std::numeric_limits<float>::max());
    gpuPrim.aabbMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto &p  = positions[i];
        const auto &n  = normals[i];
        const auto &uv = uvs[i];
        const auto &t  = tangents[i];
        gpuPrim.aabbMin = glm::min(gpuPrim.aabbMin, p);
        gpuPrim.aabbMax = glm::max(gpuPrim.aabbMax, p);
        vertexData.insert(vertexData.end(),
                          {p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y,
                           t.x, t.y, t.z, t.w, cutValues[i]});
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
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(8 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(12 * sizeof(float)));

    gpuPrim.indexCount = static_cast<GLsizei>(indices.size());
    switch (prim.type) {
    case fastgltf::PrimitiveType::Points:        gpuPrim.mode = GL_POINTS;         break;
    case fastgltf::PrimitiveType::Lines:         gpuPrim.mode = GL_LINES;          break;
    case fastgltf::PrimitiveType::LineLoop:      gpuPrim.mode = GL_LINE_LOOP;      break;
    case fastgltf::PrimitiveType::LineStrip:     gpuPrim.mode = GL_LINE_STRIP;     break;
    case fastgltf::PrimitiveType::TriangleStrip: gpuPrim.mode = GL_TRIANGLE_STRIP; break;
    case fastgltf::PrimitiveType::TriangleFan:   gpuPrim.mode = GL_TRIANGLE_FAN;   break;
    default:                                     gpuPrim.mode = GL_TRIANGLES;      break;
    }

    const fastgltf::Material *matPtr = nullptr;
    if (!asset.materials.empty()) {
        if (prim.materialIndex && *prim.materialIndex < asset.materials.size())
            matPtr = &asset.materials[*prim.materialIndex];
        else
            matPtr = &asset.materials.front();
    }

    MaterialGPU mgpu{};
    if (matPtr) {
        const auto &mat = *matPtr;
        mgpu.baseColorFactor = glm::vec4(mat.pbrData.baseColorFactor[0], mat.pbrData.baseColorFactor[1],
                                         mat.pbrData.baseColorFactor[2], mat.pbrData.baseColorFactor[3]);
        mgpu.metallicFactor   = static_cast<float>(mat.pbrData.metallicFactor);
        mgpu.roughnessFactor  = static_cast<float>(mat.pbrData.roughnessFactor);
        mgpu.emissiveFactor   = glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2])
                                * static_cast<float>(mat.emissiveStrength);
        mgpu.doubleSided = mat.doubleSided;
        mgpu.unlit       = mat.unlit;

        mgpu.baseColorTex = mat.pbrData.baseColorTexture
            ? resolveTexture(asset, *mat.pbrData.baseColorTexture, basePath, cache, true, adapter, cache.white)
            : cache.white;
        mgpu.mrTex = mat.pbrData.metallicRoughnessTexture
            ? resolveTexture(asset, *mat.pbrData.metallicRoughnessTexture, basePath, cache, false, adapter, cache.mrrDefault)
            : cache.mrrDefault;
        mgpu.normalTex = mat.normalTexture
            ? resolveTexture(asset, *mat.normalTexture, basePath, cache, false, adapter, cache.normalDefault)
            : cache.normalDefault;
        mgpu.emissiveTex = mat.emissiveTexture
            ? resolveTexture(asset, *mat.emissiveTexture, basePath, cache, true, adapter, cache.emissiveDefault)
            : cache.emissiveDefault;

        log::info(log::format(
            "Material: baseColorFactor=[", mgpu.baseColorFactor.r, ",", mgpu.baseColorFactor.g, ",",
            mgpu.baseColorFactor.b, ",", mgpu.baseColorFactor.a, "] metallic=", mgpu.metallicFactor,
            " roughness=", mgpu.roughnessFactor,
            " baseTex=",    (mat.pbrData.baseColorTexture           ? "yes" : "no"),
            " mrTex=",      (mat.pbrData.metallicRoughnessTexture   ? "yes" : "no"),
            " normalTex=",  (mat.normalTexture                      ? "yes" : "no"),
            " emissiveTex=",(mat.emissiveTexture                    ? "yes" : "no"),
            " unlit=",      (mgpu.unlit                             ? "yes" : "no")));
        if (!mat.pbrData.metallicRoughnessTexture) {
            log::info(log::format("Note: No MetallicRoughness-Texture, using roughness=",
                                  mgpu.roughnessFactor, " metallic=", mgpu.metallicFactor));
        }
    } else {
        mgpu.baseColorFactor = glm::vec4(1.0f);
        mgpu.metallicFactor  = 1.0f;
        mgpu.roughnessFactor = 1.0f;
        mgpu.emissiveFactor  = glm::vec3(0.0f);
        mgpu.doubleSided     = false;
        mgpu.unlit           = false;
        mgpu.baseColorTex    = cache.white;
        mgpu.mrTex           = cache.mrrDefault;
        mgpu.normalTex       = cache.normalDefault;
        mgpu.emissiveTex     = cache.emissiveDefault;
    }

    gpuPrim.material = mgpu;
    log::info(log::format(attrLog.str(), " | indices=", indices.size(), " verts=", positions.size()));
    return gpuPrim;
}

static void expandBounds(glm::vec3 &minOut, glm::vec3 &maxOut, const glm::mat4 &m,
                          const glm::vec3 &localMin, const glm::vec3 &localMax) {
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
    for (auto &c : corners) {
        glm::vec3 wc = glm::vec3(m * glm::vec4(c, 1.0f));
        minOut = glm::min(minOut, wc);
        maxOut = glm::max(maxOut, wc);
    }
}

std::optional<LoadedGltf> loadGltfAsset(const fs::path &gltfPath) {
    auto dataResult = fastgltf::GltfDataBuffer::FromPath(gltfPath);
    if (!dataResult) {
        log::error(log::format("Failed to load glTF: ", gltfPath, " (Error ",
                               static_cast<int>(dataResult.error()), ")"));
        return std::nullopt;
    }

    LoadedGltf loaded;
    loaded.buffer = std::move(dataResult.get());

    fastgltf::Parser parser(fastgltf::Extensions::KHR_mesh_quantization |
                            fastgltf::Extensions::KHR_texture_transform |
                            fastgltf::Extensions::KHR_materials_variants);

    auto asset = parser.loadGltf(loaded.buffer, gltfPath.parent_path(),
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

void printAssetSummary(const fastgltf::Asset &asset, const fs::path &gltfPath) {
    log::info(log::format("Loaded: ",    gltfPath));
    log::info(log::format("Scenes:   ",  asset.scenes.size()));
    log::info(log::format("Nodes:    ",  asset.nodes.size()));
    log::info(log::format("Meshes:   ",  asset.meshes.size()));
    log::info(log::format("Materials:", asset.materials.size()));
    log::info(log::format("Images:   ",  asset.images.size()));
}

SceneGPU buildSceneGPU(const fastgltf::Asset &asset, const fs::path &basePath,
                        TextureCache &texCache, fastgltf::DefaultBufferDataAdapter &adapter) {
    SceneGPU scene{};
    scene.meshes.reserve(asset.meshes.size());

    std::optional<std::size_t> whiteKeyMeshIndex;
    std::optional<std::size_t> blackKeyMeshIndex;
    std::optional<std::size_t> backPlaneMeshIndex;
    std::optional<std::size_t> baseMeshIndex;
    std::optional<glm::mat4>   whiteKeyModel;
    std::optional<glm::mat4>   blackKeyModel;
    std::optional<glm::mat4>   backPlaneModel;
    std::optional<std::size_t> preferredCameraIndex;

    for (std::size_t i = 0; i < asset.cameras.size(); ++i) {
        if (asset.cameras[i].name == "Camera") {
            preferredCameraIndex = i;
            break;
        }
    }
    for (std::size_t i = 0; i < asset.meshes.size(); ++i) {
        if (asset.meshes[i].name == "WhiteKey")  whiteKeyMeshIndex  = i;
        if (asset.meshes[i].name == "BlackKey")  blackKeyMeshIndex  = i;
        if (asset.meshes[i].name == "BackPlane") backPlaneMeshIndex = i;
        if (asset.meshes[i].name == "Base")      baseMeshIndex      = i;
    }

    for (const auto &mesh : asset.meshes) {
        MeshGPU gpuMesh{};
        gpuMesh.primitives.reserve(mesh.primitives.size());
        for (const auto &prim : mesh.primitives)
            gpuMesh.primitives.push_back(uploadPrimitive(asset, prim, basePath, texCache, adapter));
        scene.meshes.push_back(std::move(gpuMesh));
    }

    std::size_t sceneIndex = asset.defaultScene.value_or(0);
    if (sceneIndex >= asset.scenes.size()) sceneIndex = 0;

    std::unordered_set<std::size_t> skipMeshes;
    if (whiteKeyMeshIndex) skipMeshes.insert(*whiteKeyMeshIndex);
    if (blackKeyMeshIndex) skipMeshes.insert(*blackKeyMeshIndex);

    std::function<void(std::size_t, const glm::mat4 &)> gatherFiltered =
        [&](std::size_t nodeIndex, const glm::mat4 &parent) {
            const auto &node  = asset.nodes[nodeIndex];
            glm::mat4 model   = parent * nodeTransform(node);

            if (node.cameraIndex) {
                bool isPreferred = preferredCameraIndex && (*node.cameraIndex == *preferredCameraIndex);
                if (!scene.camera.valid || isPreferred) {
                    const auto &cam = asset.cameras[*node.cameraIndex];
                    if (std::holds_alternative<fastgltf::Camera::Perspective>(cam.camera)) {
                        const auto &p = std::get<fastgltf::Camera::Perspective>(cam.camera);
                        scene.camera.world  = model;
                        scene.camera.view   = glm::inverse(model);
                        scene.camera.yfov   = p.yfov;
                        scene.camera.znear  = p.znear;
                        scene.camera.zfar   = p.zfar.value_or(1000.0f);
                        if (p.aspectRatio.has_value()) scene.camera.aspect = *p.aspectRatio;
                        scene.camera.valid  = true;
                        glm::vec3 eye     = glm::vec3(scene.camera.world[3]);
                        glm::vec3 forward = glm::normalize(glm::vec3(scene.camera.world * glm::vec4(0, 0, -1, 0)));
                        glm::vec3 up      = glm::normalize(glm::vec3(scene.camera.world * glm::vec4(0, 1, 0, 0)));
                        log::info(log::format(
                            "Camera '", cam.name, "' pos=", eye.x, ",", eye.y, ",", eye.z,
                            " forward=", forward.x, ",", forward.y, ",", forward.z,
                            " up=", up.x, ",", up.y, ",", up.z,
                            " fovY(deg)=", glm::degrees(scene.camera.yfov),
                            " aspect=", (scene.camera.aspect
                                ? std::to_string(*scene.camera.aspect) : std::string("fb"))));
                    }
                }
            }

            if (node.meshIndex) {
                if (skipMeshes.count(*node.meshIndex)) {
                    if (whiteKeyMeshIndex && *node.meshIndex == *whiteKeyMeshIndex && !whiteKeyModel)
                        whiteKeyModel = model;
                    if (blackKeyMeshIndex && *node.meshIndex == *blackKeyMeshIndex && !blackKeyModel)
                        blackKeyModel = model;
                } else {
                    if (backPlaneMeshIndex && *node.meshIndex == *backPlaneMeshIndex && !backPlaneModel)
                        backPlaneModel = model;
                    scene.drawCommands.push_back(DrawCommand{model, *node.meshIndex});
                }
            }
            for (auto child : node.children)
                gatherFiltered(child, model);
        };

    for (auto root : asset.scenes[sceneIndex].nodeIndices)
        gatherFiltered(root, glm::mat4(1.0f));

    scene.keyRotations     = createKeyRotationBuffer();
    scene.keyRotationsDirty = true;
    scene.pitchToKey       = buildPitchToKeyMapping();

    const GLintptr whiteRotOffset = 0;
    const GLintptr blackRotOffset = static_cast<GLintptr>(kWhiteKeys.size() * sizeof(float));

    if (whiteKeyMeshIndex) {
        InstanceInfo inst = createWhiteKeyInstanceBuffer(scene.keyRotations.vbo, whiteRotOffset);
        for (auto &prim : scene.meshes[*whiteKeyMeshIndex].primitives)
            bindInstanceAttributes(prim.vao, inst);
        scene.instancedMeshes[*whiteKeyMeshIndex] = inst;
        scene.drawCommands.push_back(DrawCommand{whiteKeyModel.value_or(glm::mat4(1.0f)), *whiteKeyMeshIndex});
    }
    if (blackKeyMeshIndex) {
        InstanceInfo inst = createBlackKeyInstanceBuffer(scene.keyRotations.vbo, blackRotOffset);
        for (auto &prim : scene.meshes[*blackKeyMeshIndex].primitives)
            bindInstanceAttributes(prim.vao, inst);
        scene.instancedMeshes[*blackKeyMeshIndex] = inst;
        scene.drawCommands.push_back(DrawCommand{blackKeyModel.value_or(glm::mat4(1.0f)), *blackKeyMeshIndex});
    }

    glm::vec3 sceneMin = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 sceneMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (const auto &dc : scene.drawCommands) {
        const auto &mesh = scene.meshes[dc.meshIndex];
        for (const auto &prim : mesh.primitives)
            expandBounds(sceneMin, sceneMax, dc.model, prim.aabbMin, prim.aabbMax);
    }
    if (!scene.drawCommands.empty()) {
        scene.sceneCenter = (sceneMin + sceneMax) * 0.5f;
        scene.sceneRadius = glm::length(sceneMax - sceneMin) * 0.5f;
        if (scene.sceneRadius < 0.001f) scene.sceneRadius = 1.0f;
    }
    scene.sceneMin         = sceneMin;
    scene.sceneMax         = sceneMax;
    scene.backPlaneMeshIndex = backPlaneMeshIndex;
    scene.backPlaneModel   = backPlaneModel;

    if (backPlaneMeshIndex && backPlaneModel) {
        glm::vec3 localMin(std::numeric_limits<float>::max());
        glm::vec3 localMax(std::numeric_limits<float>::lowest());
        for (const auto &prim : scene.meshes[*backPlaneMeshIndex].primitives) {
            localMin = glm::min(localMin, prim.aabbMin);
            localMax = glm::max(localMax, prim.aabbMax);
        }
        glm::vec3 extent = localMax - localMin;
        glm::vec3 localNormal(0, 0, 1);
        float minExtent = std::numeric_limits<float>::max();
        int axis = 2;
        for (int i = 0; i < 3; ++i) {
            if (extent[i] < minExtent) { minExtent = extent[i]; axis = i; }
        }
        if (axis == 0)      localNormal = glm::vec3(1, 0, 0);
        else if (axis == 1) localNormal = glm::vec3(0, 1, 0);
        else                localNormal = glm::vec3(0, 0, 1);

        glm::vec3 localPoint  = (localMin + localMax) * 0.5f;
        glm::vec3 worldNormal = glm::normalize(glm::vec3(*backPlaneModel * glm::vec4(localNormal, 0.0f)));
        glm::vec3 worldPoint  = glm::vec3(*backPlaneModel * glm::vec4(localPoint, 1.0f));
        scene.backPlaneNormalWS = worldNormal;
        scene.backPlanePointWS  = worldPoint;
    }

    scene.baseMeshIndex = baseMeshIndex;
    scene.envMap        = texCache.envMap;
    return scene;
}

} // namespace keys
