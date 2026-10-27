#include "Core/Texture2D.h"
#include <stdexcept>

namespace Ccino {
    namespace {
        bool IsDepthFormat(TextureFormat format) {
            return format == TextureFormat::D32_Float;
        }

        VkFormat ToVkFormat(TextureFormat format) {
            switch (format) {
            case TextureFormat::RGBA8_UNorm:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case TextureFormat::RGBA8_sRGB:
                return VK_FORMAT_R8G8B8A8_SRGB;
            case TextureFormat::RGBA16_Float:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            case TextureFormat::R32_Float:
                return VK_FORMAT_R32_SFLOAT;
            case TextureFormat::D32_Float:
                return VK_FORMAT_D32_SFLOAT;
            }
            throw std::runtime_error("Unknown texture format");
        }

        VkImageUsageFlags ToVkUsage(TextureUsage usage) {
            VkImageUsageFlags flags = 0;
            if (HasTextureUsage(usage, TextureUsage::Sampled)) {
                flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
            }
            if (HasTextureUsage(usage, TextureUsage::Storage)) {
                flags |= VK_IMAGE_USAGE_STORAGE_BIT;
            }
            if (HasTextureUsage(usage, TextureUsage::ColorAttachment)) {
                flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            }
            if (HasTextureUsage(usage, TextureUsage::DepthAttachment)) {
                flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            }
            if (HasTextureUsage(usage, TextureUsage::TransferSrc)) {
                flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }
            if (HasTextureUsage(usage, TextureUsage::TransferDst)) {
                flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }
            return flags;
        }

        void ValidateCreateArgs(const Texture2D::CreateArgs& createArgs) {
            if (createArgs.width == 0 || createArgs.height == 0) {
                throw std::runtime_error("Texture2D size must be non-zero");
            }
            if (static_cast<uint32_t>(createArgs.usage) == 0) {
                throw std::runtime_error("Texture2D usage is empty");
            }

            const bool depth = IsDepthFormat(createArgs.format);
            if (depth && HasTextureUsage(createArgs.usage, TextureUsage::ColorAttachment)) {
                throw std::runtime_error("Depth texture cannot be a color attachment");
            }
            if (depth && HasTextureUsage(createArgs.usage, TextureUsage::Storage)) {
                throw std::runtime_error("Depth texture cannot be a storage image");
            }
            if (!depth && HasTextureUsage(createArgs.usage, TextureUsage::DepthAttachment)) {
                throw std::runtime_error("Color texture cannot be a depth attachment");
            }
            if (createArgs.format == TextureFormat::RGBA8_sRGB &&
                HasTextureUsage(createArgs.usage, TextureUsage::Storage)) {
                throw std::runtime_error("sRGB texture cannot be a storage image");
            }
        }
    }

    void Texture2D::CreateImpl(RHI::Context& context, const CreateArgs& createArgs) {
        ValidateCreateArgs(createArgs);
        m_context = &context;
        m_createArgs = createArgs;
        if (m_createArgs.mipLevels == 0) {
            m_createArgs.mipLevels = 1;
        }

        RHI::TextureCreateInfo info{};
        info.width = m_createArgs.width;
        info.height = m_createArgs.height;
        info.depth = 1;
        info.mipLevels = m_createArgs.mipLevels;
        info.arrayLayers = 1;
        info.format = ToVkFormat(m_createArgs.format);
        info.type = VK_IMAGE_TYPE_2D;
        info.usage = ToVkUsage(m_createArgs.usage);
        m_rhiTexture = context.CreateTexture(info);
    }

    void Texture2D::Delete() {
        if (m_rhiTexture == RID::Invalid() || m_context == nullptr) {
            return;
        }
        m_context->DeleteResource(m_rhiTexture);
        m_rhiTexture = RID::Invalid();
    }
}
