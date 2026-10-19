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

    struct ShaderProperty {
        std::string name;
        ShaderPropertyType propertyType;
    };

    struct ShaderUniformData {
        std::string name; //结构体名

    };

    //这里只需要声明资源类型和名称
    struct Binding {
        std::string name;
        ShaderPropertyType propertyType;
    };

    struct ShaderIR {
        std::string passName;
        ShaderType shaderType;
        ThreadGroupSize threadGroupSize; //只有计算着色器需要

        //资源部分
        DescriptorSlotLayout descriptorSlotLayout; 

        //代码部分
        std::string glsl;
    };

    //这里统一丢到HugeHeader中，并且只需要处理下其中的类型
    struct ShaderFunction {
        std::string name;
        std::string glsl;
    };
}