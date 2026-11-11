#pragma once
#include "Core/Flags.h"
#include "GraphicsShaderObject.h"
#include "RHI/RHIStruct.h"
#include "RHI/Command.h"
#include "RHI/TimelineSemaphore.h"
#include "RHI/ComputePipeline.h"
#include "RHI/DescriptorSlotBinding.h"
#include "ShaderStruct/UniformData.h"

#include <concepts>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

union SDL_Event;


namespace Ccino::RHI {

    class Core;
    class CommandContext;
    class DescriptorSlot;
    class Swapchain;

    //这个是决策底层Buffer 的内存行为
    enum class HostAccess : uint8_t {
        None = 0,
        Write = 1 << 0,
        Read = 1 << 1,
        ReadWrite = Write | Read
    };

    constexpr bool HasHostAccess(HostAccess access, HostAccess bit) {
        return (static_cast<uint8_t>(access) & static_cast<uint8_t>(bit)) != 0;
    }

    struct BufferCreateInfo {
        uint32_t size = 0;
        VkBufferUsageFlags2 usage = 0;
        HostAccess access = HostAccess::None;
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



    struct AttachmentInfo {
        RID textureView;
        VkAttachmentLoadOp       loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        VkAttachmentStoreOp      storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkClearValue clearValue{}; //颜色附件填 color, 深度附件填 depthStencil, 只在 loadOp 为 CLEAR 时生效
    };

    struct RenderDesc {
        //如果width 和 height有值为0则使用附件尺寸
        uint32_t width = 0;
        uint32_t height = 0;
        int32_t offsetX = 0;
        int32_t offsetY = 0;
        uint32_t layerCount = 1;
        std::vector<AttachmentInfo> colorAttachments;
        AttachmentInfo depthAttachment; //textureView 为 Invalid 表示没有深度附件
    };

    struct VulkanResource {
        // 最近一次 GPU 使用所在的队列，以及那次提交的 timeline 目标值。0 表示还没有 GPU 使用。
        virtual ~VulkanResource() = default;
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

    struct TextureView {
        VkImageView imageView; //这里是本体
        RID imageRID; //这里指向原来的view
        ViewRange range; //附件尺寸按 range.baseMipMap 从纹理尺寸推出
    };

    class Context {
    public:
        Context();
        ~Context();


        void Init(const RHISettings& settings);
        void Tick(); //每帧必须调用一次, 充当飞行帧的Fence: 推进帧索引, 等上一次使用新帧槽位的全部提交完成, 再释放它的延迟删除队列. 在帧尾(PresentFrame 之后)、下一次PrepareFrame 之前调用
        void Destroy();

        //Resource
        void DeleteResource(RID rid);
        template<typename R>
        requires std::derived_from<R, VulkanResource>
        void WaitResource(RID rid) {
            if constexpr (std::is_same_v<R, VulkanBuffer>) {
                WaitResource_Impl(m_buffers.Get(rid));
            }
            if constexpr (std::is_same_v<R, VulkanTexture>) {
                WaitResource_Impl(m_textures.Get(rid));
            }
        }


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
        VkSemaphore* GetVkSemaphore(RID rid) { return m_semaphores.Get(rid); }
        void CreateGlobalBinarySemaphores();
        void DestroyGlobalBinarySemaphores();
        void RecreateGlobalBinarySemaphores();
        VkSemaphore GetImageReadySemaphore() const;
        VkSemaphore GetPresentReadySemaphore() const;


        //Global Timeline Semaphore
        void CreateGlobalTimelineSemaphore(); //尽可能不让外部感知到这个信号量，这里不用RID管理
        void DestroyGlobalTimelineSemaphore();
        bool IsGlobalTimelineSemaphoreAvailable(CommandQueueType queueType, uint64_t value); //使用value来判断是否可用

        //Texture
        RID CreateTexture(const TextureCreateInfo& createInfo);
        void DestroyTexture(RID rid);
        void TransitionTextureLayout(RID rid, VkImageLayout newLayout);
        VkImage GetVkImage(const RID& rid) { return m_textures.Get(rid)->image; }
        uint32_t GetTextureSize(RID rid);
        VkFormat GetFormat(const RID& textureRID) {return m_textures.Get(textureRID)->format;}
        bool isDepthTexture(const RID& textureRID) {return m_textures.Get(textureRID)->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;}
        VkImageLayout GetTextureLayout(RID textureRID) { return m_textures.Get(textureRID)->layout; }
        void SetTextureLayout(const RID& textureRID, VkImageLayout layout) { m_textures.Get(textureRID)->layout = layout; }

        //不支持多MipmapLevel TODO：完善对多格式和布局的TextureCopy的支持
        bool CopyTexFromBuffer(RID dstTex, RID srcBuffer); //先只支持全量copy
        bool CopyTexToBuffer(RID srcTex, RID dstBuffer);
        bool CopyTex(RID srcTex, RID dstTex);


        RID CreateTextureView(const RID& textureRID, const ViewRange& viewRange); //format 取自纹理, 销毁走 DeleteResource
        void DestroyImageView(RID rid);
        VkImageView GetVkImageView(RID rid) { return m_textureViews.Get(rid)->imageView; }

        //Swapchain
        std::vector<RID> RegisterSwapchainImages(const std::vector<VkImage>& images);
        void UnRegisterSwapchainImages(const std::vector<RID>& imageRIDs);
        void RecreateSwapchain();
        VkExtent2D GetSwapchainExtent() const;
        //轮询 SDL 事件队列: 收到 SDL_EVENT_QUIT 或窗口关闭事件时返回 true; 其余事件交给 onEvent 处理(返回 true 同样退出)
        bool PollEvents(const std::function<bool(SDL_Event&)>& onEvent = {});
        //检查窗口最小化与尺寸变化: 最小化时休眠并返回 false; 尺寸变化时重建交换链并将 *outResized 置为 true
        bool SyncSwapchain(bool* outResized = nullptr);

        //Command
        CommandContext::CommandBuffer& GetCommandBuffer(CommandQueueType queueType, uint32_t frameIndex = 0);
        void StartRecording(CommandContext::CommandBuffer& commandBuffer);
        void EndRecording(CommandContext::CommandBuffer& commandBuffer);
        //返回本次渲染的 renderArea 尺寸, 供 Viewport / Scissor 使用
        VkExtent2D BeginRendering(VkCommandBuffer commandBuffer, const RenderDesc& renderingInfo);
        void EndRendering(VkCommandBuffer commandBuffer);
        void SubmitCommandBuffer(CommandQueueType queueType, const std::vector<CommandContext::CommandBuffer>& commandBuffers, uint64_t signalValue = 0,
            const std::vector<RID>& waitSemaphores = {}, const std::vector<RID>& signalSemaphores = {}, const std::vector<VkPipelineStageFlags2> pipelineStageFlags = {});
        //waitSemaphores 不给默认值, 否则省略信号量的调用会和RID 版本产生重载歧义
        void SubmitCommandBuffer(CommandQueueType queueType, const std::vector<CommandContext::CommandBuffer>& commandBuffers, uint64_t signalValue,
            const std::vector<VkSemaphore>& waitSemaphores, const std::vector<VkSemaphore>& signalSemaphores = {}, const std::vector<VkPipelineStageFlags2> pipelineStageFlags = {});
        //一帧的顺序: PrepareFrame -> 录制 -> BlitToSwapchain -> SubmitFrame -> PresentFrame
        bool PrepareFrame(); //Acquire 交换链图像, 返回false 表示交换链已重建或窗口最小化, 本帧需要跳过. 帧槽位的复用由上一帧末尾的Tick 保证
        //这里使用Copy的方案，可以避免我为Swapchain单独绑定一一个ImageView，并且集中在Context中处理Swapchain的Image， 减少在RenderGraph层计算依赖时的特判, 代替用户GetSwapchain Image再操作
        void BlitToSwapchain(VkCommandBuffer commandBuffer, RID srcTexture); //src 需要TransferSrc usage, 交换链图像最终转到PRESENT_SRC
        //这个Submit 不使用Tracked ，因为Tracked中会直接等待整个Queue中的任务完成才会开始录制，这会导致飞行帧直接失效，并且在Tick中的类似Fence操作已经保证了飞行帧安全
        void SubmitFrame(CommandContext::CommandBuffer& commandBuffer); //等待ImageReady, 完成后Signal PresentReady
        void PresentFrame();

        template<typename Fn>
        requires std::invocable<Fn, CommandContext::CommandBuffer&>
        void ExecuteCommand(Fn&& fn, CommandQueueType queueType) {
            CommandContext::CommandBuffer& commandBuffer = GetCommandBuffer(queueType, 0);
            StartRecording(commandBuffer);
            fn(commandBuffer);
            EndRecording(commandBuffer);
            SubmitCommandBuffer(queueType, {commandBuffer});
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
        uint32_t m_currentImageIndex = 0;

        bool bVsync = false;
        bool bUseSwapChain = false;
        bool m_swapchainResized = false;
        int m_swapchainWindowWidth = 0; //交换链最近一次创建时的窗口像素尺寸, 和当前窗口不一致就重建
        int m_swapchainWindowHeight = 0;

        //Imgui
        bool bUseImgui = false;


        //其实这里需要使用unique比较合适，然后前向声明
        std::unique_ptr<Core> m_core;
        std::unique_ptr<CommandContext> m_commandContext;
        std::unique_ptr<DescriptorSlot> m_descriptorSlot;
        std::unique_ptr<Swapchain> m_swapchain;


        TRIDArray<VulkanBuffer> m_buffers;
        TRIDArray<VkSemaphore> m_semaphores;
        TRIDArray<TextureView> m_textureViews; //轻量包装托管到RID中 ，后续再DescriptorSlot中使用
        TRIDArray<ComputePipeline> m_computePipelines;
        TRIDArray<DescriptorSlotBinding> m_descriptorSlots;
        TRIDArray<VulkanTexture> m_textures;
        TRIDArray<GraphicsShaderObject> m_graphicsShaderObjects;

        std::mutex m_deletionMutex; //保护m_bufferDeletionQueue 和m_currentFrameIndex 的切换, Ref 可能在任意线程析构
        std::vector<std::vector<RID>> m_bufferDeletionQueue; //延迟释放队列
        std::vector<std::vector<uint64_t>> m_deletionWaitValues; //[帧][队列家族] 释放该帧队列前要等到的timeline 值

        std::mutex m_commandBufferSubmitMutex;
        std::unordered_map<VkCommandBuffer, uint64_t> m_commandBufferSubmitValues; //命令缓冲最近一次提交的timeline 值, 重新录制前要等它完成

        // 按队列家族索引。每个 TimelineSemaphore 自己保护目标值。
        std::vector<std::unique_ptr<TimelineSemaphore>> m_timelines;

        //Raster
        //内部管理两个信号量,这个信号量处理需要
        std::vector<VkSemaphore> m_imageReadySemaphores;
        std::vector<VkSemaphore> m_presentReadySemaphores;




        void DestroyResource(RID rid);
        uint64_t SubmitCommandBufferTracked(CommandQueueType queueType, bool waitQueue,const std::vector<CommandContext::CommandBuffer>& commandBuffers,
            uint64_t signalValue, const std::vector<VkSemaphore>& waitSemaphores, const std::vector<VkSemaphore>& signalSemaphores, const std::vector<VkPipelineStageFlags2>& pipelineStageFlags);
        void ReleaseUploadStaging(VulkanBuffer& buffer);
        TimelineSemaphore* GetTimeline(uint32_t queueFamilyIndex);
        bool CheckAndNormalizeRenderDesc(const RenderDesc& renderDesc, uint32_t& width, uint32_t& height);

        //这里是直接阻塞等待
        void WaitResource_Impl(VulkanResource* resource); //首先其是资源

        VkRenderingAttachmentInfo GetAttachmentInfo(const AttachmentInfo& attachmentInfo);

        //使用 InitImgui
        void InitImgui();
        void DestroyImgui();
    };
}
