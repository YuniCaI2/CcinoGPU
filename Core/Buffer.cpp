#include "Core/Buffer.h"
#include "RHI/Context.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace Ccino {
    namespace {
        VkBufferUsageFlags2 ToVkBufferUsage(BufferUsage usage) {
            //HostAccess 覆盖不到的方向会走 Transfer 队列中转，两个方向都带上
            constexpr VkBufferUsageFlags2 transfer = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
            switch (usage) {
            case BufferUsage::Storage:
                return VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | transfer;
            case BufferUsage::Uniform:
                return VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT | transfer;
            }
            throw std::invalid_argument("Unsupported buffer usage");
        }
    }

    void Buffer::CreateImpl(RHI::Context& context, const CreateArgs& createArgs) {

        m_size = createArgs.size;
        m_createArgs = createArgs; 
        m_context = &context;

        RHI::BufferCreateInfo createInfo{};
        createInfo.size = createArgs.size;
        createInfo.usage = ToVkBufferUsage(createArgs.usage);
        createInfo.access = createArgs.hostAccess;
        m_rhiBuffer = context.CreateBuffer(createInfo);
    }


    void Buffer::Delete() {
        if (m_rhiBuffer == RID::Invalid()) {
            return;
        }
        m_context->DeleteResource(m_rhiBuffer);
        m_rhiBuffer = RID::Invalid();
    }
    
    void Buffer::Flush() {
        if (m_rhiBuffer == RID::Invalid()) {
            std::cerr << "The Buffer RID Is Invild" << std::endl;
            return;
        }
        m_context->WaitForUploadStaging(m_rhiBuffer);
    }

    void Buffer::Copy(const void* data) {
        if (m_rhiBuffer == RID::Invalid()) {
            std::cerr << "The Buffer RID Is Invild" << std::endl;
            return;
        }
        if (!RHI::HasHostAccess(m_createArgs.hostAccess, RHI::HostAccess::Write)) {
            m_context->UploadBuffer(m_rhiBuffer, data, m_size);
            return;
        }
        void* mapped = m_context->MapBuffer(m_rhiBuffer);
        if (mapped == nullptr) {
            return;
        }
        std::memcpy(mapped, data, m_size);
        m_context->UnmapBuffer(m_rhiBuffer);
    }

    void Buffer::Readback(void* data) {
        if (m_rhiBuffer == RID::Invalid() || data == nullptr) {
            std::cerr << "The Buffer RID or destination data pointer is invalid" << std::endl;
            return;
        }
        if (RHI::HasHostAccess(m_createArgs.hostAccess, RHI::HostAccess::Read)) {
            m_context->WaitResource<RHI::VulkanBuffer>(m_rhiBuffer);
            void* mapped = m_context->MapBuffer(m_rhiBuffer);
            if (mapped != nullptr) {
                std::memcpy(data, mapped, m_size);
                m_context->UnmapBuffer(m_rhiBuffer);
            }
            return;
        }
        RID readbackRID = m_context->CreateReadbackBuffer(m_size);
        m_context->ReadbackBuffer(m_rhiBuffer, readbackRID);
        while (!m_context->IsBufferAvailable(readbackRID)) {
        }
        void* mapped = m_context->MapBuffer(readbackRID);
        if (mapped != nullptr) {
            std::memcpy(data, mapped, m_size);
            m_context->UnmapBuffer(readbackRID);
        }
        m_context->DestroyBuffer(readbackRID);
    }
}
