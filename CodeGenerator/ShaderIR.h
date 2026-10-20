#pragma once
#include "ShaderStruct/UniformData.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Ccino {
    enum class ShaderType {
        Compute,
        Vertex,
        Fragment
    };

    struct ThreadGroupSize {
        uint32_t x;
        uint32_t y;
        uint32_t z;
    };


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

    //这个也是资源绑定，并且加上资源声明
    struct ShaderPropertyBinding {
        std::string name;
        ShaderPropertyType propertyType;
    };

    struct ShaderUniformDataBinding {
        std::string name; //结构体名
        uint32_t offset; // 当前的Pass的Lambda的堆偏移,后续Cpp消费这个堆偏移实现绑定
        UniformDataType uniformDataType;
    };

    struct ShaderIR {
        std::string passName;
        ShaderType shaderType;
        ThreadGroupSize threadGroupSize; //只有计算着色器需要

        //资源部分,声明布局
        DescriptorSlotLayout descriptorSlotLayout; 

        //实际绑定，被Cpp消费
        std::vector<ShaderPropertyBinding> propertyBindings;
        std::vector<ShaderUniformDataBinding> uniformDataBindings;

        //代码部分
        std::string glsl;
    };

    //这里统一丢到HugeHeader中，并且只需要处理下其中的类型
    struct ShaderFunction {
        std::string name;
        std::string glsl;
    };
}