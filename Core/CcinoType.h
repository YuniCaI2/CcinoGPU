#pragma once

#include <glm/glm.hpp>

#include <utility>
#include <array>

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
}
