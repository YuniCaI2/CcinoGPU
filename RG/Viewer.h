#pragma once
#include "AccessorEnums.h"
#include "CcinoType.h"
#include "RHI/RHIStruct.h"
#include "Texture2D.h"
#include <array>
#include <stdexcept>
#include <variant>

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

    enum class LoadOp {
        Load,     //保留附件之前的内容
        Clear,    //用 clear 值清空
        DontCare, //之前的内容不需要, 整个区域都会被覆盖时使用
    };

    enum class StoreOp {
        Store,
        DontCare, //渲染结束后内容不再需要
    };

    struct ClearColor {
        std::array<float, 4> color{};
    };

    struct ClearDepthStencil {
        float depth;
        uint32_t stencil;
    };

    using ClearValue = std::variant<ClearColor, ClearDepthStencil>;

    //颜色附件, 只能由 RGBuilder::RTView 创建, 交给 AddVSAndFS 消费
    class RenderTarget {
    public:
        RenderTarget() = default;

    private:
        friend class RGBuilder;
        RenderTarget(RID resource, ViewRange viewRange, LoadOp loadOp, StoreOp storeOp, ClearColor clear)
            : m_resource(resource), m_viewRange(viewRange), m_loadOp(loadOp), m_storeOp(storeOp), m_clear(clear) {}

        RID m_resource = RID::Invalid();
        ViewRange m_viewRange{};
        LoadOp m_loadOp = LoadOp::Load;
        StoreOp m_storeOp = StoreOp::Store;
        ClearColor m_clear{}; //只在 m_loadOp 为 Clear 时生效
        ClearValue m_clearValue{}; //这里是为了处理两种不同的Depth
    };

}