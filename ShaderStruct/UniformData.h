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

    // X(EnumName, CppType, Std140Info)
    // 1. 与C++ 类型一一对应的Uniform 类型 (支持 CppType <-> UniformDataType 双向映射)
    #define CCINO_UNIFORM_BIDIR_TYPES(X) \
        /* Param 标量 */ \
        X(Bool,     bool,         Std140::Scalar(4)) \
        X(Int,      int32_t,      Std140::Scalar(4)) \
        X(UInt,     uint32_t,     Std140::Scalar(4)) \
        X(UInt64,   uint64_t,     Std140::Scalar(8)) \
        X(Float,    float,        Std140::Scalar(4)) \
        X(Double,   double,       Std140::Scalar(8)) \
        /* Param 向量 (对应 Core/CcinoType.h) */ \
        X(BVec2,    glm::bvec2,   Std140::Vector(2, 4)) \
        X(BVec3,    glm::bvec3,   Std140::Vector(3, 4)) \
        X(BVec4,    glm::bvec4,   Std140::Vector(4, 4)) \
        X(IVec2,    glm::ivec2,   Std140::Vector(2, 4)) \
        X(IVec3,    glm::ivec3,   Std140::Vector(3, 4)) \
        X(IVec4,    glm::ivec4,   Std140::Vector(4, 4)) \
        X(UVec2,    glm::uvec2,   Std140::Vector(2, 4)) \
        X(UVec3,    glm::uvec3,   Std140::Vector(3, 4)) \
        X(UVec4,    glm::uvec4,   Std140::Vector(4, 4)) \
        X(Vec2,     glm::vec2,    Std140::Vector(2, 4)) \
        X(Vec3,     glm::vec3,    Std140::Vector(3, 4)) \
        X(Vec4,     glm::vec4,    Std140::Vector(4, 4)) \
        X(DVec2,    glm::dvec2,   Std140::Vector(2, 8)) \
        X(DVec3,    glm::dvec3,   Std140::Vector(3, 8)) \
        X(DVec4,    glm::dvec4,   Std140::Vector(4, 8)) \
        /* Param float 矩阵 (列主序, MatCxR: C列R行) */ \
        X(Mat2,     glm::mat2,    Std140::Matrix(2, 2, 4)) \
        X(Mat3,     glm::mat3,    Std140::Matrix(3, 3, 4)) \
        X(Mat4,     glm::mat4,    Std140::Matrix(4, 4, 4)) \
        X(Mat2x3,   glm::mat2x3,  Std140::Matrix(2, 3, 4)) \
        X(Mat2x4,   glm::mat2x4,  Std140::Matrix(2, 4, 4)) \
        X(Mat3x2,   glm::mat3x2,  Std140::Matrix(3, 2, 4)) \
        X(Mat3x4,   glm::mat3x4,  Std140::Matrix(3, 4, 4)) \
        X(Mat4x2,   glm::mat4x2,  Std140::Matrix(4, 2, 4)) \
        X(Mat4x3,   glm::mat4x3,  Std140::Matrix(4, 3, 4)) \
        /* Param double 矩阵 */ \
        X(DMat2,    glm::dmat2,   Std140::Matrix(2, 2, 8)) \
        X(DMat3,    glm::dmat3,   Std140::Matrix(3, 3, 8)) \
        X(DMat4,    glm::dmat4,   Std140::Matrix(4, 4, 8)) \
        X(DMat2x3,  glm::dmat2x3, Std140::Matrix(2, 3, 8)) \
        X(DMat2x4,  glm::dmat2x4, Std140::Matrix(2, 4, 8)) \
        X(DMat3x2,  glm::dmat3x2, Std140::Matrix(3, 2, 8)) \
        X(DMat3x4,  glm::dmat3x4, Std140::Matrix(3, 4, 8)) \
        X(DMat4x2,  glm::dmat4x2, Std140::Matrix(4, 2, 8)) \
        X(DMat4x3,  glm::dmat4x3, Std140::Matrix(4, 3, 8))

    // 2. 资源类型 (单向映射: UniformDataType -> Uniform 中的表示类型, 不能由C++ 类型反推)
    // Buffer 存的是BDA, 和普通uint64_t 参数同为8字节, 但语义来自Accessor 而不是整数本身
    #define CCINO_UNIFORM_REPR_TYPES(X) \
        X(Buffer,       uint64_t, Std140::Scalar(8)) \
        X(SampledImage, uint32_t, Std140::Scalar(4)) \
        X(StorageImage, uint32_t, Std140::Scalar(4)) \
        X(Sampler,      uint32_t, Std140::Scalar(4))

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

    //字段顺序就是UniformBuffer 中成员的顺序, 对应Shader 里Slot 结构体(std140) 的成员声明顺序
    //顺序是CPU 和Shader 之间唯一的约定, 所以用vector 而不是unordered_map
    using DescriptorSlotLayout = std::vector<UniformData>;

    struct UniformOffset {
        uint32_t offset;
        UniformDataType uniformDataType;

        bool operator==(const UniformOffset&) const = default;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UniformOffset, offset, uniformDataType)

    //单个Uniform 成员在std140 下的布局信息
    struct UniformTypeInfo {
        uint32_t size = 0;         //成员在Buffer 中占用的字节数, 矩阵包含列间padding
        uint32_t alignment = 0;    //基础对齐, 成员偏移必须是它的整数倍
        uint32_t columns = 1;      //矩阵列数C, 非矩阵为1
        uint32_t rows = 1;         //向量分量数 / 矩阵行数R, 标量为1
        uint32_t scalarSize = 4;   //单个分量的字节数, double 为8
        uint32_t columnStride = 0; //矩阵相邻两列的间距, 非矩阵等于size

        constexpr bool operator==(const UniformTypeInfo&) const = default;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UniformTypeInfo, size, alignment, columns, rows, scalarSize, columnStride)

    //UniformBuffer 按std140 排布, Shader 里的Slot 结构体需要声明为std140
    namespace Std140 {
        constexpr uint32_t AlignUp(uint32_t value, uint32_t alignment) {
            return (value + alignment - 1) / alignment * alignment;
        }

        //标量: 大小和对齐都是N
        constexpr UniformTypeInfo Scalar(uint32_t scalarSize) {
            return {scalarSize, scalarSize, 1, 1, scalarSize, scalarSize};
        }

        //vec2 对齐2N, vec3 / vec4 对齐4N, 大小是分量数 * N (vec3 后面可以紧跟一个标量)
        constexpr UniformTypeInfo Vector(uint32_t components, uint32_t scalarSize) {
            const uint32_t size = components * scalarSize;
            const uint32_t alignment = (components == 2 ? 2 : 4) * scalarSize;
            return {size, alignment, 1, components, scalarSize, size};
        }

        //MatCxR (列主序) 等价于C 个vecR 的数组, std140 中数组步长向上取整到16
        constexpr UniformTypeInfo Matrix(uint32_t columns, uint32_t rows, uint32_t scalarSize) {
            const uint32_t stride = AlignUp(Vector(rows, scalarSize).alignment, 16);
            return {columns * stride, stride, columns, rows, scalarSize, stride};
        }
    }

    constexpr UniformTypeInfo GetUniformTypeInfo(UniformDataType type) {
        switch (type) {
            #define CCINO_DETAIL_TYPE_INFO_CASE(EnumName, CppType, Info) \
                case UniformDataType::EnumName: return Info;
            CCINO_UNIFORM_ALL_TYPES(CCINO_DETAIL_TYPE_INFO_CASE)
            #undef CCINO_DETAIL_TYPE_INFO_CASE
        }
        throw std::invalid_argument("Unknown UniformDataType");
    }

    //编译期自检, 和GLSL std140 规则对照
    static_assert(GetUniformTypeInfo(UniformDataType::Vec3).alignment == 16);
    static_assert(GetUniformTypeInfo(UniformDataType::Vec3).size == 12);
    static_assert(GetUniformTypeInfo(UniformDataType::Mat3).size == 48);
    static_assert(GetUniformTypeInfo(UniformDataType::Mat3x2).columnStride == 16);
    static_assert(GetUniformTypeInfo(UniformDataType::Mat2x3).size == 32);
    static_assert(GetUniformTypeInfo(UniformDataType::DVec3).alignment == 32);
    static_assert(GetUniformTypeInfo(UniformDataType::DMat3).size == 96);
    static_assert(GetUniformTypeInfo(UniformDataType::DMat4x2).columnStride == 16);

    // 1. C++ 类型 -> UniformDataType 映射 (没有特化的类型会在编译期报错, 例如size_t / int64_t)
    template<typename T>
    struct UniformTypeOf {
        static_assert(sizeof(T) == 0, "Type is not a supported uniform param type");
    };

    template<UniformDataType V>
    struct UniformTypeOfBase {
        static constexpr UniformDataType value = V;
    };

    // 2. UniformDataType -> C++ 类型映射
    template<UniformDataType V>
    struct UniformCppType;

    template<typename T>
    struct UniformCppTypeBase {
        using type = T;
    };

    template<UniformDataType V>
    using UniformCppType_t = typename UniformCppType<V>::type;

    #define CCINO_DETAIL_CPP_TO_ENUM(EnumName, CppType, Info) \
        template<> struct UniformTypeOf<CppType> : UniformTypeOfBase<UniformDataType::EnumName> {};
    #define CCINO_DETAIL_ENUM_TO_CPP(EnumName, CppType, Info) \
        template<> struct UniformCppType<UniformDataType::EnumName> : UniformCppTypeBase<CppType> {};

    CCINO_UNIFORM_BIDIR_TYPES(CCINO_DETAIL_CPP_TO_ENUM)
    CCINO_UNIFORM_ALL_TYPES(CCINO_DETAIL_ENUM_TO_CPP)

    #undef CCINO_DETAIL_CPP_TO_ENUM
    #undef CCINO_DETAIL_ENUM_TO_CPP

    // 3. 运行期UniformDataType 分发到编译期C++ 类型, 回调接收std::type_identity<T>{}
    template<typename Fn>
    decltype(auto) VisitUniformDataType(UniformDataType type, Fn&& fn) {
        switch (type) {
            #define CCINO_DETAIL_VISIT_CASE(EnumName, CppType, Info) \
                case UniformDataType::EnumName: return std::forward<Fn>(fn)(std::type_identity<CppType>{}); //把类型记住, 将模板编译器的对应放到了switch的运行期
            CCINO_UNIFORM_ALL_TYPES(CCINO_DETAIL_VISIT_CASE)
            #undef CCINO_DETAIL_VISIT_CASE
        }
        throw std::invalid_argument("Unknown UniformDataType");
    }

    namespace Std140 {
        //C++ 传入的类型能否写到layout 声明的成员上
        //SampledImage / StorageImage / Sampler 存的是全局DescriptorSet 中的数组索引, 可以直接用uint32_t 传入
        constexpr bool IsParamCompatible(UniformDataType paramType, UniformDataType layoutType) {
            if (paramType == layoutType) {
                return true;
            }
            return paramType == UniformDataType::UInt &&
                (layoutType == UniformDataType::SampledImage || layoutType == UniformDataType::StorageImage ||
                 layoutType == UniformDataType::Sampler);
        }

        //把一个Param 按std140 写入dst, dst 已经指向成员的偏移
        //标量 / 向量: 直接拷贝; 矩阵: 逐列拷贝, 每列之间按columnStride 留出padding; bool: 展开成uint32_t
        template<typename T>
        void WriteParam(char* dst, const T& value) {
            constexpr UniformTypeInfo info = GetUniformTypeInfo(UniformTypeOf<T>::value);

            if constexpr (std::is_same_v<T, bool>) {
                const uint32_t v = value ? 1u : 0u;
                std::memcpy(dst, &v, sizeof(v));
            } else if constexpr (std::is_same_v<T, glm::bvec2> || std::is_same_v<T, glm::bvec3> || std::is_same_v<T, glm::bvec4>) {
                for (glm::length_t i = 0; i < T::length(); ++i) {
                    const uint32_t v = value[i] ? 1u : 0u;
                    std::memcpy(dst + i * sizeof(uint32_t), &v, sizeof(v));
                }
            } else if constexpr (info.columns > 1) {
                //glm 矩阵是紧密排列的C 个vecR, 每列R * scalarSize 字节
                constexpr uint32_t columnBytes = info.rows * info.scalarSize;
                static_assert(sizeof(T) == info.columns * columnBytes, "glm matrix layout is not tightly packed");
                for (uint32_t c = 0; c < info.columns; ++c) {
                    std::memcpy(dst + c * info.columnStride, &value[static_cast<glm::length_t>(c)], columnBytes);
                }
            } else {
                static_assert(sizeof(T) == info.size, "C++ type size does not match the std140 size");
                std::memcpy(dst, &value, sizeof(T));
            }
        }

        //按运行期UniformDataType 从原始指针src 读取C++ 对象并按std140 写入dst
        inline void WriteParam(char* dst, UniformDataType type, const void* src) {
            VisitUniformDataType(type, [&](auto tag) {
                using CppType = typename decltype(tag)::type;
                WriteParam<CppType>(dst, *static_cast<const CppType*>(src));
            });
        }
    }
}
