#pragma once
#include "RHI/RHI.h"
#include "RHI/Vulkan/Core.h"

namespace Ccino::RHI::Vulkan {
    class Context : public RHI::Context {
    public:
        Context() = default;
        ~Context() = default;

        void Init(const RHISettings& settings) override;
        void Destroy() override;

        virtual RID CreateBuffer(const BufferCreateInfo& createInfo) override;
        virtual void DestroyBuffer(RID rid) override;
    private:
        Core m_core;
        std::vector<VkBuffer> m_buffers; //这里直接存m_buffers;
    };
}