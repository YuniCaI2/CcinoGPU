#pragma once
#include <cstdint>
#include "RHI/Context.h"
#include "RefCounted.hpp"

namespace Ccino {
    enum class TextureFormat {
        RGBA8_UNorm,
        RGBA8_sRGB,
        RGBA16_Float,
        R32_Float,
        D32_Float,
    };

    //采样、存储、附件和传输可以组合。Cube 是图像形状，不放在这里。
    enum class TextureUsage : uint32_t {
        Sampled         = 1u << 0,
        Storage         = 1u << 1,
        ColorAttachment = 1u << 2,
        DepthAttachment = 1u << 3,
        TransferSrc     = 1u << 4,
        TransferDst     = 1u << 5,
    };

    constexpr TextureUsage operator|(TextureUsage lhs, TextureUsage rhs) {
        return static_cast<TextureUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    constexpr TextureUsage operator&(TextureUsage lhs, TextureUsage rhs) {
        return static_cast<TextureUsage>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
    }

    constexpr bool HasTextureUsage(TextureUsage usage, TextureUsage bit) {
        return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(bit)) != 0;
    }

    //和 RHI 创建时传入的 VK_IMAGE_TYPE_2D 对齐
    //仿照 Buffer: 只作为存储媒介, Format/Usage 都是运行期 CreateArgs, 不合法组合运行期抛异常
    class Texture2D : public RefCounted {
    public:
        virtual ~Texture2D() {
            Delete();
        }

        struct CreateArgs {
            uint32_t width = 1;
            uint32_t height = 1;
            uint32_t mipLevels = 1;
            TextureFormat format = TextureFormat::RGBA8_sRGB;
            TextureUsage usage = TextureUsage::Sampled | TextureUsage::TransferDst;
        };

        static Ref<Texture2D> Create(RHI::Context& context, const CreateArgs& createArgs) {
            auto texture = Ref<Texture2D>::Make();
            texture->CreateImpl(context, createArgs);
            return texture;
        }

        void Delete();

        RID GetRHI() const { return m_rhiTexture; }
        CreateArgs GetCreateArgs() const { return m_createArgs; }

    private:
        friend class Ref<Texture2D>;
        Texture2D() = default;

        void CreateImpl(RHI::Context& context, const CreateArgs& createArgs);

        RID m_rhiTexture = RID::Invalid();
        RHI::Context* m_context = nullptr;
        CreateArgs m_createArgs{};
    };
}
