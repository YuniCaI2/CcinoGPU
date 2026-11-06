#pragma once
#include "AccessorEnums.h"
#include "CcinoType.h"
#include "RHI/RHIStruct.h"
#include "Texture2D.h"
#include <stdexcept>

namespace Ccino {
    using RHI::AspectMask;
    using RHI::HasAspectMask;
    using RHI::ToVkAspectFlags;
    using RHI::ViewRange;

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


    inline constexpr ViewType TextureFormatToViewType(TextureFormat textureFormat) {
        switch (textureFormat) {
            case Ccino::TextureFormat::RGBA8_sRGB:
            case Ccino::TextureFormat::RGBA8_UNorm:
            case Ccino::TextureFormat::R32_Float:
            case Ccino::TextureFormat::RGBA16_Float:
            case Ccino::TextureFormat::D32_Float:
                return ViewType::Float;
        }
        throw std::invalid_argument("Unknown texture format");
    }
    

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


    class RGBuilder;
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
        friend class RGBuilder;
        RID m_resource = RID::Invalid();
        ViewRange m_viewRange {};
    };
}