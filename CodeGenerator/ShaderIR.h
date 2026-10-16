#pragma once
#include <string>

namespace Ccino {
    enum class ShaderType {
        Compute,
        Vertex,
        Fragment
    };


    enum class ShaderPropertyType {
        UniformData, //其实也是StorageBuffer ？ 思考下
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

    enum class UniformDataType {
        Bool,
        Int,
        UInt,
        Float,
        Double,

        BVec2,
        BVec3,
        BVec4,
        IVec2,
        IVec3,
        IVec4,
        UVec2,
        UVec3,
        UVec4,
        Vec2,
        Vec3,
        Vec4,
        DVec2,
        DVec3,
        DVec4,

        Mat2,
        Mat3,
        Mat4,
    };

    struct ShaderUniformData {
        std::string name; //结构体名

    };


    
    struct ShaderIR {
        ShaderType shaderType;
       std::string GLSLCode; //转义出来的结果,常量需要优化到Shader中
    };
}