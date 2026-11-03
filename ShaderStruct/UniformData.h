#pragma once
#include <glm/glm.hpp> //Core/CcinoType.h 中的Vec / Mat 都是glm 类型的别名
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Ccino {
    enum class UniformDataType {
        //Resource
        Buffer, //Buffer全使用BDA uint64_t
        SampledImage, //全局DescriptorSet 中的数组索引 uint32_t
        StorageImage, //全局DescriptorSet 中的数组索引 uint32_t
        Sampler, //全局DescriptorSet 中的数组索引 uint32_t

        //Param 标量
        Bool, //GLSL bool 占4字节, BindParam 传入bool 时自动展开成uint32_t
        Int, //int32_t
        UInt, //uint32_t
        UInt64, //uint64_t, 和Buffer 的BDA 存储同为8字节, 但只是普通整数参数
        Float, //float
        Double, //double

        //Param 向量 对应Core/CcinoType.h
        BVec2, //GLSL bvec 每个分量4字节, glm::bvec 每个分量1字节, BindParam 会逐分量展开成uint32_t
        BVec3,
        BVec4,
        IVec2, //IVec2
        IVec3, //IVec3
        IVec4, //IVec4
        UVec2, //UVec2
        UVec3, //UVec3
        UVec4, //UVec4
        Vec2, //Vec2
        Vec3, //Vec3
        Vec4, //Vec4
        DVec2, //DVec2
        DVec3, //DVec3
        DVec4, //DVec4

        //Param 矩阵 (列主序, MatCxR: C列R行)
        Mat2, //Mat2 / Mat2x2
        Mat3, //Mat3 / Mat3x3
        Mat4, //Mat4 / Mat4x4
        Mat2x3,
        Mat2x4,
        Mat3x2,
        Mat3x4,
        Mat4x2,
        Mat4x3,

        DMat2, //DMat2 / DMat2x2
        DMat3, //DMat3 / DMat3x3
        DMat4, //DMat4 / DMat4x4
        DMat2x3,
        DMat2x4,
        DMat3x2,
        DMat3x4,
        DMat4x2,
        DMat4x3,
    };

    //单个Uniform 成员在 scalar 布局下的大小与对齐信息
    struct UniformTypeInfo {
        uint32_t size = 0;      //成员在Buffer 中占用的字节数
        uint32_t alignment = 0; //基础标量对齐 (4 或 8)

        constexpr bool operator==(const UniformTypeInfo&) const = default;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UniformTypeInfo, size, alignment)

    constexpr uint32_t AlignUp(uint32_t value, uint32_t alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

    // X(EnumName, CppType, TypeInfo)
    // 1. 与C++ 类型一一对应的Uniform 类型 (支持 CppType <-> UniformDataType 映射)
    #define CCINO_UNIFORM_BIDIR_TYPES(X) \
        /* Param 标量 */ \
        X(Bool,     bool,         (UniformTypeInfo{4, 4})) \
        X(Int,      int32_t,      (UniformTypeInfo{4, 4})) \
        X(UInt,     uint32_t,     (UniformTypeInfo{4, 4})) \
        X(UInt64,   uint64_t,     (UniformTypeInfo{8, 8})) \
        X(Float,    float,        (UniformTypeInfo{4, 4})) \
        X(Double,   double,       (UniformTypeInfo{8, 8})) \
        /* Param 向量 (对应 Core/CcinoType.h) */ \
        X(BVec2,    glm::bvec2,   (UniformTypeInfo{2 * 4, 4})) \
        X(BVec3,    glm::bvec3,   (UniformTypeInfo{3 * 4, 4})) \
        X(BVec4,    glm::bvec4,   (UniformTypeInfo{4 * 4, 4})) \
        X(IVec2,    glm::ivec2,   (UniformTypeInfo{2 * 4, 4})) \
        X(IVec3,    glm::ivec3,   (UniformTypeInfo{3 * 4, 4})) \
        X(IVec4,    glm::ivec4,   (UniformTypeInfo{4 * 4, 4})) \
        X(UVec2,    glm::uvec2,   (UniformTypeInfo{2 * 4, 4})) \
        X(UVec3,    glm::uvec3,   (UniformTypeInfo{3 * 4, 4})) \
        X(UVec4,    glm::uvec4,   (UniformTypeInfo{4 * 4, 4})) \
        X(Vec2,     glm::vec2,    (UniformTypeInfo{2 * 4, 4})) \
        X(Vec3,     glm::vec3,    (UniformTypeInfo{3 * 4, 4})) \
        X(Vec4,     glm::vec4,    (UniformTypeInfo{4 * 4, 4})) \
        X(DVec2,    glm::dvec2,   (UniformTypeInfo{2 * 8, 8})) \
        X(DVec3,    glm::dvec3,   (UniformTypeInfo{3 * 8, 8})) \
        X(DVec4,    glm::dvec4,   (UniformTypeInfo{4 * 8, 8})) \
        /* Param float 矩阵 (列主序, MatCxR: C列R行) */ \
        X(Mat2,     glm::mat2,    (UniformTypeInfo{2 * 2 * 4, 4})) \
        X(Mat3,     glm::mat3,    (UniformTypeInfo{3 * 3 * 4, 4})) \
        X(Mat4,     glm::mat4,    (UniformTypeInfo{4 * 4 * 4, 4})) \
        X(Mat2x3,   glm::mat2x3,  (UniformTypeInfo{2 * 3 * 4, 4})) \
        X(Mat2x4,   glm::mat2x4,  (UniformTypeInfo{2 * 4 * 4, 4})) \
        X(Mat3x2,   glm::mat3x2,  (UniformTypeInfo{3 * 2 * 4, 4})) \
        X(Mat3x4,   glm::mat3x4,  (UniformTypeInfo{3 * 4 * 4, 4})) \
        X(Mat4x2,   glm::mat4x2,  (UniformTypeInfo{4 * 2 * 4, 4})) \
        X(Mat4x3,   glm::mat4x3,  (UniformTypeInfo{4 * 3 * 4, 4})) \
        /* Param double 矩阵 */ \
        X(DMat2,    glm::dmat2,   (UniformTypeInfo{2 * 2 * 8, 8})) \
        X(DMat3,    glm::dmat3,   (UniformTypeInfo{3 * 3 * 8, 8})) \
        X(DMat4,    glm::dmat4,   (UniformTypeInfo{4 * 4 * 8, 8})) \
        X(DMat2x3,  glm::dmat2x3, (UniformTypeInfo{2 * 3 * 8, 8})) \
        X(DMat2x4,  glm::dmat2x4, (UniformTypeInfo{2 * 4 * 8, 8})) \
        X(DMat3x2,  glm::dmat3x2, (UniformTypeInfo{3 * 2 * 8, 8})) \
        X(DMat3x4,  glm::dmat3x4, (UniformTypeInfo{3 * 4 * 8, 8})) \
        X(DMat4x2,  glm::dmat4x2, (UniformTypeInfo{4 * 2 * 8, 8})) \
        X(DMat4x3,  glm::dmat4x3, (UniformTypeInfo{4 * 3 * 8, 8}))

    // 2. 资源类型
    #define CCINO_UNIFORM_REPR_TYPES(X) \
        X(Buffer,       uint64_t, (UniformTypeInfo{8, 8})) \
        X(SampledImage, uint32_t, (UniformTypeInfo{4, 4})) \
        X(StorageImage, uint32_t, (UniformTypeInfo{4, 4})) \
        X(Sampler,      uint32_t, (UniformTypeInfo{4, 4}))

    #define CCINO_UNIFORM_ALL_TYPES(X) \
        CCINO_UNIFORM_BIDIR_TYPES(X) \
        CCINO_UNIFORM_REPR_TYPES(X)

    #define CCINO_DETAIL_JSON_PAIR(EnumName, CppType, Info) {UniformDataType::EnumName, #EnumName},
    NLOHMANN_JSON_SERIALIZE_ENUM_STRICT(UniformDataType, {
        CCINO_UNIFORM_ALL_TYPES(CCINO_DETAIL_JSON_PAIR)
    })
    #undef CCINO_DETAIL_JSON_PAIR

    struct UniformData {
        std::string name;
        UniformDataType dataType;

        bool operator==(const UniformData&) const = default;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UniformData, name, dataType)

    //字段顺序就是UniformBuffer 中成员的顺序, 对应Shader 里 UniformData 结构体(scalar) 的成员声明顺序
    using DescriptorSlotLayout = std::vector<UniformData>;

    struct UniformOffset {
        uint32_t offset;
        UniformDataType uniformDataType;

        bool operator==(const UniformOffset&) const = default;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UniformOffset, offset, uniformDataType)

    constexpr UniformTypeInfo GetUniformTypeInfo(UniformDataType type) {
        switch (type) {
            #define CCINO_DETAIL_TYPE_INFO_CASE(EnumName, CppType, Info) \
                case UniformDataType::EnumName: return Info;
            CCINO_UNIFORM_ALL_TYPES(CCINO_DETAIL_TYPE_INFO_CASE)
            #undef CCINO_DETAIL_TYPE_INFO_CASE
        }
        throw std::invalid_argument("Unknown UniformDataType");
    }

    // C++ 类型 -> UniformDataType 映射
    template<typename T>
    struct UniformTypeOf {
        static_assert(sizeof(T) == 0, "Type is not a supported uniform param type");
    };

    template<UniformDataType V>
    struct UniformTypeOfBase {
        static constexpr UniformDataType value = V;
    };

    #define CCINO_DETAIL_CPP_TO_ENUM(EnumName, CppType, Info) \
        template<> struct UniformTypeOf<CppType> : UniformTypeOfBase<UniformDataType::EnumName> {};
    CCINO_UNIFORM_BIDIR_TYPES(CCINO_DETAIL_CPP_TO_ENUM)
    #undef CCINO_DETAIL_CPP_TO_ENUM

    //SampledImage / StorageImage / Sampler 存的是全局DescriptorSet 中的数组索引, 可以直接用uint32_t 传入
    constexpr bool IsParamCompatible(UniformDataType paramType, UniformDataType layoutType) {
        if (paramType == layoutType) {
            return true;
        }
        return paramType == UniformDataType::UInt &&
            (layoutType == UniformDataType::SampledImage || layoutType == UniformDataType::StorageImage ||
             layoutType == UniformDataType::Sampler);
    }

    //把一个Param 按 scalar 布局写入dst; 仅 bool / bvec (C++ 1字节 -> GLSL 4字节) 单独展开, 其余直接 memcpy
    template<typename T>
    void WriteParam(char* dst, const T& value) {
        if constexpr (std::is_same_v<T, bool>) {
            const uint32_t v = value ? 1u : 0u;
            std::memcpy(dst, &v, sizeof(v));
        } else if constexpr (std::is_same_v<T, glm::bvec2> || std::is_same_v<T, glm::bvec3> || std::is_same_v<T, glm::bvec4>) {
            for (glm::length_t i = 0; i < T::length(); ++i) {
                const uint32_t v = value[i] ? 1u : 0u;
                std::memcpy(dst + i * sizeof(uint32_t), &v, sizeof(v));
            }
        } else {
            std::memcpy(dst, &value, sizeof(T));
        }
    }

    inline void WriteParam(char* dst, UniformDataType type, const void* src) {
        const UniformTypeInfo info = GetUniformTypeInfo(type);
        if (type == UniformDataType::Bool || type == UniformDataType::BVec2 ||
            type == UniformDataType::BVec3 || type == UniformDataType::BVec4) {
            const uint32_t count = info.size / sizeof(uint32_t);
            const auto* bools = static_cast<const bool*>(src);
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t v = bools[i] ? 1u : 0u;
                std::memcpy(dst + i * sizeof(uint32_t), &v, sizeof(v));
            }
        } else {
            std::memcpy(dst, src, info.size);
        }
    }
}
