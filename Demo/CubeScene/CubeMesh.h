#pragma once

#include "Core/CcinoType.h"

#include <array>
#include <cstdint>

// 边长 1、原点居中的立方体。每条边 kCubeResolution 个质点, 共 8*8*8。
// 下标 x + 8 * (y + 8 * z), x/y/z 都在 [0, 7], 坐标从 -0.5 到 0.5。
// 索引缓冲只包住外表面 (右手系逆时针正面, 法线朝外), 内部质点留在顶点缓冲里。
struct CubeVertex {
    Ccino::Vec3 position;
    Ccino::Vec3 normal;
    Ccino::Vec2 uv;
};

// BDA Accessor<T> 需要结构体以生成 GLSL buffer_reference
struct CubeIndex {
    uint32_t index;
};

constexpr uint32_t kCubeResolution = 8;
constexpr uint32_t kCubeVertexCount = kCubeResolution * kCubeResolution * kCubeResolution;
constexpr uint32_t kCubeIndexCount = 6u * (kCubeResolution - 1) * (kCubeResolution - 1) * 6u;

constexpr uint32_t CubeGridIndex(uint32_t x, uint32_t y, uint32_t z) {
    return x + kCubeResolution * (y + kCubeResolution * z);
}

constexpr float CubeGridCoord(uint32_t i) {
    return -0.5f + static_cast<float>(i) / static_cast<float>(kCubeResolution - 1);
}

constexpr CubeVertex MakeCubeVertex(uint32_t x, uint32_t y, uint32_t z) {
    const uint32_t last = kCubeResolution - 1;
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    if (x == 0) {
        nx -= 1.0f;
    }
    if (x == last) {
        nx += 1.0f;
    }
    if (y == 0) {
        ny -= 1.0f;
    }
    if (y == last) {
        ny += 1.0f;
    }
    if (z == 0) {
        nz -= 1.0f;
    }
    if (z == last) {
        nz += 1.0f;
    }

    const int axes = (nx != 0.0f ? 1 : 0) + (ny != 0.0f ? 1 : 0) + (nz != 0.0f ? 1 : 0);
    const float scale = axes == 1 ? 1.0f : axes == 2 ? 0.7071067811865476f : axes == 3 ? 0.5773502691896258f : 0.0f;
    const float u = static_cast<float>(x) / static_cast<float>(last);
    const float v = static_cast<float>(y) / static_cast<float>(last);
    return CubeVertex{
        Ccino::Vec3(CubeGridCoord(x), CubeGridCoord(y), CubeGridCoord(z)),
        Ccino::Vec3(nx * scale, ny * scale, nz * scale),
        Ccino::Vec2(u, v),
    };
}

constexpr void PushCubeIndex(std::array<CubeIndex, kCubeIndexCount>& indices, uint32_t& cursor, uint32_t x, uint32_t y, uint32_t z) {
    indices[cursor] = CubeIndex{CubeGridIndex(x, y, z)};
    ++cursor;
}

constexpr auto MakeCubeVertices() {
    std::array<CubeVertex, kCubeVertexCount> vertices{};
    for (uint32_t z = 0; z < kCubeResolution; ++z) {
        for (uint32_t y = 0; y < kCubeResolution; ++y) {
            for (uint32_t x = 0; x < kCubeResolution; ++x) {
                vertices[CubeGridIndex(x, y, z)] = MakeCubeVertex(x, y, z);
            }
        }
    }
    return vertices;
}

constexpr auto MakeCubeIndices() {
    std::array<CubeIndex, kCubeIndexCount> indices{};
    uint32_t cursor = 0;
    const uint32_t last = kCubeResolution - 1;
    const uint32_t cells = kCubeResolution - 1;

    // +Z
    for (uint32_t y = 0; y < cells; ++y) {
        for (uint32_t x = 0; x < cells; ++x) {
            PushCubeIndex(indices, cursor, x, y, last);
            PushCubeIndex(indices, cursor, x + 1, y, last);
            PushCubeIndex(indices, cursor, x + 1, y + 1, last);
            PushCubeIndex(indices, cursor, x, y, last);
            PushCubeIndex(indices, cursor, x + 1, y + 1, last);
            PushCubeIndex(indices, cursor, x, y + 1, last);
        }
    }
    // -Z
    for (uint32_t y = 0; y < cells; ++y) {
        for (uint32_t x = 0; x < cells; ++x) {
            PushCubeIndex(indices, cursor, x, y, 0);
            PushCubeIndex(indices, cursor, x, y + 1, 0);
            PushCubeIndex(indices, cursor, x + 1, y, 0);
            PushCubeIndex(indices, cursor, x, y + 1, 0);
            PushCubeIndex(indices, cursor, x + 1, y + 1, 0);
            PushCubeIndex(indices, cursor, x + 1, y, 0);
        }
    }
    // +X
    for (uint32_t z = 0; z < cells; ++z) {
        for (uint32_t y = 0; y < cells; ++y) {
            PushCubeIndex(indices, cursor, last, y, z);
            PushCubeIndex(indices, cursor, last, y + 1, z);
            PushCubeIndex(indices, cursor, last, y, z + 1);
            PushCubeIndex(indices, cursor, last, y + 1, z);
            PushCubeIndex(indices, cursor, last, y + 1, z + 1);
            PushCubeIndex(indices, cursor, last, y, z + 1);
        }
    }
    // -X
    for (uint32_t z = 0; z < cells; ++z) {
        for (uint32_t y = 0; y < cells; ++y) {
            PushCubeIndex(indices, cursor, 0, y, z);
            PushCubeIndex(indices, cursor, 0, y, z + 1);
            PushCubeIndex(indices, cursor, 0, y + 1, z + 1);
            PushCubeIndex(indices, cursor, 0, y, z);
            PushCubeIndex(indices, cursor, 0, y + 1, z + 1);
            PushCubeIndex(indices, cursor, 0, y + 1, z);
        }
    }
    // +Y
    for (uint32_t z = 0; z < cells; ++z) {
        for (uint32_t x = 0; x < cells; ++x) {
            PushCubeIndex(indices, cursor, x, last, z);
            PushCubeIndex(indices, cursor, x, last, z + 1);
            PushCubeIndex(indices, cursor, x + 1, last, z);
            PushCubeIndex(indices, cursor, x, last, z + 1);
            PushCubeIndex(indices, cursor, x + 1, last, z + 1);
            PushCubeIndex(indices, cursor, x + 1, last, z);
        }
    }
    // -Y
    for (uint32_t z = 0; z < cells; ++z) {
        for (uint32_t x = 0; x < cells; ++x) {
            PushCubeIndex(indices, cursor, x, 0, z);
            PushCubeIndex(indices, cursor, x + 1, 0, z);
            PushCubeIndex(indices, cursor, x, 0, z + 1);
            PushCubeIndex(indices, cursor, x + 1, 0, z);
            PushCubeIndex(indices, cursor, x + 1, 0, z + 1);
            PushCubeIndex(indices, cursor, x, 0, z + 1);
        }
    }
    return indices;
}

constexpr auto kCubeVertices = MakeCubeVertices();
constexpr auto kCubeIndices = MakeCubeIndices();

static_assert(kCubeVertices.size() == 8u * 8u * 8u);
static_assert(kCubeIndices.size() == kCubeIndexCount);
