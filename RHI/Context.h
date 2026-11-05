#pragma once
#include "GraphicsShaderObject.h"
#include "RHI/RHIStruct.h"
#include "RHI/Command.h"
#include "RHI/TimelineSemaphore.h"
#include "RHI/ComputePipeline.h"
#include "RHI/DescriptorSlotBinding.h"
#include "ShaderStruct/UniformData.h"

#include <concepts>
#include <memory>
#include <mutex>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>


namespace Ccino::RHI {

    class Core;
    class CommandContext;
    class DescriptorSlot;
    class Swapchain;

    struct BufferCreateInfo {
        uint32_t size = 0;
        VkBufferUsageFlags2 usage = 0;
        bool hostVisible = false;
        bool requireHostCoherent = false; //要求落在 HOST_VISIBLE | HOST_COHERENT 内存上，选不到时创建失败，不做降级
    };

    //Texture比较复杂有各种类型的不同Format的
    //先不支持复杂度Array
    struct TextureCreateInfo {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageType type = VK_IMAGE_TYPE_2D;
        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
        VkImageUsageFlags usage = 0;
        //Layout 初始都是未定义
    };

    class Context {
    public:
        Context();
        ~Context();

        void Init(const RHISettings& settings);
        void Tick(); //每次更新，这必须要调用，这是推动Delete释放队列的根本,这个在 !!!!帧首调用 ,不过需要SwapChain Acquire 后调用
        void Destroy();

        //Resource
        void DeleteResource(RID rid);

        //meta
        uint32_t GetFlightFrameCount() const { return m_flightFrameCount; }
        uint32_t GetCurrentFrameIndex() const {return m_currentFrameIndex; }

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
        VkBuffer GetVkBuffer(RID rid) {return m_buffers.Get(rid)->buffer;}

        Core& GetCore() { return *m_core; }


        //Semphore
        RID CreateSemaphore();
        void DestroySemaphore(RID rid);

        //Global Timeline Semaphore
        void CreateGlobalTimelineSemaphore(); //尽可能不让外部感知到这个信号量，这里不用RID管理
        void DestroyGlobalTimelineSemaphore();
        bool IsGlobalTimelineSemaphoreAvailable(CommandQueueType queueType, uint64_t value); //使用value来判断是否可用

        //Texture
        RID CreateTexture(const TextureCreateInfo& createInfo);
        void DestroyTexture(RID rid);
        void TransitionTextureLayout(RID rid, VkImageLayout newLayout);
        VkImage GetVkImage(RID rid) { return m_textures.Get(rid)->image; }
        uint32_t GetTextureSize(RID rid);
        VkFormat GetFormat(RID textureRID) {return m_textures.Get(textureRID)->format;}
        VkImageLayout GetTextureLayout(RID textureRID) { return m_textures.Get(textureRID)->layout; }
        void SetTextureLayout(RID textureRID, VkImageLayout layout) { m_textures.Get(textureRID)->layout = layout; }

        //不支持多MipmapLevel TODO：完善对多格式和布局的TextureCopy的支持
        bool CopyTexFromBuffer(RID dstTex, RID srcBuffer); //先只支持全量copy
        bool CopyTexToBuffer(RID srcTex, RID dstBuffer);
        bool CopyTex(RID srcTex, RID dstTex);


        //ImageView 直接传入VkImageViewCreateInfo 创建, image 由调用方从GetVkImage 填入
        RID CreateImageView(const VkImageViewCreateInfo& createInfo);
        void DestroyImageView(RID rid);
        VkImageView GetVkImageView(RID rid) { return *m_imageViews.Get(rid); }

        std::vector<RID> RegisterSwapchainImages(const std::vector<VkImage>& images);
        void UnRegisterSwapchainImages(const std::vector<RID>& imageRIDs);

        //Command
        CommandContext::CommandBuffer& GetCommandBuffer(CommandQueueType queueType, uint32_t frameIndex = 0);
        void StartRecording(CommandContext::CommandBuffer& commandBuffer);
        void EndRecording(CommandContext::CommandBuffer& commandBuffer);
        // void BeginRendering(CommandContext::CommandBuffer& commandBuffer);
        // void EndRendering(CommandContext::CommandBuffer& commandBuffer);

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
        RID CreateComputePipeline(const std::string& name, void* shaderCode, uint32_t size);
        void DestroyComputePipeline(RID rid);
        VkPipeline GetComputePipeline(RID rid);

        //Create GraphicsShaderObject,这里就VS和PS先绑死
        RID CreateGraphicsShaderObject(const std::string& name, void* vertCode, uint32_t vertSize, void* fragCode, uint32_t fragSize);
        void DestroyGraphicsShaderObject(RID rid);
        GraphicsShaderObject* GetGraphicsShaderObject(RID rid);

        //Descriptor
        RID CreateDescriptorSlotBinding(const DescriptorSlotLayout& descriptorSlotLayout);
        void DestroyDescriptorSlotBinding(RID rid);
        DescriptorSlotBinding* GetDescriptorSlotBinding(const RID& rid) {
            return m_descriptorSlots.Get(rid);
        }
        void BindDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE) const;


    private:
        //meta
        uint32_t m_flightFrameCount = 0;
        uint32_t m_currentFrameIndex = 0;

        struct VulkanResource {
            // 最近一次 GPU 使用所在的队列，以及那次提交的 timeline 目标值。0 表示还没有 GPU 使用。
            CommandQueueType lastQueue = CommandQueueType::GRAPHICS;
            uint64_t lastTimelineValue = 0;
            RID uploadStaging{};
        };

        struct VulkanBuffer : VulkanResource {
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;

            //MetaInfo
            uint32_t size = 0;
            VkBufferUsageFlags2 usage = 0;

            void* mappedPtr = nullptr;
            bool hostVisible = false;
            bool hostCoherent = false;
            bool persistentlyMapped = false;
            uint64_t bda = 0; //GPU的地址

        };

        struct VulkanTexture : VulkanResource {
            VkImage image = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;

            //MetaInfo
            bool isSwapchain = false;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t depth = 1;
            uint32_t mipLevels = 1;
            uint32_t arrayLayers = 1;
            VkFormat format = VK_FORMAT_UNDEFINED;
            VkImageType type = VK_IMAGE_TYPE_2D;
            VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageUsageFlags usage = 0;
        };

        void DestroyResource(RID rid); 
        uint64_t SubmitCommandBufferTracked(CommandQueueType queueType, const std::vector<CommandContext::CommandBuffer>& commandBuffers, 
            uint64_t signalValue, const std::vector<RID>& waitSemaphores, const std::vector<RID>& signalSemaphores, const std::vector<VkPipelineStageFlags2>& pipelineStageFlags);
        void ReleaseUploadStaging(VulkanBuffer& buffer);
        TimelineSemaphore* GetTimeline(uint32_t queueFamilyIndex);


        //其实这里需要使用unique比较合适，然后前向声明
        std::unique_ptr<Core> m_core;
        std::unique_ptr<CommandContext> m_commandContext;
        std::unique_ptr<DescriptorSlot> m_descriptorSlot;
        std::unique_ptr<Swapchain> m_swapchain;


        TRIDArray<VulkanBuffer> m_buffers;
        TRIDArray<VkSemaphore> m_semaphores;
        TRIDArray<VkImageView> m_imageViews; //轻量包装托管到RID中 ，后续再DescriptorSlot中使用
        TRIDArray<ComputePipeline> m_computePipelines;
        TRIDArray<DescriptorSlotBinding> m_descriptorSlots;
        TRIDArray<VulkanTexture> m_textures;
        TRIDArray<GraphicsShaderObject> m_graphicsShaderObjects;

        std::mutex m_deletionMutex; //保护m_bufferDeletionQueue 和m_currentFrameIndex 的切换, Ref 可能在任意线程析构
        std::vector<std::vector<RID>> m_bufferDeletionQueue; //延迟释放队列

        // 按队列家族索引。每个 TimelineSemaphore 自己保护目标值。
        std::vector<std::unique_ptr<TimelineSemaphore>> m_timelines;



    };
}
