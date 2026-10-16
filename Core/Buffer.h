#pragma once
#include <type_traits>
#include <stdint.h>
#include <iostream>
#include <vulkan/vulkan_core.h>
#include "RHI/Context.h"
#include "RefCounted.hpp"



namespace Ccino {
    enum class BufferType {
        Device,
        Host
    };

    template<typename DataType, BufferType bufferType>  //DataType是方便在Lambda反射时使用
    requires std::is_trivially_copyable_v<DataType> //这个对象一定是平凡的
    class Buffer : RefCounted{
    public:
        //GPU资源
        ~Buffer() {
            Delete();
        }

        //模版改造
        static Ref<Buffer> Create(RHI::Context& context, uint32_t count) {
            auto buffer = Ref<Buffer>::Make();
            buffer->CreateImpl(context, count);
            return buffer; 
        }
        



        void Delete() {
            if (m_rhiBuffer == RID::Invalid()) {
                return;
            }
            m_context->DeleteResource(m_rhiBuffer);
            m_rhiBuffer = RID::Invalid();
        }
        
        //刷新对象
        void Flush() {
            if (m_rhiBuffer == RID::Invalid()) {
                std::cerr << "The Buffer RID Is Invild" << std::endl;
                return;
            }
            m_context->WaitForUploadStaging(m_rhiBuffer);
        }

        void Copy(const DataType& data) {
            if (m_rhiBuffer == RID::Invalid()) {
                std::cerr << "The Buffer RID Is Invild" << std::endl;
                return;
            }
            if constexpr (bufferType == BufferType::Device) {
                m_context->UploadBuffer(m_rhiBuffer, &data, m_size * sizeof(DataType));
            } else if constexpr (bufferType == BufferType::Host) {
                //直接copy即可
                std::memcpy(m_context->GetBufferMappedPtr(m_rhiBuffer), &data, m_size * sizeof(DataType));
            } else {
                static_assert(false, "Invalid buffer type");
            }
        }

        //TODO实现Accessor

        
    private:
        Buffer() = default;

        void CreateImpl(RHI::Context& context, uint32_t count) {
            m_size = sizeof(DataType) * count;
            m_context = &context;
            //这里都不直接Copy
            if constexpr (bufferType == BufferType::Device) {
                m_rhiBuffer = context.CreateStorageBuffer(m_size * sizeof(DataType), nullptr);
            } else if constexpr (bufferType == BufferType::Host) {
                m_rhiBuffer = context.CreateUniformBuffer(m_size * sizeof(DataType), nullptr);
            } else {
                static_assert(false, "Invalid buffer type");
            }
        }

        uint32_t m_size; //字节数为 sizeof(DataType) * count
        RID m_rhiBuffer; //资源Buffer
        RHI::Context* m_context;
        using dataType_t = DataType; //数据类型
    };

}