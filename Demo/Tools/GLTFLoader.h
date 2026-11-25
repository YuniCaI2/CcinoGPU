#pragma once
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include <array>
#include <memory>
#include <string>

namespace DemoTools::GLTF {
    //这里使用暴力的存储

    enum class TextureDataType {
        BaseColor,
        Metallic,
        Roughness,
        Occlusion,
        Emissive,
        Count
    };

    struct TextureDatas {
        std::array<std::vector<uint8_t>, static_cast<uint8_t>(TextureDataType::Count)> datas;
    };

    struct VertexData {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoord;
    };


    struct MeshData {
        std::vector<VertexData> vertices {};
        std::vector<uint32_t> indices {};
        float metallicFactor = {};
        float roughnessFactor = {};
        TextureDatas textures {};
    };

    struct Node {
        glm::mat4 transform{};
        uint32_t index{};
        std::vector<uint32_t> children{};
    };

    struct ModelData {
        std::vector<MeshData> meshes{};
        std::vector<Node> nodes{};
        uint32_t root{};
    };

    class GLTFLoader {
    public:
        static std::unique_ptr<ModelData> Load(const std::string& str);
    };
}
