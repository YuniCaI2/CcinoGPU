#include "Demo/RayQueryDemo/Material.h"

#include "Core/Buffer.h"

namespace PBR {

    Ccino::Ref<Ccino::Texture2D> CreateSolidTexture2D(
        Ccino::Context& context,
        std::array<uint8_t, 4> rgba,
        Ccino::TextureFormat format)
    {
        auto staging = context.CreateBuffer({
            .size = 4,
            .usage = Ccino::BufferUsage::Storage,
            .hostAccess = Ccino::RHI::HostAccess::Write,
        });
        staging->Copy(rgba.data());

        auto tex = context.CreateTexture2D({
            .width = 1,
            .height = 1,
            .mipLevels = 1,
            .format = format,
            .usage = Ccino::TextureUsage::Sampled | Ccino::TextureUsage::TransferDst,
        });
        auto& rhi = context.GetRHIContext();
        rhi.CopyTexFromBuffer(tex->GetRHI(), staging->GetRHI());
        rhi.WaitResource<Ccino::RHI::VulkanBuffer>(staging->GetRHI());
        return tex;
    }

} // namespace PBR
