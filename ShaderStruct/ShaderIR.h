#pragma once
#include "ShaderStruct/ShaderCommon.h"
#include "ShaderStruct/UniformData.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Ccino {
    enum class ShaderPropertyType {
        ReadOnlyStorageBuffer,
        WriteOnlyStorageBuffer,
        ReadWriteStorageBuffer,
        /*
        SampledTexture,
        StorageTexture,
        Sampler,
        先不实现Texture
        */
    };

    NLOHMANN_JSON_SERIALIZE_ENUM_STRICT(ShaderPropertyType, {
        {ShaderPropertyType::ReadOnlyStorageBuffer,  "ReadOnlyStorageBuffer"},
        {ShaderPropertyType::WriteOnlyStorageBuffer, "WriteOnlyStorageBuffer"},
        {ShaderPropertyType::ReadWriteStorageBuffer, "ReadWriteStorageBuffer"},
    })

    //这个也是资源绑定，并且加上资源声明
    struct ShaderPropertyBinding {
        std::string name;
        ShaderPropertyType propertyType;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ShaderPropertyBinding, name, propertyType)

    struct ShaderUniformDataBinding {
        std::string name{}; //结构体名
        uint32_t offset{}; // 当前的Pass的Lambda的堆偏移,后续Cpp消费这个堆偏移实现绑定
        UniformDataType uniformDataType;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ShaderUniformDataBinding, name, offset, uniformDataType)

    struct ShaderIR {
        std::string passName;
        ShaderType shaderType;

        //资源部分,声明布局
        DescriptorSlotLayout descriptorSlotLayout;

        //实际绑定，被Cpp消费. SSBO和Texture在声明Accessor时会被显式绑定
        std::vector<ShaderUniformDataBinding> uniformDataBindings; //这里负责的是常量

        //代码部分
        std::string glsl;
        std::string spvPath;//直接编译成SPV后续不用再运行期编译

        //后续标志位
        uint32_t size = 0; //记录代码字节数，因为后续glsl 可能spv 数据直接放到std::string中
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ShaderIR,
        passName, shaderType, descriptorSlotLayout, uniformDataBindings, glsl, spvPath, size)

    //这里统一丢到HugeHeader中，并且只需要处理下其中的类型
    struct ShaderFunction {
        std::string name;
        std::string glsl;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ShaderFunction, name, glsl)

    // 全局 ShaderIR 单例表 (passName -> ShaderIR)
    class ShaderIRRegistry {
    public:
        static ShaderIRRegistry& Instance() {
            static ShaderIRRegistry instance;
            return instance;
        }

        void Register(ShaderIR ir) {
            std::string name = ir.passName;
            m_shaderIRMap[std::move(name)] = std::move(ir);
        }

        void RegisterAll(std::vector<ShaderIR>&& irs) {
            for (auto& ir : irs) {
                Register(std::move(ir));
            }
        }

        const ShaderIR* Find(const std::string& passName) const {
            auto it = m_shaderIRMap.find(passName);
            return it != m_shaderIRMap.end() ? &it->second : nullptr;
        }

        ShaderIR& operator[](const std::string& passName) {
            return m_shaderIRMap[passName];
        }

        bool Contains(const std::string& passName) const {
            return m_shaderIRMap.contains(passName);
        }

        void Clear() {
            m_shaderIRMap.clear();
        }

        const std::unordered_map<std::string, ShaderIR>& GetAll() const {
            return m_shaderIRMap;
        }

    private:
        ShaderIRRegistry() = default;
        std::unordered_map<std::string, ShaderIR> m_shaderIRMap;
    };
}
