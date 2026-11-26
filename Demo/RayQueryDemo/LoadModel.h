#pragma once
#include "Buffer.h"
#include "CcinoContext.h"
#include "Flags.h"
#include "RefCounted.hpp"
#include "Texture2D.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

enum class VertexTypeFlagBits : uint8_t {
    POSITION = 1 << 0,
    NORMAL = 1 << 1,
    TEXCOORD = 1 << 2
};

using VertexTypeFlags = Flags<VertexTypeFlagBits>;

std::string VertexTypeToString(VertexTypeFlagBits type);

enum class TextureType {
    BASE_COLOR = 0,
    NORMAL,
    ROUGHNESS,
    METALLIC,
    AO,
    EMISSIVE,
    COUNT
};

struct Mesh {
    glm::mat4 trans{};
    //这里需要记录不同的VertexType的Offset
    std::array<glm::ivec4, 3> bufferViews{}; //LSB 得到其索引即可 //就是bufferIndex / stride / start / count
    //方便GPU使用
    uint32_t vertexTypeFlags = (uint32_t)VertexTypeFlagBits::POSITION;
    //这里使用enum class索引,如果索引为-1 则空
    std::array<int, static_cast<uint8_t>(TextureType::COUNT)> textureIndices = [] {
        std::array<int, static_cast<uint8_t>(TextureType::COUNT)> arr{};
        arr.fill(-1);
        return arr;
    }();
    //具体参数
    glm::vec4 baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    float roughnessFactor = 1.0f;
    float metallicFactor = 1.0f;
    glm::vec3 emissiveFactor = {};
};

struct Node {
    uint32_t meshIndex;
    std::vector<uint32_t> children;
};

struct Model {
    std::vector<Ccino::Ref<Ccino::Buffer>> vertexBuffers;
    std::vector<Ccino::Ref<Ccino::Buffer>> meshBuffers;
    std::vector<Ccino::Ref<Ccino::Texture2D>> textures;

    std::vector<Mesh> meshs;
    std::vector<Node> nodes;
    Node root;
};

std::unique_ptr<Model> LoadModel(Ccino::Context& context, const std::string& path, VertexTypeFlags vertexTypeFlags);
