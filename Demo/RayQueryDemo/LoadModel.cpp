#include "Demo/RayQueryDemo/LoadModel.h"

#include "tiny_gltf_v3.h"
#define STB_IMAGE_IMPLEMENTATION
#include "attic/stb_image.h"

#include <cstdio>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string_view>
#include <unordered_map>
#include <utility>

std::string VertexTypeToString(VertexTypeFlagBits type) {
    switch (type) {
        case VertexTypeFlagBits::POSITION: return "POSITION";
        case VertexTypeFlagBits::NORMAL: return "NORMAL";
        case VertexTypeFlagBits::TEXCOORD: return "TEXCOORD_0";
        default:
            return "";
    }
}

namespace {
    glm::mat4 GetTrans(const tg3_node& node) {
        if (node.has_matrix) {
            // glTF matrix 是列主序 double[16]，转 float 后直接给 glm::make_mat4
            float m[16];
            for (int i = 0; i < 16; ++i) m[i] = static_cast<float>(node.matrix[i]);
            return glm::make_mat4(m);
        }
        // 否则按规范 T * R * S 组合（v3 已自动填好默认值，无需判空）
        glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2])));
        // 注意：glm::quat 构造顺序是 (w, x, y, z)，glTF rotation 是 (x, y, z, w)
        glm::quat q(
            static_cast<float>(node.rotation[3]),
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2]));
        glm::mat4 R = glm::mat4_cast(q);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2])));
        return T * R * S;
    }

    void LoadBuffers(Ccino::Context& context, const tg3_model& gltfModel, Model& modelData) {
        for (uint32_t i = 0; i < gltfModel.buffers_count; i++) {
            const auto& buffer = gltfModel.buffers[i];
            if (buffer.data.data == nullptr || buffer.data.count == 0) {
                modelData.vertexBuffers.push_back({});
                continue;
            }
            Ccino::Buffer::CreateArgs args = {
                .size = static_cast<uint32_t>(buffer.data.count),
                .usage = Ccino::BufferUsage::Storage,
                //None: Copy/Readback 走 Transfer 队列中转; 带 Write 的 Copy、带 Read 的 Readback 直接映射
                .hostAccess = Ccino::RHI::HostAccess::None,
            };
            auto gpuBuffer = context.CreateBuffer(args);
            gpuBuffer->Copy(buffer.data.data);
            gpuBuffer->Flush();
            modelData.vertexBuffers.push_back(std::move(gpuBuffer));
        }
    }

    Ccino::Ref<Ccino::Buffer> CreateIndexBufferForPrimitive(
        Ccino::Context& context,
        const tg3_model& gltfModel,
        const tg3_primitive& gltfPrim,
        uint32_t vertexCount)
    {
        std::vector<uint32_t> indices;
        if (gltfPrim.indices >= 0 && static_cast<uint32_t>(gltfPrim.indices) < gltfModel.accessors_count) {
            const tg3_accessor& acc = gltfModel.accessors[gltfPrim.indices];
            if (acc.buffer_view >= 0 && static_cast<uint32_t>(acc.buffer_view) < gltfModel.buffer_views_count) {
                const tg3_buffer_view& bv = gltfModel.buffer_views[acc.buffer_view];
                const tg3_buffer& buf = gltfModel.buffers[bv.buffer];
                const int32_t stride = tg3_accessor_byte_stride(&acc, &bv);
                if (buf.data.data != nullptr && stride > 0) {
                    const uint8_t* base = buf.data.data + bv.byte_offset + acc.byte_offset;
                    indices.resize(static_cast<size_t>(acc.count));
                    for (uint64_t i = 0; i < acc.count; ++i) {
                        const uint8_t* ptr = base + i * static_cast<uint64_t>(stride);
                        switch (acc.component_type) {
                            case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
                                indices[static_cast<size_t>(i)] = *reinterpret_cast<const uint8_t*>(ptr);
                                break;
                            case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
                                indices[static_cast<size_t>(i)] = *reinterpret_cast<const uint16_t*>(ptr);
                                break;
                            case TG3_COMPONENT_TYPE_UNSIGNED_INT:
                                indices[static_cast<size_t>(i)] = *reinterpret_cast<const uint32_t*>(ptr);
                                break;
                            default:
                                indices[static_cast<size_t>(i)] = 0;
                                break;
                        }
                    }
                }
            }
        } else if (vertexCount > 0) {
            indices.resize(vertexCount);
            for (uint32_t i = 0; i < vertexCount; ++i) {
                indices[i] = i;
            }
        }

        if (indices.empty()) {
            return {};
        }

        Ccino::Buffer::CreateArgs args = {
            .size = static_cast<uint32_t>(indices.size() * sizeof(uint32_t)),
            .usage = Ccino::BufferUsage::Storage,
            .hostAccess = Ccino::RHI::HostAccess::None,
        };
        auto gpuIndexBuffer = context.CreateBuffer(args);
        gpuIndexBuffer->Copy(indices.data());
        gpuIndexBuffer->Flush();
        return gpuIndexBuffer;
    }

    int FindAttributeAccessor(const tg3_primitive& prim, const char* attrName) {
        for (uint32_t i = 0; i < prim.attributes_count; ++i) {
            if (tg3_str_equals_cstr(prim.attributes[i].key, attrName)) {
                return prim.attributes[i].value;
            }
        }
        return TG3_INDEX_NONE; // -1
    }

    bool GetAccessorView(const tg3_model& model, int accessorIdx, glm::ivec4& bufferView) {
        if (accessorIdx < 0) return false;
        const tg3_accessor& acc = model.accessors[accessorIdx];
        if (acc.buffer_view < 0) return false; // 规范允许无 bufferView（代表全 0 数据或稀疏数据）
        const tg3_buffer_view& bv = model.buffer_views[acc.buffer_view];
        bufferView[0] = bv.buffer;
        bufferView[1] = tg3_accessor_byte_stride(&acc, &bv); // 自动处理 bv.byte_stride == 0
        bufferView[2] = static_cast<int>(bv.byte_offset + acc.byte_offset);
        bufferView[3] = static_cast<int>(acc.count);
        if (bufferView[1] <= 0) {
            bufferView[0] = -1; //这是用来标记当前的顶点属性空
        }

        return bufferView[1] > 0;
    }

    std::vector<uint8_t> DecodeBase64(std::string_view input) {
        static constexpr int8_t kTable[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        };
        std::vector<uint8_t> out;
        out.reserve((input.size() * 3) / 4);
        int val = 0;
        int valb = -8;
        for (unsigned char c : input) {
            if (c == '=') break;
            const int8_t d = kTable[c];
            if (d == -1) continue;
            val = (val << 6) + d;
            valb += 6;
            if (valb >= 0) {
                out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    int LoadGLTFTexture(
        Ccino::Context& context,
        const tg3_model& gltfModel,
        Model& modelData,
        const std::filesystem::path& gltfDir,
        std::unordered_map<uint64_t, int>& textureCache,
        int32_t gltfTexIndex,
        Ccino::TextureFormat format)
    {
        if (gltfTexIndex < 0 || static_cast<uint32_t>(gltfTexIndex) >= gltfModel.textures_count) {
            return -1;
        }
        const tg3_texture& gltfTex = gltfModel.textures[gltfTexIndex];
        const int32_t imgIndex = gltfTex.source;
        if (imgIndex < 0 || static_cast<uint32_t>(imgIndex) >= gltfModel.images_count) {
            return -1;
        }

        // 同一张 Image + 同一 Format 复用已上传的 Texture2D（例如 glTF 的 Roughness 与 Metallic 共用一张贴图）
        const uint64_t cacheKey = (static_cast<uint64_t>(imgIndex) << 32) | static_cast<uint32_t>(format);
        if (auto it = textureCache.find(cacheKey); it != textureCache.end()) {
            return it->second;
        }

        const tg3_image& img = gltfModel.images[imgIndex];
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = nullptr;

        if (img.buffer_view >= 0 && static_cast<uint32_t>(img.buffer_view) < gltfModel.buffer_views_count) {
            // 1. GLB / 内嵌在 BufferView 中的压缩图片字节
            const tg3_buffer_view& bv = gltfModel.buffer_views[img.buffer_view];
            const tg3_buffer& buf = gltfModel.buffers[bv.buffer];
            const uint8_t* rawBytes = buf.data.data + bv.byte_offset;
            pixels = stbi_load_from_memory(
                rawBytes, static_cast<int>(bv.byte_length),
                &width, &height, &channels, STBI_rgb_alpha);
        } else if (img.uri.data != nullptr && img.uri.len > 0) {
            std::string_view uriView(img.uri.data, img.uri.len);
            if (tg3_is_data_uri(img.uri.data, img.uri.len)) {
                // 2. Base64 Data URI (data:image/png;base64,...)
                const size_t commaPos = uriView.find(',');
                if (commaPos != std::string_view::npos) {
                    std::vector<uint8_t> decoded = DecodeBase64(uriView.substr(commaPos + 1));
                    if (!decoded.empty()) {
                        pixels = stbi_load_from_memory(
                            decoded.data(), static_cast<int>(decoded.size()),
                            &width, &height, &channels, STBI_rgb_alpha);
                    }
                }
            } else {
                // 3. 外部图片文件路径（相对于 glTF 所在目录）
                const std::filesystem::path fullPath = gltfDir / std::filesystem::path(uriView);
                pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
            }
        }

        if (pixels == nullptr || width <= 0 || height <= 0) {
            if (pixels != nullptr) {
                stbi_image_free(pixels);
            }
            return -1;
        }

        const uint32_t pixelBytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4u;
        auto staging = context.CreateBuffer({
            .size = pixelBytes,
            .usage = Ccino::BufferUsage::Storage,
            .hostAccess = Ccino::RHI::HostAccess::Write,
        });
        staging->Copy(pixels);
        stbi_image_free(pixels);

        auto gpuTex = context.CreateTexture2D({
            .width = static_cast<uint32_t>(width),
            .height = static_cast<uint32_t>(height),
            .mipLevels = 1,
            .format = format,
            .usage = Ccino::TextureUsage::Sampled | Ccino::TextureUsage::TransferDst,
        });

        auto& rhi = context.GetRHIContext();
        if (!rhi.CopyTexFromBuffer(gpuTex->GetRHI(), staging->GetRHI())) {
            return -1;
        }
        rhi.WaitResource<Ccino::RHI::VulkanBuffer>(staging->GetRHI());

        const int outIndex = static_cast<int>(modelData.textures.size());
        modelData.textures.push_back(std::move(gpuTex));
        textureCache.emplace(cacheKey, outIndex);
        return outIndex;
    }

    void LoadMaterial(
        Ccino::Context& context,
        const tg3_model& gltfModel,
        Model& modelData,
        const std::filesystem::path& gltfDir,
        std::unordered_map<uint64_t, int>& textureCache,
        int materialIndex,
        Mesh& mesh)
    {
        mesh.textureIndices.fill(-1);
        mesh.baseColorFactor = glm::vec4(1.0f);
        mesh.roughnessFactor = 1.0f;
        mesh.metallicFactor = 1.0f;
        mesh.emissiveFactor = glm::vec3(0.0f);

        if (materialIndex < 0 || static_cast<uint32_t>(materialIndex) >= gltfModel.materials_count) {
            return;
        }

        const tg3_material& mat = gltfModel.materials[materialIndex];
        const tg3_pbr_metallic_roughness& pbr = mat.pbr_metallic_roughness;

        mesh.baseColorFactor = glm::vec4(
            static_cast<float>(pbr.base_color_factor[0]),
            static_cast<float>(pbr.base_color_factor[1]),
            static_cast<float>(pbr.base_color_factor[2]),
            static_cast<float>(pbr.base_color_factor[3]));
        mesh.roughnessFactor = static_cast<float>(pbr.roughness_factor);
        mesh.metallicFactor = static_cast<float>(pbr.metallic_factor);
        mesh.emissiveFactor = glm::vec3(
            static_cast<float>(mat.emissive_factor[0]),
            static_cast<float>(mat.emissive_factor[1]),
            static_cast<float>(mat.emissive_factor[2]));

        auto bindTexture = [&](TextureType slot, int32_t gltfTexIdx, Ccino::TextureFormat fmt) {
            mesh.textureIndices[static_cast<size_t>(slot)] = LoadGLTFTexture(
                context, gltfModel, modelData, gltfDir, textureCache, gltfTexIdx, fmt);
        };

        // BaseColor / Emissive 为颜色数据 (sRGB); Normal / MetallicRoughness / AO 为线性数据 (UNorm)
        // 注: glTF 2.0 中 Metallic(B) 与 Roughness(G) 共用 metallic_roughness_texture，缓存会确保只上传一次
        bindTexture(TextureType::BASE_COLOR, pbr.base_color_texture.index,         Ccino::TextureFormat::RGBA8_sRGB);
        bindTexture(TextureType::NORMAL,     mat.normal_texture.index,             Ccino::TextureFormat::RGBA8_UNorm);
        bindTexture(TextureType::ROUGHNESS,  pbr.metallic_roughness_texture.index, Ccino::TextureFormat::RGBA8_UNorm);
        bindTexture(TextureType::METALLIC,   pbr.metallic_roughness_texture.index, Ccino::TextureFormat::RGBA8_UNorm);
        bindTexture(TextureType::AO,         mat.occlusion_texture.index,          Ccino::TextureFormat::RGBA8_UNorm);
        bindTexture(TextureType::EMISSIVE,   mat.emissive_texture.index,           Ccino::TextureFormat::RGBA8_sRGB);
    }

    uint32_t TraverseNode(
        Ccino::Context& context,
        const tg3_model& gltfModel,
        Model& modelData,
        const std::filesystem::path& gltfDir,
        std::unordered_map<uint64_t, int>& textureCache,
        int nodeIdx,
        const glm::mat4& parentWorld,
        VertexTypeFlags vertexTypeFlags)
    {
        const auto& gltfNode = gltfModel.nodes[nodeIdx];
        const glm::mat4 world = parentWorld * GetTrans(gltfNode);
        Node node = {};
        node.meshIndex = UINT32_MAX;

        if (gltfNode.mesh >= 0 && static_cast<uint32_t>(gltfNode.mesh) < gltfModel.meshes_count) {
            const tg3_mesh& gltfMesh = gltfModel.meshes[gltfNode.mesh];
            for (uint32_t pIndex = 0; pIndex < gltfMesh.primitives_count; pIndex++) {
                const auto& gltfPrim = gltfMesh.primitives[pIndex];
                Mesh mesh;
                mesh.trans = world;
                mesh.bufferViews.fill(glm::ivec4(-1, 0, 0, 0));
                mesh.vertexTypeFlags = 0;

                if (vertexTypeFlags & VertexTypeFlagBits::POSITION) {
                    if (GetAccessorView(gltfModel, FindAttributeAccessor(gltfPrim, VertexTypeToString(VertexTypeFlagBits::POSITION).c_str()), mesh.bufferViews[0])) {
                        mesh.vertexTypeFlags |= static_cast<uint32_t>(VertexTypeFlagBits::POSITION);
                    }
                }
                if (vertexTypeFlags & VertexTypeFlagBits::NORMAL) {
                    if (GetAccessorView(gltfModel, FindAttributeAccessor(gltfPrim, VertexTypeToString(VertexTypeFlagBits::NORMAL).c_str()), mesh.bufferViews[1])) {
                        mesh.vertexTypeFlags |= static_cast<uint32_t>(VertexTypeFlagBits::NORMAL);
                    }
                }
                if (vertexTypeFlags & VertexTypeFlagBits::TEXCOORD) {
                    if (GetAccessorView(gltfModel, FindAttributeAccessor(gltfPrim, VertexTypeToString(VertexTypeFlagBits::TEXCOORD).c_str()), mesh.bufferViews[2])) {
                        mesh.vertexTypeFlags |= static_cast<uint32_t>(VertexTypeFlagBits::TEXCOORD);
                    }
                }
                LoadMaterial(context, gltfModel, modelData, gltfDir, textureCache, gltfPrim.material, mesh);

                const uint32_t vertexCount = mesh.bufferViews[0][0] >= 0 ? static_cast<uint32_t>(mesh.bufferViews[0][3]) : 0u;
                auto indexBuffer = CreateIndexBufferForPrimitive(context, gltfModel, gltfPrim, vertexCount);

                const uint32_t meshIndex = static_cast<uint32_t>(modelData.meshs.size());
                modelData.meshs.push_back(std::move(mesh));
                modelData.meshBuffers.push_back(std::move(indexBuffer));

                if (pIndex == 0) {
                    node.meshIndex = meshIndex;
                } else {
                    Node subNode = {};
                    subNode.meshIndex = meshIndex;
                    const uint32_t subNodeIdx = static_cast<uint32_t>(modelData.nodes.size());
                    modelData.nodes.push_back(std::move(subNode));
                    node.children.push_back(subNodeIdx);
                }
            }
        }

        for (uint32_t i = 0; i < gltfNode.children_count; i++) {
            node.children.push_back(TraverseNode(context, gltfModel, modelData, gltfDir, textureCache, gltfNode.children[i], world, vertexTypeFlags));
        }
        modelData.nodes.push_back(std::move(node));

        return static_cast<uint32_t>(modelData.nodes.size() - 1);
    }
}

std::unique_ptr<Model> LoadModel(Ccino::Context& context, const std::string& path, VertexTypeFlags vertexTypeFlags) {
    auto rt = std::make_unique<Model>();
    rt->root.meshIndex = UINT32_MAX;

    tg3_parse_options opts;
    tg3_error_stack errors;
    tg3_model model{};

    tg3_parse_options_init(&opts);
    tg3_error_stack_init(&errors);

    tg3_error_code err = tg3_parse_file(&model, &errors, path.c_str(), static_cast<uint32_t>(path.size()), &opts);
    if (err != TG3_OK) {
        for (uint32_t i = 0; i < errors.count; i++) {
            fprintf(stderr, "[%d] %s\n", (int)errors.entries[i].severity,
                    errors.entries[i].message ? errors.entries[i].message : "(null)");
        }
        tg3_model_free(&model);
        tg3_error_stack_free(&errors);
        return nullptr;
    }

    LoadBuffers(context, model, *rt);

    const std::filesystem::path gltfDir = std::filesystem::path(path).parent_path();
    std::unordered_map<uint64_t, int> textureCache;

    if (model.scenes_count > 0) {
        const int32_t sceneIdx = (model.default_scene >= 0 && static_cast<uint32_t>(model.default_scene) < model.scenes_count)
            ? model.default_scene
            : 0;
        const tg3_scene& scene = model.scenes[sceneIdx];
        for (uint32_t i = 0; i < scene.nodes_count; ++i) {
            rt->root.children.push_back(
                TraverseNode(context, model, *rt, gltfDir, textureCache, scene.nodes[i], glm::mat4(1.0f), vertexTypeFlags));
        }
    }

    tg3_model_free(&model);
    tg3_error_stack_free(&errors);
    return rt;
}
