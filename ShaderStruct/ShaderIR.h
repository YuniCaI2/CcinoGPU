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
        std::vector<ShaderUniformDataBinding> csUniformDataBindings; //这里负责的是常量
        std::vector<ShaderUniformDataBinding> vsUniformDataBindings; //这里负责的是常量
        std::vector<ShaderUniformDataBinding> fsUniformDataBindings; //这里负责的是常量

        //代码部分. 计算 Pass 使用 cs, 图形 Pass 使用 vs 和 fs. 运行期会把对应的 SPIR-V 字节放进这些字符串
        std::string cs;
        std::string vs;
        std::string fs;
        uint32_t csSize = 0;
        uint32_t vsSize = 0;
        uint32_t fsSize = 0;
        std::string csSpvPath;//直接编译成SPV后续不用再运行期编译
        std::string vsSpvPath;
        std::string fsSpvPath;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ShaderIR,
        passName, shaderType, descriptorSlotLayout, csUniformDataBindings, cs, vs, fs, csSize, vsSize, fsSize, csSpvPath)

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
