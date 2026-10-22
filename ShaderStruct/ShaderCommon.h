#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace Ccino {
    enum class ShaderType {
        Compute,
        Vertex,
        Fragment
    };

    NLOHMANN_JSON_SERIALIZE_ENUM_STRICT(ShaderType, {
        {ShaderType::Compute,  "Compute"},
        {ShaderType::Vertex,   "Vertex"},
        {ShaderType::Fragment, "Fragment"},
    })

    struct ThreadGroupSize {
        uint32_t x = 1;
        uint32_t y = 1;
        uint32_t z = 1;

        constexpr bool operator==(const ThreadGroupSize&) const = default;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ThreadGroupSize, x, y, z)

    struct WorkGroupSize {
        uint32_t x = 1;
        uint32_t y = 1;
        uint32_t z = 1;

        constexpr bool operator==(const WorkGroupSize&) const = default;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WorkGroupSize, x, y, z)

    struct ComputeGlobalID {
        uint32_t x;
        uint32_t y;
        uint32_t z;
    };

    // C++20 结构化字符串包装，支持作为非类型模板参数 (NTTP)，如 AddPass<"MyPass">
    template <std::size_t N>
    struct FixedString {
        char data[N]{};

        constexpr FixedString(const char (&str)[N]) {
            std::copy_n(str, N, data);
        }

        constexpr std::string_view View() const {
            return std::string_view(data, N - 1);
        }

        std::string Str() const {
            return std::string(data, N - 1);
        }
    };
}
