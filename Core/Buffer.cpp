#include "Core/Buffer.h"

namespace Ccino {
    void Buffer::CreateImpl(RHI::Context& context, uint32_t count, uint32_t typeSize) {

        m_size = typeSize * count;
        m_context = &context;
        //这里都不直接Copy
        if (m_bufferType == BufferType::Device) {
            m_rhiBuffer = context.CreateStorageBuffer(m_size, nullptr);
        } else if (m_bufferType == BufferType::Host) {
            m_rhiBuffer = context.CreateUniformBuffer(m_size, nullptr);
        } else {
            std::cerr << "Invalid buffer type" << std::endl;
            return;
        }
    }

    void Buffer::CreateImpl(RHI::Context& context, uint32_t size) {
        m_size = size;
        m_context = &context;
        //这里都不直接Copy
        if (m_bufferType == BufferType::Device) {
            m_rhiBuffer = context.CreateStorageBuffer(m_size, nullptr);
        } else if (m_bufferType == BufferType::Host) {
            m_rhiBuffer = context.CreateUniformBuffer(m_size, nullptr);
        } else {
            std::cerr << "Invalid buffer type" << std::endl;
            return;
        }
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
        if (m_bufferType == BufferType::Device) {
            m_context->UploadBuffer(m_rhiBuffer, data, m_size);
        } else if (m_bufferType == BufferType::Host) {
            //直接copy即可
            std::memcpy(m_context->GetBufferMappedPtr(m_rhiBuffer), data, m_size);
        } else {
            std::cerr << "Invalid buffer type" << std::endl;
            return;
        }
    }
}