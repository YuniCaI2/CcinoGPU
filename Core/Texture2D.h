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

            bool operator==(const CreateArgs&) const = default;
        };

        static Ref<Texture2D> Create(RHI::Context& context, const CreateArgs& createArgs) {
            auto texture = Ref<Texture2D>::Make();
            texture->CreateImpl(context, createArgs);
            return texture;
        }

        void Delete();
        //整张纹理按紧密排列读回到 data, 纹理需要 TransferSrc usage
        bool Readback(void* data);

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

//处理CreateArgs Hash
namespace std {
    template<>
    struct hash<Ccino::Texture2D::CreateArgs> {
        size_t operator()(const Ccino::Texture2D::CreateArgs& args) const noexcept {
            auto combine = [](uint64_t seed, uint64_t value) noexcept {
                return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
            };

            uint64_t h = args.width;
            h = combine(h, args.height);
            h = combine(h, args.mipLevels);
            h = combine(h, static_cast<uint64_t>(args.format));
            h = combine(h, static_cast<uint64_t>(args.usage));
            return static_cast<size_t>(h);
        }
    };
}
