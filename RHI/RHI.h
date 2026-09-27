#pragma once
#include "RHI/RHIStruct.h"

//Resource Layout
//PushConstant 传输资源句柄 Uniform BDA SSBO BDA  Texture Index ... //单Pass传输一个
//UniformBuffer Set = 0 binding = 0 

namespace Ccino::RHI {
    class Context {
    public:
        virtual void Init(const RHISettings& settings) = 0; // TODO: 传入一些初始化参数，用来处理Vulkan Feature
        virtual void Destroy() = 0;
        virtual ~Context() = default;

        //此处使用Bindless所以资源使用索引代替原本的Vulkan的资源句柄
        //Buffer
        virtual RID CreateBuffer(const BufferCreateInfo& createInfo) = 0;
        virtual void DestroyBuffer(RID rid) = 0;
        //Image
        //ImageView
        //Sampler
        //Semaphore

        //Bindless Descriptor

        //Pipeline

        //Command

        //Viewport And SwapChain //TODO: 初始化的时候可以决定是否需要SwapChain,因为
    private:
        //资源释放使用队列保持，可以让外层设置释放索引
    };
}