#pragma once
#include "AccessorEnums.h"
#include "CcinoType.h"
#include "RHI/RHIStruct.h"

namespace Ccino {
    //和 VkImageAspectFlagBits 对齐: 枚举值直接等于 Vulkan 的位值, 可直接 static_cast 成 VkImageAspectFlags
    enum class AspectMask : uint32_t {
        None    = VK_IMAGE_ASPECT_NONE,
        Color   = VK_IMAGE_ASPECT_COLOR_BIT,
        Depth   = VK_IMAGE_ASPECT_DEPTH_BIT,
        Stencil = VK_IMAGE_ASPECT_STENCIL_BIT,
    };

    constexpr AspectMask operator|(AspectMask lhs, AspectMask rhs) {
        return static_cast<AspectMask>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    constexpr AspectMask operator&(AspectMask lhs, AspectMask rhs) {
        return static_cast<AspectMask>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
    }

    constexpr bool HasAspectMask(AspectMask mask, AspectMask bit) {
        return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(bit)) != 0;
    }

    constexpr VkImageAspectFlags ToVkAspectFlags(AspectMask mask) {
        return static_cast<VkImageAspectFlags>(mask);
    }

    //Viewer 只处理StorageImage， SampledImage使用Sampler
    struct ViewRange {
        uint32_t baseMipMap = 0;
        uint32_t mipMapCounts = 1;
        uint32_t baseArray = 0;
        uint32_t arrayCounts = 1;
        AspectMask aspectMask = AspectMask::Color;
    };

    enum class ViewType {
        Float,
        Int,
        UInt
    };

    template<ViewType viewType>
    struct ViewTypeToCppType {
    };

    template <>
    struct ViewTypeToCppType<ViewType::Float> {
        using Type = Vec4;
    };

    template <>
    struct ViewTypeToCppType<ViewType::Int> {
        using Type = IVec4;
    };

    template <>
    struct ViewTypeToCppType<ViewType::UInt> {
        using Type = UVec4;
    };

    //完全和RHI对齐即可
    enum class SamplerType {
        LINEAR_REPEAT = 0,
        LINEAR_CLAMP_TO_EDGE = 1,
        LINEAR_CLAMP_TO_BORDER = 2,
        LINEAR_MIRRORED_REPEAT = 3,
        NEAREST_REPEAT = 4,
        NEAREST_CLAMP_TO_EDGE = 5,
        NEAREST_CLAMP_TO_BORDER = 6,
        NEAREST_MIRRORED_REPEAT = 7,
        COUNT
    };



    //先只考虑2D， 3D纹理后续再扩展，太复杂
    template<TextureAccessFlags textureAccessFlags, ViewType viewType = ViewType::Float>
    class Viewer {
    public:
        Viewer(const RID resource, ViewRange viewRange) {
            m_resource = resource;
            m_viewRange = viewRange;
        }
        using Texel = typename ViewTypeToCppType<viewType>::Type;
        
        Texel Load(IVec2 dummy) const
            requires TextureAccessHas<textureAccessFlags, TextureAccessFlagBits::StorageRead>
        {
            return Texel{};
        }

        void Store(IVec2 dummy, Texel tex) const
            requires TextureAccessHas<textureAccessFlags, TextureAccessFlagBits::StorageWrite>
        {

        }

        Texel Sample(Vec2 dummy, SamplerType samplerType = SamplerType::LINEAR_CLAMP_TO_BORDER, float lod = 0.0f) const
            requires TextureAccessHas<textureAccessFlags, TextureAccessFlagBits::Sampled>
        {
            return Texel{};
        }

        //TODO: 支持Array
    private:
        RID m_resource = RID::Invalid();
        ViewRange m_viewRange {};
    };
}