#pragma once

#include "ShaderStruct/ShaderCommon.h"
#include <glm/glm.hpp>

#include <utility>
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Ccino {


    //常规应用
    template<typename T, size_t N>
    using Array = std::array<T, N>;

    //GLM Packed
    using Vec2 = glm::vec2;
    using Vec3 = glm::vec3;
    using Vec4 = glm::vec4;

    using DVec2 = glm::dvec2;
    using DVec3 = glm::dvec3;
    using DVec4 = glm::dvec4;

    using IVec2 = glm::ivec2;
    using IVec3 = glm::ivec3;
    using IVec4 = glm::ivec4;

    using UVec2 = glm::uvec2;
    using UVec3 = glm::uvec3;
    using UVec4 = glm::uvec4;

    using BVec2 = glm::bvec2;
    using BVec3 = glm::bvec3;
    using BVec4 = glm::bvec4;

    using Mat2 = glm::mat2;
    using Mat3 = glm::mat3;
    using Mat4 = glm::mat4;

    using Mat2x2 = glm::mat2x2;
    using Mat2x3 = glm::mat2x3;
    using Mat2x4 = glm::mat2x4;
    using Mat3x2 = glm::mat3x2;
    using Mat3x3 = glm::mat3x3;
    using Mat3x4 = glm::mat3x4;
    using Mat4x2 = glm::mat4x2;
    using Mat4x3 = glm::mat4x3;
    using Mat4x4 = glm::mat4x4;

    using DMat2 = glm::dmat2;
    using DMat3 = glm::dmat3;
    using DMat4 = glm::dmat4;

    using DMat2x2 = glm::dmat2x2;
    using DMat2x3 = glm::dmat2x3;
    using DMat2x4 = glm::dmat2x4;
    using DMat3x2 = glm::dmat3x2;
    using DMat3x3 = glm::dmat3x3;
    using DMat3x4 = glm::dmat3x4;
    using DMat4x2 = glm::dmat4x2;
    using DMat4x3 = glm::dmat4x3;
    using DMat4x4 = glm::dmat4x4;


    // 内建全局变量 (与下方 GLSLBuiltinVarMap 一一对应)
    // 1. Compute Shader 内建变量 (in)
    inline constexpr UVec3    ComputeNumWorkGroups{};        // gl_NumWorkGroups
    inline constexpr UVec3    ComputeWorkGroupSize{};        // gl_WorkGroupSize
    inline constexpr UVec3    ComputeWorkGroupID{};          // gl_WorkGroupID
    inline constexpr UVec3    ComputeLocalID{};              // gl_LocalInvocationID
    inline constexpr UVec3    ComputeLocalInvocationID{};    // gl_LocalInvocationID
    inline constexpr UVec3    ComputeGlobalID{};             // gl_GlobalInvocationID
    inline constexpr UVec3    ComputeGlobalInvocationID{};   // gl_GlobalInvocationID
    inline constexpr uint32_t ComputeLocalIndex{};           // gl_LocalInvocationIndex
    inline constexpr uint32_t ComputeLocalInvocationIndex{}; // gl_LocalInvocationIndex

    // 2. Subgroup (Warp / Wave) 内建变量 (in)
    inline constexpr uint32_t NumSubgroups{};                // gl_NumSubgroups
    inline constexpr uint32_t SubgroupID{};                  // gl_SubgroupID
    inline constexpr uint32_t SubgroupSize{};                // gl_SubgroupSize
    inline constexpr uint32_t SubgroupInvocationID{};        // gl_SubgroupInvocationID

    // 3. Vertex Shader 内建变量 (in / out)
    inline constexpr int32_t  VertexIndex{};                 // in  gl_VertexIndex
    inline constexpr int32_t  InstanceIndex{};               // in  gl_InstanceIndex
    inline Vec4               Position{};                    // out gl_Position (可写，不加 constexpr)
    inline float              PointSize{};                   // out gl_PointSize (可写，不加 constexpr)

    // 4. Fragment Shader 内建变量 (in / out)
    inline constexpr Vec4     FragCoord{};                   // in  gl_FragCoord
    inline constexpr bool     FrontFacing{};                 // in  gl_FrontFacing
    inline constexpr Vec2     PointCoord{};                  // in  gl_PointCoord
    inline float              FragDepth{};                   // out gl_FragDepth (可写，不加 constexpr)
    inline constexpr bool     HelperInvocation{};            // in  gl_HelperInvocation

    // 1. 角度与三角函数 (Angle and Trigonometry Functions)
    template <typename... Args>
    constexpr decltype(auto) Radians(Args&&... args) {
        return glm::radians(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Degrees(Args&&... args) {
        return glm::degrees(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Sin(Args&&... args) {
        return glm::sin(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Cos(Args&&... args) {
        return glm::cos(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Tan(Args&&... args) {
        return glm::tan(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Asin(Args&&... args) {
        return glm::asin(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Acos(Args&&... args) {
        return glm::acos(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Atan(Args&&... args) {
        return glm::atan(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Sinh(Args&&... args) {
        return glm::sinh(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Cosh(Args&&... args) {
        return glm::cosh(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Tanh(Args&&... args) {
        return glm::tanh(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Asinh(Args&&... args) {
        return glm::asinh(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Acosh(Args&&... args) {
        return glm::acosh(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Atanh(Args&&... args) {
        return glm::atanh(std::forward<Args>(args)...);
    }

    // 2. 指数函数 (Exponential Functions)
    template <typename... Args>
    constexpr decltype(auto) Pow(Args&&... args) {
        return glm::pow(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Exp(Args&&... args) {
        return glm::exp(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Log(Args&&... args) {
        return glm::log(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Exp2(Args&&... args) {
        return glm::exp2(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Log2(Args&&... args) {
        return glm::log2(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Sqrt(Args&&... args) {
        return glm::sqrt(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) InverseSqrt(Args&&... args) {
        return glm::inversesqrt(std::forward<Args>(args)...);
    }

    // 3. 常用函数 (Common Functions)
    template <typename... Args>
    constexpr decltype(auto) Abs(Args&&... args) {
        return glm::abs(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Sign(Args&&... args) {
        return glm::sign(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Floor(Args&&... args) {
        return glm::floor(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Trunc(Args&&... args) {
        return glm::trunc(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Round(Args&&... args) {
        return glm::round(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) RoundEven(Args&&... args) {
        return glm::roundEven(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Ceil(Args&&... args) {
        return glm::ceil(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Fract(Args&&... args) {
        return glm::fract(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Mod(Args&&... args) {
        return glm::mod(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Modf(Args&&... args) {
        return glm::modf(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Min(Args&&... args) {
        return glm::min(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Max(Args&&... args) {
        return glm::max(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Clamp(Args&&... args) {
        return glm::clamp(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Mix(Args&&... args) {
        return glm::mix(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Step(Args&&... args) {
        return glm::step(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) SmoothStep(Args&&... args) {
        return glm::smoothstep(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) IsNan(Args&&... args) {
        return glm::isnan(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) IsInf(Args&&... args) {
        return glm::isinf(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) FloatBitsToInt(Args&&... args) {
        return glm::floatBitsToInt(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) FloatBitsToUint(Args&&... args) {
        return glm::floatBitsToUint(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) IntBitsToFloat(Args&&... args) {
        return glm::intBitsToFloat(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) UintBitsToFloat(Args&&... args) {
        return glm::uintBitsToFloat(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Fma(Args&&... args) {
        return glm::fma(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Frexp(Args&&... args) {
        return glm::frexp(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Ldexp(Args&&... args) {
        return glm::ldexp(std::forward<Args>(args)...);
    }

    // 4. 浮点打包与解包函数 (Floating-Point Pack and Unpack Functions)
    template <typename... Args>
    constexpr decltype(auto) PackUnorm2x16(Args&&... args) {
        return glm::packUnorm2x16(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) PackSnorm2x16(Args&&... args) {
        return glm::packSnorm2x16(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) PackUnorm4x8(Args&&... args) {
        return glm::packUnorm4x8(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) PackSnorm4x8(Args&&... args) {
        return glm::packSnorm4x8(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) UnpackUnorm2x16(Args&&... args) {
        return glm::unpackUnorm2x16(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) UnpackSnorm2x16(Args&&... args) {
        return glm::unpackSnorm2x16(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) UnpackUnorm4x8(Args&&... args) {
        return glm::unpackUnorm4x8(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) UnpackSnorm4x8(Args&&... args) {
        return glm::unpackSnorm4x8(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) PackDouble2x32(Args&&... args) {
        return glm::packDouble2x32(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) UnpackDouble2x32(Args&&... args) {
        return glm::unpackDouble2x32(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) PackHalf2x16(Args&&... args) {
        return glm::packHalf2x16(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) UnpackHalf2x16(Args&&... args) {
        return glm::unpackHalf2x16(std::forward<Args>(args)...);
    }

    // 5. 几何函数 (Geometric Functions)
    template <typename... Args>
    constexpr decltype(auto) Length(Args&&... args) {
        return glm::length(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Distance(Args&&... args) {
        return glm::distance(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Dot(Args&&... args) {
        return glm::dot(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Cross(Args&&... args) {
        return glm::cross(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Normalize(Args&&... args) {
        return glm::normalize(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) FaceForward(Args&&... args) {
        return glm::faceforward(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Reflect(Args&&... args) {
        return glm::reflect(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Refract(Args&&... args) {
        return glm::refract(std::forward<Args>(args)...);
    }

    // 6. 矩阵函数 (Matrix Functions)
    template <typename... Args>
    constexpr decltype(auto) MatrixCompMult(Args&&... args) {
        return glm::matrixCompMult(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) OuterProduct(Args&&... args) {
        return glm::outerProduct(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Transpose(Args&&... args) {
        return glm::transpose(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Determinant(Args&&... args) {
        return glm::determinant(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Inverse(Args&&... args) {
        return glm::inverse(std::forward<Args>(args)...);
    }

    // 7. 向量关系函数 (Vector Relational Functions)
    template <typename... Args>
    constexpr decltype(auto) LessThan(Args&&... args) {
        return glm::lessThan(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) LessThanEqual(Args&&... args) {
        return glm::lessThanEqual(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) GreaterThan(Args&&... args) {
        return glm::greaterThan(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) GreaterThanEqual(Args&&... args) {
        return glm::greaterThanEqual(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Equal(Args&&... args) {
        return glm::equal(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) NotEqual(Args&&... args) {
        return glm::notEqual(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Any(Args&&... args) {
        return glm::any(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) All(Args&&... args) {
        return glm::all(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) Not(Args&&... args) {
        return glm::not_(std::forward<Args>(args)...);
    }

    // 8. 整数函数 (Integer Functions)
    template <typename... Args>
    constexpr decltype(auto) UaddCarry(Args&&... args) {
        return glm::uaddCarry(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) UsubBorrow(Args&&... args) {
        return glm::usubBorrow(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) UmulExtended(Args&&... args) {
        return glm::umulExtended(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) ImulExtended(Args&&... args) {
        return glm::imulExtended(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) BitfieldExtract(Args&&... args) {
        return glm::bitfieldExtract(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) BitfieldInsert(Args&&... args) {
        return glm::bitfieldInsert(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) BitfieldReverse(Args&&... args) {
        return glm::bitfieldReverse(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) BitCount(Args&&... args) {
        return glm::bitCount(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) FindLSB(Args&&... args) {
        return glm::findLSB(std::forward<Args>(args)...);
    }

    template <typename... Args>
    constexpr decltype(auto) FindMSB(Args&&... args) {
        return glm::findMSB(std::forward<Args>(args)...);
    }

    // glm 内部函数名 / GLSL 标准内置函数白名单
    inline const std::unordered_set<std::string> GLSLBuiltinFuncSet = {
        // 1. 角度与三角函数 (Angle and Trigonometry Functions)
        "radians",
        "degrees",
        "sin",
        "cos",
        "tan",
        "asin",
        "acos",
        "atan",
        "sinh",
        "cosh",
        "tanh",
        "asinh",
        "acosh",
        "atanh",

        // 2. 指数函数 (Exponential Functions)
        "pow",
        "exp",
        "log",
        "exp2",
        "log2",
        "sqrt",
        "inversesqrt",

        // 3. 常用函数 (Common Functions)
        "abs",
        "sign",
        "floor",
        "trunc",
        "round",
        "roundEven",
        "ceil",
        "fract",
        "mod",
        "modf",
        "min",
        "max",
        "clamp",
        "mix",
        "step",
        "smoothstep",
        "isnan",
        "isinf",
        "floatBitsToInt",
        "floatBitsToUint",
        "intBitsToFloat",
        "uintBitsToFloat",
        "fma",
        "frexp",
        "ldexp",

        // 4. 浮点打包与解包函数 (Floating-Point Pack and Unpack Functions)
        "packUnorm2x16",
        "packSnorm2x16",
        "packUnorm4x8",
        "packSnorm4x8",
        "unpackUnorm2x16",
        "unpackSnorm2x16",
        "unpackUnorm4x8",
        "unpackSnorm4x8",
        "packDouble2x32",
        "unpackDouble2x32",
        "packHalf2x16",
        "unpackHalf2x16",

        // 5. 几何函数 (Geometric Functions)
        "length",
        "distance",
        "dot",
        "cross",
        "normalize",
        "faceforward",
        "reflect",
        "refract",

        // 6. 矩阵函数 (Matrix Functions)
        "matrixCompMult",
        "outerProduct",
        "transpose",
        "determinant",
        "inverse",

        // 7. 向量关系函数 (Vector Relational Functions)
        "lessThan",
        "lessThanEqual",
        "greaterThan",
        "greaterThanEqual",
        "equal",
        "notEqual",
        "any",
        "all",
        "not_",
        "not",

        // 8. 整数函数 (Integer Functions)
        "uaddCarry",
        "usubBorrow",
        "umulExtended",
        "imulExtended",
        "bitfieldExtract",
        "bitfieldInsert",
        "bitfieldReverse",
        "bitCount",
        "findLSB",
        "findMSB",

        // 9. 同步与屏障函数 (Shader Invocation Control & Memory Barrier Functions)
        "barrier",
    };

    // Cpp -> GLSL 内建变量映射表
    inline const std::unordered_map<std::string, std::string> GLSLBuiltinVarMap = {
        // 1. Compute Shader 内建变量
        {"ComputeNumWorkGroups",        "gl_NumWorkGroups"},
        {"ComputeWorkGroupSize",        "gl_WorkGroupSize"},
        {"ComputeWorkGroupID",          "gl_WorkGroupID"},
        {"ComputeLocalID",              "gl_LocalInvocationID"},
        {"ComputeLocalInvocationID",    "gl_LocalInvocationID"},
        {"ComputeGlobalID",             "gl_GlobalInvocationID"},
        {"ComputeGlobalInvocationID",   "gl_GlobalInvocationID"},
        {"ComputeLocalIndex",           "gl_LocalInvocationIndex"},
        {"ComputeLocalInvocationIndex", "gl_LocalInvocationIndex"},

        // 2. Subgroup (Warp / Wave) 内建变量
        {"NumSubgroups",                "gl_NumSubgroups"},
        {"SubgroupID",                  "gl_SubgroupID"},
        {"SubgroupSize",                "gl_SubgroupSize"},
        {"SubgroupInvocationID",        "gl_SubgroupInvocationID"},

        // 3. Vertex Shader 内建变量 (Vulkan GLSL)
        {"VertexIndex",                 "gl_VertexIndex"},
        {"InstanceIndex",               "gl_InstanceIndex"},
        {"Position",                    "gl_Position"},
        {"PointSize",                   "gl_PointSize"},

        // 4. Fragment Shader 内建变量
        {"FragCoord",                   "gl_FragCoord"},
        {"FrontFacing",                 "gl_FrontFacing"},
        {"PointCoord",                  "gl_PointCoord"},
        {"FragDepth",                   "gl_FragDepth"},
        {"HelperInvocation",            "gl_HelperInvocation"},
    };
}
