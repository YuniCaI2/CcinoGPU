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

    //这里的Buffer只是作为存储的媒介，BufferView的功能不包含，减少模板带来的耦合

    class Buffer : public RefCounted{
    public:
        //GPU资源
        virtual ~Buffer() {
            Delete();
        }

        //两种构造方式
        static Ref<Buffer> Create(RHI::Context& context, uint32_t count, uint32_t typeSize) {
            auto buffer = Ref<Buffer>::Make();
            buffer->CreateImpl(context, count, typeSize);
            return buffer; 
        }

        static Ref<Buffer> Create(RHI::Context& context, uint32_t size) {
            auto buffer = Ref<Buffer>::Make();
            buffer->CreateImpl(context, size);
            return buffer; 
        }
        



        void Delete();
        
        //刷新对象
        void Flush();

        void Copy(const void* data);

        RID GetRHI() const { return m_rhiBuffer; }


        
    private:
        friend class Ref<Buffer>;
        Buffer() = default;

        void CreateImpl(RHI::Context& context, uint32_t count, uint32_t typeSize);
        void CreateImpl(RHI::Context& context, uint32_t size);

        uint32_t m_size = 0; //字节数为 sizeof(DataType) * count
        RID m_rhiBuffer = RID::Invalid(); //资源Buffer
        RHI::Context* m_context = nullptr;

        BufferType m_bufferType =  BufferType::Device;
    };

}