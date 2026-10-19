#pragma once
#include <type_traits>

//强类型保护，相对于uint32_t，更加安全
template <typename BitType>
struct Flags {
    using MaskType = std::underlying_type_t<BitType>; //退化到底层类型
    MaskType mask = 0;
    constexpr Flags() = default;
    constexpr Flags(BitType bit) : mask(static_cast<MaskType>(bit)) {}
    constexpr explicit Flags(MaskType flags) : mask(flags) {}
    constexpr Flags operator|(Flags other) const { return Flags(mask | other.mask); }
    constexpr Flags operator&(Flags other) const { return Flags(mask & other.mask); }
    constexpr explicit operator bool() const { return mask != 0; }
};