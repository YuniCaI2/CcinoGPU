#include "Core/Buffer.h"
#include <iostream>

namespace Ccino {
    void Buffer::CreateImpl(RHI::Context& context, const CreateArgs& createArgs) {

        m_size = createArgs.size;
        m_bufferType = createArgs.bufferType;
        m_createArgs = createArgs; 
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

    void Buffer::Readback(void* data) {
        if (m_rhiBuffer == RID::Invalid() || data == nullptr) {
            std::cerr << "The Buffer RID or destination data pointer is invalid" << std::endl;
            return;
        }
        if (m_bufferType == BufferType::Device) {
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
        } else if (m_bufferType == BufferType::Host) {
            std::memcpy(data, m_context->GetBufferMappedPtr(m_rhiBuffer), m_size);
        } else {
            std::cerr << "Invalid buffer type" << std::endl;
        }
    }
}