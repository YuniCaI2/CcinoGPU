#pragma once
#include "RHI/RHIStruct.h"
#include "RHI/Core.h"
#include "RHI/Command.h"
#include "RHI/TimelineSemaphore.h"
#include "RHI/ComputePipeline.h"

#include <concepts>
#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>


namespace Ccino::RHI {

    struct BufferCreateInfo {
        uint32_t size = 0;
        VkBufferUsageFlags2 usage = 0;
        bool hostVisible = false;
    };

    class Context {
    public:
        Context();
        ~Context() = default;

        void Init(const RHISettings& settings);
        void Tick(); //每次更新，这必须要调用，这是推动Delete释放队列的根本,这个在 !!!!帧首调用 ,不过需要SwapChain Acquire 后调用
        void Destroy();

        //Resource
        void DeleteResource(RID rid);

        //meta
        uint32_t GetFlightFrameCount() const { return m_flightFrameCount; }

        //Buffer 默认使用BDA
        RID CreateBuffer(const BufferCreateInfo& createInfo); //这里使用新的的BufferUsageFlags2
        RID CreateUniformBuffer(uint32_t size, const void* data); //Uniform Buffer 
        RID CreateStorageBuffer(uint32_t size, const void* data);
        RID CreateTransferBuffer(uint32_t size, const void* data); //StagingBuffer用来上传
        RID CreateReadbackBuffer(uint32_t size); //CPU 可见，用来把 Local Buffer 读回
        void ReadbackBuffer(RID src, RID dst); //Transfer 队列从 src 拷到 Readback Buffer
        void UploadBuffer(RID rid, const void* data, uint32_t size);
        bool IsBufferAvailable(RID rid); //使用value来判断是否可用
        void* MapBuffer(RID rid); //必须使用可以支持CPU映射的Buffer
        void UnmapBuffer(RID rid);
        void WaitForUploadStaging(RID rid);

        void DestroyBuffer(RID rid);

        bool GetBufferUsage(RID rid, VkBufferUsageFlags2& usage);
        bool GetImageDescriptorType(RID rid, VkDescriptorType& type);
        bool GetSamplerDescriptorType(RID rid, VkDescriptorType& type);
        uint64_t GetBufferBDA(RID rid) {return m_buffers.Get(rid)->bda;}
        void* GetBufferMappedPtr(RID rid) {return m_buffers.Get(rid)->mappedPtr;}
        uint32_t GetBufferSize(RID rid) {return m_buffers.Get(rid)->size;}

        Core& GetCore() { return m_core; }


        //Semphore
        RID CreateSemaphore();
        void DestroySemaphore(RID rid);

        //Global Timeline Semaphore
        void CreateGlobalTimelineSemaphore(); //尽可能不让外部感知到这个信号量，这里不用RID管理
        void DestroyGlobalTimelineSemaphore();
        bool IsGlobalTimelineSemaphoreAvailable(CommandQueueType queueType, uint64_t value); //使用value来判断是否可用

        //Texture

        //Command
        CommandContext::CommandBuffer& GetCommandBuffer(CommandQueueType queueType, uint32_t frameIndex = 0);
        void StartRecording(CommandContext::CommandBuffer& commandBuffer);
        void EndRecording(CommandContext::CommandBuffer& commandBuffer);

        void SubmitCommandBuffer(CommandQueueType queueType, const std::vector<CommandContext::CommandBuffer>& commandBuffers, uint64_t signalValue = 0, 
            const std::vector<RID>& waitSemaphores = {}, const std::vector<RID>& signalSemaphores = {}, const std::vector<VkPipelineStageFlags2> pipelineStageFlags = {});
       
        template<typename Fn>
        requires std::invocable<Fn, CommandContext::CommandBuffer&>
        void ExecuteCommand(Fn&& fn, CommandQueueType queueType) {
            CommandContext::CommandBuffer& commandBuffer = GetCommandBuffer(queueType, 0);
            StartRecording(commandBuffer);
            fn(commandBuffer);
            EndRecording(commandBuffer);
            SubmitCommandBuffer(queueType, {commandBuffer}, {}, {});
        } //立即执行并且提交到队列中


        //这里的VkShahderModule需要使用复杂的方法得到这里作为RHI层不需要管理Shader IR 到spv的过程，只作为消费端
        //Create Compute Pipline
        RID CreateComputePipeline(const std::string& name, VkShaderModule shaderModule);
        void DestroyComputePipeline(RID rid);
        VkPipeline GetComputePipeline(RID rid);


    private:
        //meta
        uint32_t m_flightFrameCount = 0;
        uint32_t m_currentFrameIndex = 0;

        struct VulkanBuffer {
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;

            //MetaInfo
            uint32_t size = 0;
            VkBufferUsageFlags2 usage = 0;

            void* mappedPtr = nullptr;
            bool hostVisible = false;
            bool persistentlyMapped = false;
            uint64_t bda = 0; //GPU的地址

            // Transfer 队列 timeline 的目标值。0 表示没有未完成的 DMA 上传。
            uint64_t isAvailable = 0;
            RID uploadStaging{};
        };

        void DestroyResource(RID rid); 
        uint64_t SubmitCommandBufferTracked(CommandQueueType queueType, const std::vector<CommandContext::CommandBuffer>& commandBuffers, 
            uint64_t signalValue, const std::vector<RID>& waitSemaphores, const std::vector<RID>& signalSemaphores, const std::vector<VkPipelineStageFlags2>& pipelineStageFlags);
        void ReleaseUploadStaging(VulkanBuffer& buffer);
        TimelineSemaphore* GetTimeline(uint32_t queueFamilyIndex);


        Core m_core;
        CommandContext m_commandContext;

        TRIDArray<VulkanBuffer> m_buffers;
        TRIDArray<VkSemaphore> m_semaphores;
        TRIDArray<ComputePipeline> m_computePipelines;

        std::vector<std::vector<RID>> m_bufferDeletionQueue; //延迟释放队列

        // 按队列家族索引。每个 TimelineSemaphore 自己保护目标值。
        std::vector<std::unique_ptr<TimelineSemaphore>> m_timelines;



    };
}
