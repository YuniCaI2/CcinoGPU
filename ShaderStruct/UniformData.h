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

    NLOHMANN_JSON_SERIALIZE_ENUM_STRICT(UniformDataType, {
        //Resource
        {UniformDataType::Buffer,       "Buffer"},
        {UniformDataType::SampledImage, "SampledImage"},
        {UniformDataType::StorageImage, "StorageImage"},
        {UniformDataType::Sampler,      "Sampler"},

        //Param 标量
        {UniformDataType::Bool,   "Bool"},
        {UniformDataType::Int,    "Int"},
        {UniformDataType::UInt,   "UInt"},
        {UniformDataType::Float,  "Float"},
        {UniformDataType::Double, "Double"},

        //Param 向量
        {UniformDataType::BVec2, "BVec2"},
        {UniformDataType::BVec3, "BVec3"},
        {UniformDataType::BVec4, "BVec4"},
        {UniformDataType::IVec2, "IVec2"},
        {UniformDataType::IVec3, "IVec3"},
        {UniformDataType::IVec4, "IVec4"},
        {UniformDataType::UVec2, "UVec2"},
        {UniformDataType::UVec3, "UVec3"},
        {UniformDataType::UVec4, "UVec4"},
        {UniformDataType::Vec2,  "Vec2"},
        {UniformDataType::Vec3,  "Vec3"},
        {UniformDataType::Vec4,  "Vec4"},
        {UniformDataType::DVec2, "DVec2"},
        {UniformDataType::DVec3, "DVec3"},
        {UniformDataType::DVec4, "DVec4"},

        //Param float 矩阵
        {UniformDataType::Mat2,   "Mat2"},
        {UniformDataType::Mat3,   "Mat3"},
        {UniformDataType::Mat4,   "Mat4"},
        {UniformDataType::Mat2x3, "Mat2x3"},
        {UniformDataType::Mat2x4, "Mat2x4"},
        {UniformDataType::Mat3x2, "Mat3x2"},
        {UniformDataType::Mat3x4, "Mat3x4"},
        {UniformDataType::Mat4x2, "Mat4x2"},
        {UniformDataType::Mat4x3, "Mat4x3"},

        //Param double 矩阵
        {UniformDataType::DMat2,   "DMat2"},
        {UniformDataType::DMat3,   "DMat3"},
        {UniformDataType::DMat4,   "DMat4"},
        {UniformDataType::DMat2x3, "DMat2x3"},
        {UniformDataType::DMat2x4, "DMat2x4"},
        {UniformDataType::DMat3x2, "DMat3x2"},
        {UniformDataType::DMat3x4, "DMat3x4"},
        {UniformDataType::DMat4x2, "DMat4x2"},
        {UniformDataType::DMat4x3, "DMat4x3"},
    })

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
        using T = UniformDataType;
        switch (type) {
            //Resource
            case T::Buffer:       return Std140::Scalar(8); //BDA uint64_t
            case T::SampledImage: return Std140::Scalar(4); //数组索引 uint32_t
            case T::StorageImage: return Std140::Scalar(4); //数组索引 uint32_t
            case T::Sampler:      return Std140::Scalar(4); //数组索引 uint32_t

            //标量
            case T::Bool:   return Std140::Scalar(4);
            case T::Int:    return Std140::Scalar(4);
            case T::UInt:   return Std140::Scalar(4);
            case T::Float:  return Std140::Scalar(4);
            case T::Double: return Std140::Scalar(8);

            //向量
            case T::BVec2: return Std140::Vector(2, 4);
            case T::BVec3: return Std140::Vector(3, 4);
            case T::BVec4: return Std140::Vector(4, 4);
            case T::IVec2: return Std140::Vector(2, 4);
            case T::IVec3: return Std140::Vector(3, 4);
            case T::IVec4: return Std140::Vector(4, 4);
            case T::UVec2: return Std140::Vector(2, 4);
            case T::UVec3: return Std140::Vector(3, 4);
            case T::UVec4: return Std140::Vector(4, 4);
            case T::Vec2:  return Std140::Vector(2, 4);
            case T::Vec3:  return Std140::Vector(3, 4);
            case T::Vec4:  return Std140::Vector(4, 4);
            case T::DVec2: return Std140::Vector(2, 8);
            case T::DVec3: return Std140::Vector(3, 8);
            case T::DVec4: return Std140::Vector(4, 8);

            //float 矩阵 Matrix(列C, 行R, 分量字节)
            case T::Mat2:   return Std140::Matrix(2, 2, 4);
            case T::Mat3:   return Std140::Matrix(3, 3, 4);
            case T::Mat4:   return Std140::Matrix(4, 4, 4);
            case T::Mat2x3: return Std140::Matrix(2, 3, 4);
            case T::Mat2x4: return Std140::Matrix(2, 4, 4);
            case T::Mat3x2: return Std140::Matrix(3, 2, 4);
            case T::Mat3x4: return Std140::Matrix(3, 4, 4);
            case T::Mat4x2: return Std140::Matrix(4, 2, 4);
            case T::Mat4x3: return Std140::Matrix(4, 3, 4);

            //double 矩阵
            case T::DMat2:   return Std140::Matrix(2, 2, 8);
            case T::DMat3:   return Std140::Matrix(3, 3, 8);
            case T::DMat4:   return Std140::Matrix(4, 4, 8);
            case T::DMat2x3: return Std140::Matrix(2, 3, 8);
            case T::DMat2x4: return Std140::Matrix(2, 4, 8);
            case T::DMat3x2: return Std140::Matrix(3, 2, 8);
            case T::DMat3x4: return Std140::Matrix(3, 4, 8);
            case T::DMat4x2: return Std140::Matrix(4, 2, 8);
            case T::DMat4x3: return Std140::Matrix(4, 3, 8);
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

    //C++ 类型到UniformDataType 的映射, BindParam 用它在运行期检查类型
    //没有特化的类型会在编译期报错, 例如size_t / int64_t
    template<typename T>
    struct UniformTypeOf {
        static_assert(sizeof(T) == 0, "Type is not a supported uniform param type");
    };

    template<UniformDataType V>
    struct UniformTypeOfBase {
        static constexpr UniformDataType value = V;
    };

    //标量. bool 单独处理: C++ 1字节, GLSL 4字节
    template<> struct UniformTypeOf<bool>     : UniformTypeOfBase<UniformDataType::Bool> {};
    template<> struct UniformTypeOf<int32_t>  : UniformTypeOfBase<UniformDataType::Int> {};
    template<> struct UniformTypeOf<uint32_t> : UniformTypeOfBase<UniformDataType::UInt> {};
    template<> struct UniformTypeOf<uint64_t> : UniformTypeOfBase<UniformDataType::Buffer> {}; //BDA, 即VkDeviceAddress
    template<> struct UniformTypeOf<float>    : UniformTypeOfBase<UniformDataType::Float> {};
    template<> struct UniformTypeOf<double>   : UniformTypeOfBase<UniformDataType::Double> {};

    //向量 (与Core/CcinoType.h 中的别名一一对应)
    template<> struct UniformTypeOf<glm::bvec2> : UniformTypeOfBase<UniformDataType::BVec2> {};
    template<> struct UniformTypeOf<glm::bvec3> : UniformTypeOfBase<UniformDataType::BVec3> {};
    template<> struct UniformTypeOf<glm::bvec4> : UniformTypeOfBase<UniformDataType::BVec4> {};
    template<> struct UniformTypeOf<glm::ivec2> : UniformTypeOfBase<UniformDataType::IVec2> {};
    template<> struct UniformTypeOf<glm::ivec3> : UniformTypeOfBase<UniformDataType::IVec3> {};
    template<> struct UniformTypeOf<glm::ivec4> : UniformTypeOfBase<UniformDataType::IVec4> {};
    template<> struct UniformTypeOf<glm::uvec2> : UniformTypeOfBase<UniformDataType::UVec2> {};
    template<> struct UniformTypeOf<glm::uvec3> : UniformTypeOfBase<UniformDataType::UVec3> {};
    template<> struct UniformTypeOf<glm::uvec4> : UniformTypeOfBase<UniformDataType::UVec4> {};
    template<> struct UniformTypeOf<glm::vec2>  : UniformTypeOfBase<UniformDataType::Vec2> {};
    template<> struct UniformTypeOf<glm::vec3>  : UniformTypeOfBase<UniformDataType::Vec3> {};
    template<> struct UniformTypeOf<glm::vec4>  : UniformTypeOfBase<UniformDataType::Vec4> {};
    template<> struct UniformTypeOf<glm::dvec2> : UniformTypeOfBase<UniformDataType::DVec2> {};
    template<> struct UniformTypeOf<glm::dvec3> : UniformTypeOfBase<UniformDataType::DVec3> {};
    template<> struct UniformTypeOf<glm::dvec4> : UniformTypeOfBase<UniformDataType::DVec4> {};

    //float 矩阵 (glm::matCxR: C列R行, 与GLSL 一致)
    template<> struct UniformTypeOf<glm::mat2>   : UniformTypeOfBase<UniformDataType::Mat2> {};
    template<> struct UniformTypeOf<glm::mat3>   : UniformTypeOfBase<UniformDataType::Mat3> {};
    template<> struct UniformTypeOf<glm::mat4>   : UniformTypeOfBase<UniformDataType::Mat4> {};
    template<> struct UniformTypeOf<glm::mat2x3> : UniformTypeOfBase<UniformDataType::Mat2x3> {};
    template<> struct UniformTypeOf<glm::mat2x4> : UniformTypeOfBase<UniformDataType::Mat2x4> {};
    template<> struct UniformTypeOf<glm::mat3x2> : UniformTypeOfBase<UniformDataType::Mat3x2> {};
    template<> struct UniformTypeOf<glm::mat3x4> : UniformTypeOfBase<UniformDataType::Mat3x4> {};
    template<> struct UniformTypeOf<glm::mat4x2> : UniformTypeOfBase<UniformDataType::Mat4x2> {};
    template<> struct UniformTypeOf<glm::mat4x3> : UniformTypeOfBase<UniformDataType::Mat4x3> {};

    //double 矩阵
    template<> struct UniformTypeOf<glm::dmat2>   : UniformTypeOfBase<UniformDataType::DMat2> {};
    template<> struct UniformTypeOf<glm::dmat3>   : UniformTypeOfBase<UniformDataType::DMat3> {};
    template<> struct UniformTypeOf<glm::dmat4>   : UniformTypeOfBase<UniformDataType::DMat4> {};
    template<> struct UniformTypeOf<glm::dmat2x3> : UniformTypeOfBase<UniformDataType::DMat2x3> {};
    template<> struct UniformTypeOf<glm::dmat2x4> : UniformTypeOfBase<UniformDataType::DMat2x4> {};
    template<> struct UniformTypeOf<glm::dmat3x2> : UniformTypeOfBase<UniformDataType::DMat3x2> {};
    template<> struct UniformTypeOf<glm::dmat3x4> : UniformTypeOfBase<UniformDataType::DMat3x4> {};
    template<> struct UniformTypeOf<glm::dmat4x2> : UniformTypeOfBase<UniformDataType::DMat4x2> {};
    template<> struct UniformTypeOf<glm::dmat4x3> : UniformTypeOfBase<UniformDataType::DMat4x3> {};

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
    }
}
