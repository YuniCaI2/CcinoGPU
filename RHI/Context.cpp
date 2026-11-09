#include "RHI/Context.h"
#include "RHI/ComputePipeline.h"
#include "RHI/Core.h"
#include "RHI/DescriptorSlot.h"
#include "RHI/Swapchain.h"
#include "RHI/TimelineSemaphore.h"
#include "ThirdPart/SDL/src/video/khronos/vulkan/vulkan_core.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace Ccino::RHI {
    namespace {
        VkImageAspectFlags ImageAspect(VkFormat format) {
            switch (format) {
            case VK_FORMAT_D16_UNORM:
            case VK_FORMAT_X8_D24_UNORM_PACK32:
            case VK_FORMAT_D32_SFLOAT:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            case VK_FORMAT_S8_UINT:
                return VK_IMAGE_ASPECT_STENCIL_BIT;
            case VK_FORMAT_D16_UNORM_S8_UINT:
            case VK_FORMAT_D24_UNORM_S8_UINT:
            case VK_FORMAT_D32_SFLOAT_S8_UINT:
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            default:
                return VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }

        //紧密打包下单个 texel 的字节数；压缩 / combined depth-stencil 等格式暂不支持，返回 0
        uint32_t BytesPerTexel(VkFormat format) {
            switch (format) {
            case VK_FORMAT_R8_UNORM:
            case VK_FORMAT_R8_SNORM:
            case VK_FORMAT_R8_USCALED:
            case VK_FORMAT_R8_SSCALED:
            case VK_FORMAT_R8_UINT:
            case VK_FORMAT_R8_SINT:
            case VK_FORMAT_R8_SRGB:
            case VK_FORMAT_S8_UINT:
                return 1;
            case VK_FORMAT_R8G8_UNORM:
            case VK_FORMAT_R8G8_SNORM:
            case VK_FORMAT_R8G8_USCALED:
            case VK_FORMAT_R8G8_SSCALED:
            case VK_FORMAT_R8G8_UINT:
            case VK_FORMAT_R8G8_SINT:
            case VK_FORMAT_R8G8_SRGB:
            case VK_FORMAT_R16_UNORM:
            case VK_FORMAT_R16_SNORM:
            case VK_FORMAT_R16_USCALED:
            case VK_FORMAT_R16_SSCALED:
            case VK_FORMAT_R16_UINT:
            case VK_FORMAT_R16_SINT:
            case VK_FORMAT_R16_SFLOAT:
            case VK_FORMAT_D16_UNORM:
                return 2;
            case VK_FORMAT_R8G8B8_UNORM:
            case VK_FORMAT_R8G8B8_SNORM:
            case VK_FORMAT_R8G8B8_USCALED:
            case VK_FORMAT_R8G8B8_SSCALED:
            case VK_FORMAT_R8G8B8_UINT:
            case VK_FORMAT_R8G8B8_SINT:
            case VK_FORMAT_R8G8B8_SRGB:
            case VK_FORMAT_B8G8R8_UNORM:
            case VK_FORMAT_B8G8R8_SNORM:
            case VK_FORMAT_B8G8R8_USCALED:
            case VK_FORMAT_B8G8R8_SSCALED:
            case VK_FORMAT_B8G8R8_UINT:
            case VK_FORMAT_B8G8R8_SINT:
            case VK_FORMAT_B8G8R8_SRGB:
                return 3;
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_SNORM:
            case VK_FORMAT_R8G8B8A8_USCALED:
            case VK_FORMAT_R8G8B8A8_SSCALED:
            case VK_FORMAT_R8G8B8A8_UINT:
            case VK_FORMAT_R8G8B8A8_SINT:
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_SNORM:
            case VK_FORMAT_B8G8R8A8_USCALED:
            case VK_FORMAT_B8G8R8A8_SSCALED:
            case VK_FORMAT_B8G8R8A8_UINT:
            case VK_FORMAT_B8G8R8A8_SINT:
            case VK_FORMAT_B8G8R8A8_SRGB:
            case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
            case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
            case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
            case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
            case VK_FORMAT_A8B8G8R8_UINT_PACK32:
            case VK_FORMAT_A8B8G8R8_SINT_PACK32:
            case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
            case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
            case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
            case VK_FORMAT_A2R10G10B10_UINT_PACK32:
            case VK_FORMAT_A2R10G10B10_SINT_PACK32:
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
            case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
            case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
            case VK_FORMAT_A2B10G10R10_UINT_PACK32:
            case VK_FORMAT_A2B10G10R10_SINT_PACK32:
            case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
            case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
            case VK_FORMAT_R16G16_UNORM:
            case VK_FORMAT_R16G16_SNORM:
            case VK_FORMAT_R16G16_USCALED:
            case VK_FORMAT_R16G16_SSCALED:
            case VK_FORMAT_R16G16_UINT:
            case VK_FORMAT_R16G16_SINT:
            case VK_FORMAT_R16G16_SFLOAT:
            case VK_FORMAT_R32_UINT:
            case VK_FORMAT_R32_SINT:
            case VK_FORMAT_R32_SFLOAT:
            case VK_FORMAT_D32_SFLOAT:
            case VK_FORMAT_X8_D24_UNORM_PACK32:
            case VK_FORMAT_D24_UNORM_S8_UINT:
                return 4;
            case VK_FORMAT_R16G16B16_UNORM:
            case VK_FORMAT_R16G16B16_SNORM:
            case VK_FORMAT_R16G16B16_USCALED:
            case VK_FORMAT_R16G16B16_SSCALED:
            case VK_FORMAT_R16G16B16_UINT:
            case VK_FORMAT_R16G16B16_SINT:
            case VK_FORMAT_R16G16B16_SFLOAT:
                return 6;
            case VK_FORMAT_R16G16B16A16_UNORM:
            case VK_FORMAT_R16G16B16A16_SNORM:
            case VK_FORMAT_R16G16B16A16_USCALED:
            case VK_FORMAT_R16G16B16A16_SSCALED:
            case VK_FORMAT_R16G16B16A16_UINT:
            case VK_FORMAT_R16G16B16A16_SINT:
            case VK_FORMAT_R16G16B16A16_SFLOAT:
            case VK_FORMAT_R32G32_UINT:
            case VK_FORMAT_R32G32_SINT:
            case VK_FORMAT_R32G32_SFLOAT:
            case VK_FORMAT_R64_UINT:
            case VK_FORMAT_R64_SINT:
            case VK_FORMAT_R64_SFLOAT:
                return 8;
            case VK_FORMAT_R32G32B32_UINT:
            case VK_FORMAT_R32G32B32_SINT:
            case VK_FORMAT_R32G32B32_SFLOAT:
                return 12;
            case VK_FORMAT_R32G32B32A32_UINT:
            case VK_FORMAT_R32G32B32A32_SINT:
            case VK_FORMAT_R32G32B32A32_SFLOAT:
            case VK_FORMAT_R64G64_UINT:
            case VK_FORMAT_R64G64_SINT:
            case VK_FORMAT_R64G64_SFLOAT:
                return 16;
            default:
                return 0;
            }
        }

        void LayoutSync(VkImageLayout layout, VkPipelineStageFlags2& stage, VkAccessFlags2& access) {
            switch (layout) {
            case VK_IMAGE_LAYOUT_UNDEFINED:
            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                access = VK_ACCESS_2_NONE;
                break;
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
                stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
                stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                stage = VK_PIPELINE_STAGE_2_COPY_BIT;
                access = VK_ACCESS_2_TRANSFER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                stage = VK_PIPELINE_STAGE_2_COPY_BIT;
                access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                access = VK_ACCESS_2_NONE;
                break;
            default:
                stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                break;
            }
        }

        VkImageMemoryBarrier2 MakeLayoutBarrier(VkImage image, VkFormat format, VkImageLayout oldLayout,
            VkImageLayout newLayout, uint32_t mipLevels, uint32_t arrayLayers) {
            VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE;
            VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
            VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_NONE;
            VkAccessFlags2 dstAccess = VK_ACCESS_2_NONE;
            LayoutSync(oldLayout, srcStage, srcAccess);
            LayoutSync(newLayout, dstStage, dstAccess);

            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = srcStage;
            barrier.srcAccessMask = srcAccess;
            barrier.dstStageMask = dstStage;
            barrier.dstAccessMask = dstAccess;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = ImageAspect(format);
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = mipLevels == 0 ? VK_REMAINING_MIP_LEVELS : mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = arrayLayers == 0 ? VK_REMAINING_ARRAY_LAYERS : arrayLayers;
            return barrier;
        }
    }

    Context::Context()
        : m_core(std::make_unique<Core>())
        , m_commandContext(std::make_unique<CommandContext>())
        , m_descriptorSlot(std::make_unique<DescriptorSlot>()) {}

    Context::~Context() = default;

    void Context::Init(const RHISettings& settings) {
        m_core->Init(settings);
        m_commandContext->Init(m_core->GetVkDevice(), {
            m_core->GetGraphicsQueueFamilyIndex(),
            m_core->GetComputeQueueFamilyIndex(),
            m_core->GetTransferQueueFamilyIndex()
        });
        CreateGlobalTimelineSemaphore();
        DescriptorSlot::InitGlobal(*m_core); //全局唯一的DescriptorSetLayout / PipelineLayout / DescriptorPool
        m_flightFrameCount = settings.flightFrameCount; //飞行帧数用来处理资源和同步的分配

        m_bufferDeletionQueue.resize(m_flightFrameCount); //初始化队列
        m_descriptorSlot->Init(this);
        m_swapchain->Init(this);
        m_swapchain->Create(settings.windowWidth, settings.windowHeight, settings.bVsync);

        m_settings = settings;

        //Global Semaphore
        CreateGlobalBinarySemaphores();
        //FrameNums
        m_frameFenceNums.resize(m_flightFrameCount, 0);
    }

    void Context::Tick() {
        std::vector<RID> expired;
        {
            std::lock_guard lock(m_deletionMutex);
            m_currentFrameIndex = (m_currentFrameIndex + 1) % m_flightFrameCount;
            expired.swap(m_bufferDeletionQueue[m_currentFrameIndex]); //取出后在锁外销毁, 避免销毁过程中持锁
        }
        for (RID rid : expired) {
            DestroyResource(rid);
        }
    }

    void Context::Destroy() {
        //Global Semaphore Destroy
        DestroyGlobalBinarySemaphores();

        VkDevice device = m_core->GetVkDevice();
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            m_computePipelines.ForEach([&](ComputePipeline& pipeline) {
                pipeline.Destroy();
            });
            m_graphicsShaderObjects.ForEach([&](GraphicsShaderObject& shaderObject) {
                shaderObject.Destroy();
            });
            m_descriptorSlots.ForEach([&](DescriptorSlotBinding& slot) {
                slot.Release();
            });
            m_descriptorSlot->Destroy();
        }

        m_buffers.ForEach([&](VulkanBuffer& buffer) {
            if (buffer.mappedPtr != nullptr && !buffer.persistentlyMapped) {
                vmaUnmapMemory(m_core->GetVmaAllocator(), buffer.allocation);
                buffer.mappedPtr = nullptr;
            }
            if (buffer.buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(m_core->GetVmaAllocator(), buffer.buffer, buffer.allocation);
                buffer.buffer = VK_NULL_HANDLE;
                buffer.allocation = VK_NULL_HANDLE;
            }
        });

        //View 引用着Image, 必须在Image 之前销毁
        if (device != VK_NULL_HANDLE) {
            m_textureViews.ForEach([&](TextureView& view) {
                if (view.imageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, view.imageView, nullptr);
                    view.imageView = VK_NULL_HANDLE;
                }
            });
        }

        m_textures.ForEach([&](VulkanTexture& texture) {
            if (texture.isSwapchain || texture.image == VK_NULL_HANDLE) {
                return;
            }
            vmaDestroyImage(m_core->GetVmaAllocator(), texture.image, texture.allocation);
            texture.image = VK_NULL_HANDLE;
            texture.allocation = VK_NULL_HANDLE;
        });

        if (device != VK_NULL_HANDLE) {
            m_semaphores.ForEach([&](VkSemaphore& semaphore) {
                if (semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, semaphore, nullptr);
                    semaphore = VK_NULL_HANDLE;
                }
            });
        }

        m_bufferDeletionQueue.clear();
        DescriptorSlot::DestroyGlobal();
        DestroyGlobalTimelineSemaphore();
        m_commandContext->Destroy();
        m_core->Destroy();
    }

    void Context::DeleteResource(RID rid) {
        if (!rid) {
            return;
        }
        //类型匹配
        if (m_flightFrameCount <= 1)  {
            for (const auto& timeline : m_timelines) {
                if (timeline != nullptr) {
                    timeline->Wait(timeline->ReadValue()); //这里的ReadValue是timeline的目标值，可以理解是最后一次提交时Command记录的值
                }
            }
            DestroyResource(rid);
        } else {
            std::lock_guard lock(m_deletionMutex);
            m_bufferDeletionQueue[m_currentFrameIndex].push_back(rid);
        }
    }

    RID Context::CreateBuffer(const BufferCreateInfo& createInfo) {
        const VkBufferUsageFlags2 usage = createInfo.usage | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;
        const bool hostVisible = createInfo.access != HostAccess::None;
        const bool hostRead = HasHostAccess(createInfo.access, HostAccess::Read);

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = createInfo.size;
        bufferCreateInfo.usage = 0;
        bufferCreateInfo.flags = 0;

        VkBufferUsageFlags2CreateInfo bufferUsageCreateInfo{};
        bufferUsageCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO;
        bufferUsageCreateInfo.usage = usage;
        bufferCreateInfo.pNext = &bufferUsageCreateInfo;

        uint32_t queueFamilies[] = {
            m_core->GetGraphicsQueueFamilyIndex(),
            m_core->GetComputeQueueFamilyIndex(),
            m_core->GetTransferQueueFamilyIndex()
        };
        std::vector<uint32_t> uniqueQueueFamilies;
        for (uint32_t family : queueFamilies) {
            if (std::find(uniqueQueueFamilies.begin(), uniqueQueueFamilies.end(), family) == uniqueQueueFamilies.end()) {
                uniqueQueueFamilies.push_back(family);
            }
        }
        if (uniqueQueueFamilies.size() > 1) {
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            bufferCreateInfo.queueFamilyIndexCount = static_cast<uint32_t>(uniqueQueueFamilies.size());
            bufferCreateInfo.pQueueFamilyIndices = uniqueQueueFamilies.data();
        } else {
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        VmaAllocationCreateInfo allocCreateInfo{};
        if (hostVisible) {
            // Staging / Uniform 由 CPU 写入。Device-local 的初始数据走 Transfer 队列。
            allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                (hostRead ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                          : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            if (createInfo.requireHostCoherent) {
                //用 requiredFlags 才是保证：没有满足的内存类型时 vmaCreateBuffer 直接失败
                allocCreateInfo.requiredFlags |=
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            }
        } else {
            allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }

        VulkanBuffer buffer{};
        VmaAllocationInfo allocInfo{};
        VULKAN_CHECK(vmaCreateBuffer(
            m_core->GetVmaAllocator(),
            &bufferCreateInfo,
            &allocCreateInfo,
            &buffer.buffer,
            &buffer.allocation,
            &allocInfo));

        buffer.usage = usage;
        buffer.size = createInfo.size;
        buffer.hostVisible = hostVisible;
        if ((allocCreateInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) != 0 && allocInfo.pMappedData != nullptr) {
            buffer.mappedPtr = allocInfo.pMappedData;
            buffer.persistentlyMapped = true;
        }
        //不要求 coherent 时也可能分到 coherent 内存，按实际内存类型记录
        VkMemoryPropertyFlags memoryProperties = 0;
        vmaGetAllocationMemoryProperties(m_core->GetVmaAllocator(), buffer.allocation, &memoryProperties);
        buffer.hostCoherent = (memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer.buffer;
        buffer.bda = vkGetBufferDeviceAddress(m_core->GetVkDevice(), &addressInfo);

        return m_buffers.Add(std::move(buffer));
    }

    bool Context::GetBufferUsage(RID rid, VkBufferUsageFlags2& usage) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr) {
            return false;
        }
        usage = buffer->usage;
        return true;
    }

    bool Context::GetImageDescriptorType(RID, VkDescriptorType&) {
        return false;
    }

    bool Context::GetSamplerDescriptorType(RID, VkDescriptorType&) {
        return false;
    }

    RID Context::CreateUniformBuffer(uint32_t size, const void* data) {
        BufferCreateInfo createInfo{};
        createInfo.size = size;
        createInfo.usage = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT;
        createInfo.access = HostAccess::Write;
        createInfo.requireHostCoherent = true; //Uniform 走持久映射裸写，必须保证主机一致，免去每次写后 Flush
        RID rid = CreateBuffer(createInfo);
        if (data == nullptr) {
            MapBuffer(rid);
        } else {
            void* ptr = MapBuffer(rid);
            if (ptr == nullptr) {
                throw std::runtime_error("Failed to map uniform buffer");
            }
            std::memcpy(ptr, data, size);
            UnmapBuffer(rid);
        }
        return rid;
    }

    RID Context::CreateTransferBuffer(uint32_t size, const void* data) {
        RID rid = CreateBuffer(BufferCreateInfo{size, VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT, HostAccess::Write});
        if (data != nullptr && size > 0) {
            void* mapped = MapBuffer(rid);
            if (mapped == nullptr) {
                throw std::runtime_error("Failed to map transfer buffer");
            }
            std::memcpy(mapped, data, size);
            UnmapBuffer(rid);
        }
        return rid;
    }

    RID Context::CreateStorageBuffer(uint32_t size, const void* data) {
        RID rid = CreateBuffer(BufferCreateInfo{
            size,
            VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT
        });
        if (data != nullptr && size > 0) {
            UploadBuffer(rid, data, size);
        }
        return rid;
    }

    RID Context::CreateReadbackBuffer(uint32_t size) {
        BufferCreateInfo createInfo;
        createInfo.size = size;
        createInfo.usage = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
        createInfo.access = HostAccess::Read;
        return CreateBuffer(createInfo);
    }

    void Context::ReadbackBuffer(RID src, RID dst) {
        VulkanBuffer* source = m_buffers.Get(src);
        VulkanBuffer* destination = m_buffers.Get(dst);
        if (source == nullptr || destination == nullptr) {
            throw std::runtime_error("Readback buffer is invalid");
        }
        if (destination->size < source->size) {
            throw std::runtime_error("Readback buffer is smaller than the source");
        }

        const VkBuffer srcBuffer = source->buffer;
        const VkBuffer dstBuffer = destination->buffer;
        const uint32_t size = source->size;

        WaitResource_Impl(source);
        WaitResource_Impl(destination);



        CommandContext::CommandBuffer& commandBuffer = GetCommandBuffer(CommandQueueType::TRANSFER);
        StartRecording(commandBuffer);
        VkBufferCopy region{};
        region.size = size;
        vkCmdCopyBuffer(commandBuffer.commandBuffer, srcBuffer, dstBuffer, 1, &region);

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = dstBuffer;
        barrier.offset = 0;
        barrier.size = size;

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer.commandBuffer, &dependency);
        EndRecording(commandBuffer);

        const uint64_t value = SubmitCommandBufferTracked(CommandQueueType::TRANSFER, {commandBuffer}, 0, {}, {}, {});
        destination = m_buffers.Get(dst);
        if (destination == nullptr) {
            throw std::runtime_error("Readback buffer was destroyed");
        }
        destination->lastQueue = CommandQueueType::TRANSFER;
        destination->lastTimelineValue = value;
    }

    void Context::UploadBuffer(RID rid, const void* data, uint32_t size) {
        if (data == nullptr || size == 0) {
            return;
        }
        if (m_buffers.Get(rid) == nullptr) {
            return;
        }

        CommandContext::CommandBuffer& commandBuffer = GetCommandBuffer(CommandQueueType::TRANSFER);
        StartRecording(commandBuffer);

        VulkanBuffer* destination = m_buffers.Get(rid);
        if (destination == nullptr) {
            throw std::runtime_error("Upload destination was destroyed");
        }
        ReleaseUploadStaging(*destination);

        RID stagingRid = CreateTransferBuffer(size, data);
        VulkanBuffer* staging = m_buffers.Get(stagingRid);
        destination = m_buffers.Get(rid);
        if (staging == nullptr || destination == nullptr) {
            DestroyBuffer(stagingRid);
            throw std::runtime_error("Failed to create staging buffer for upload");
        }

        const VkBuffer src = staging->buffer;
        const VkBuffer dst = destination->buffer;
        bool submitted = false;
        try {
            VkBufferCopy region{};
            region.size = size;
            vkCmdCopyBuffer(commandBuffer.commandBuffer, src, dst, 1, &region);
            EndRecording(commandBuffer);

            const uint64_t value = SubmitCommandBufferTracked(CommandQueueType::TRANSFER, {commandBuffer}, 0, {}, {}, {});
            submitted = true;

            destination = m_buffers.Get(rid);
            if (destination == nullptr) {
                throw std::runtime_error("Upload destination was destroyed");
            }
            destination->lastQueue = CommandQueueType::TRANSFER;
            destination->lastTimelineValue = value;
            destination->uploadStaging = stagingRid;
        } catch (...) {
            if (!submitted) {
                DestroyBuffer(stagingRid);
            }
            throw;
        }
    }

    bool Context::IsBufferAvailable(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr) {
            return false;
        }
        if (buffer->lastTimelineValue == 0) {
            return true;
        }

        const bool available = IsGlobalTimelineSemaphoreAvailable(buffer->lastQueue, buffer->lastTimelineValue);
        if (available) {
            ReleaseUploadStaging(*buffer);
        }
        return available;
    }

    void* Context::MapBuffer(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr || !buffer->hostVisible) {
            std::cerr << "Buffer is not a uniform buffer or transfer source buffer" << std::endl;
            return nullptr;
        }
        if (buffer->mappedPtr == nullptr) {
            void* mapped = nullptr;
            VULKAN_CHECK(vmaMapMemory(m_core->GetVmaAllocator(), buffer->allocation, &mapped));
            buffer->mappedPtr = mapped;
        }
        if (!buffer->hostCoherent) {
            VULKAN_CHECK(vmaInvalidateAllocation(m_core->GetVmaAllocator(), buffer->allocation, 0, buffer->size));
        }
        return buffer->mappedPtr;
    }

    void Context::UnmapBuffer(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr || !buffer->hostVisible) {
            std::cerr << "Buffer is not a uniform buffer or transfer source buffer" << std::endl;
            return;
        }

        if (!buffer->hostCoherent) {
            VULKAN_CHECK(vmaFlushAllocation(m_core->GetVmaAllocator(), buffer->allocation, 0, buffer->size));
        }
        if (!buffer->persistentlyMapped && buffer->mappedPtr != nullptr) {
            vmaUnmapMemory(m_core->GetVmaAllocator(), buffer->allocation);
            buffer->mappedPtr = nullptr;
        }
    }

    void Context::WaitForUploadStaging(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr || !buffer->uploadStaging) {
            return;
        }

        if (TimelineSemaphore* transferTimeline = GetTimeline(m_core->GetQueueFamilyIndex(CommandQueueType::TRANSFER));
            transferTimeline != nullptr) {
            if (buffer->lastQueue != CommandQueueType::TRANSFER || buffer->lastTimelineValue == 0 ||
                buffer->lastTimelineValue < transferTimeline->ReadValue()) {
                return;
            }
            transferTimeline->Wait(buffer->lastTimelineValue);
        } else {
            throw std::runtime_error("Transfer timeline is not available");
        }
    }

    void Context::ReleaseUploadStaging(VulkanBuffer& buffer) {
        RID staging = buffer.uploadStaging;
        buffer.uploadStaging = RID::Invalid();
        if (staging) {
            DestroyBuffer(staging);
        }
    }

    void Context::DestroyBuffer(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr) {
            return;
        }

        if (buffer->lastTimelineValue != 0) {
            if (TimelineSemaphore* timeline = GetTimeline(m_core->GetQueueFamilyIndex(buffer->lastQueue))) {
                timeline->Wait(buffer->lastTimelineValue);
            }
        }
        ReleaseUploadStaging(*buffer);

        if (buffer->mappedPtr != nullptr && !buffer->persistentlyMapped) {
            vmaUnmapMemory(m_core->GetVmaAllocator(), buffer->allocation);
            buffer->mappedPtr = nullptr;
        }
        if (buffer->buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_core->GetVmaAllocator(), buffer->buffer, buffer->allocation);
            buffer->buffer = VK_NULL_HANDLE;
            buffer->allocation = VK_NULL_HANDLE;
        }
        m_buffers.Remove(rid);
    }

    RID Context::CreateSemaphore() {
        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VULKAN_CHECK(vkCreateSemaphore(m_core->GetVkDevice(), &createInfo, nullptr, &semaphore));
        return m_semaphores.Add(std::move(semaphore));
    }

    void Context::DestroySemaphore(RID rid) {
        VkSemaphore* semaphore = m_semaphores.Get(rid);
        if (semaphore == nullptr || *semaphore == VK_NULL_HANDLE) {
            return;
        }
        vkDestroySemaphore(m_core->GetVkDevice(), *semaphore, nullptr);
        *semaphore = VK_NULL_HANDLE;
        m_semaphores.Remove(rid);
    }

    void Context::CreateGlobalBinarySemaphores() {
        int n = m_swapchain->m_images.size();
        m_imageReadySemaphores.resize(n);
        m_presentCompletedSemaphores.resize(n);

        auto Create = [this](VkSemaphore& semaphore) {
            VkSemaphoreCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            VULKAN_CHECK(vkCreateSemaphore(m_core->GetVkDevice(), &createInfo, nullptr, &semaphore));
        };

        for (int i = 0; i < n; i++) {
            Create(m_imageReadySemaphores[i]);
            Create(m_presentCompletedSemaphores[i]);
        }

        return;
    }
    void Context::DestroyGlobalBinarySemaphores() {
        int n = m_imageReadySemaphores.size();
        for (int i = 0; i < n; i++) {
            vkDestroySemaphore(m_core->GetVkDevice(), m_imageReadySemaphores[i], nullptr);
            vkDestroySemaphore(m_core->GetVkDevice(), m_presentCompletedSemaphores[i], nullptr);
        }
    }

    void Context::CreateGlobalTimelineSemaphore() {
        DestroyGlobalTimelineSemaphore();

        const uint32_t familyCount = m_core->GetQueueFamilyCount();
        m_timelines.resize(familyCount);

        auto createForFamily = [&](uint32_t family) {
            if (family >= familyCount) {
                return;
            }
            if (m_timelines[family] == nullptr) {
                m_timelines[family] = std::make_unique<TimelineSemaphore>();
            }
            if (m_timelines[family]->GetSemaphore() != VK_NULL_HANDLE) {
                return;
            }
            m_timelines[family]->Create(m_core->GetVkDevice());
        };

        createForFamily(m_core->GetGraphicsQueueFamilyIndex());
        createForFamily(m_core->GetComputeQueueFamilyIndex());
        createForFamily(m_core->GetTransferQueueFamilyIndex());
    }

    void Context::DestroyGlobalTimelineSemaphore() {
        for (std::unique_ptr<TimelineSemaphore>& timeline : m_timelines) {
            if (timeline != nullptr) {
                timeline->Destroy();
            }
        }
        m_timelines.clear();
    }

    bool Context::IsGlobalTimelineSemaphoreAvailable(CommandQueueType queueType, uint64_t value) {
        TimelineSemaphore* timeline = GetTimeline(m_core->GetQueueFamilyIndex(queueType));
        if (timeline == nullptr) {
            return value == 0;
        }
        return timeline->IsAvailable(value);
    }

    TimelineSemaphore* Context::GetTimeline(uint32_t queueFamilyIndex) {
        if (queueFamilyIndex >= m_timelines.size() || m_timelines[queueFamilyIndex] == nullptr ||
            m_timelines[queueFamilyIndex]->GetSemaphore() == VK_NULL_HANDLE) {
            return nullptr;
        }
        return m_timelines[queueFamilyIndex].get();
    }

    bool Context::CheckAndNormalizeRenderDesc(const RenderDesc& renderDesc, uint32_t& width, uint32_t& height) {
        //所有附件在各自 mip 上的尺寸必须一致
        bool hasExtent = false;
        uint32_t extentW = 0;
        uint32_t extentH = 0;
        auto checkAttachment = [&](const AttachmentInfo& attachment) {
            const TextureView* view = m_textureViews.Get(attachment.textureView);
            const VulkanTexture* texture = view != nullptr ? m_textures.Get(view->imageRID) : nullptr;
            if (texture == nullptr) {
                return false;
            }
            const uint32_t w = std::max(texture->width >> view->range.baseMipMap, 1u);
            const uint32_t h = std::max(texture->height >> view->range.baseMipMap, 1u);
            if (!hasExtent) {
                extentW = w;
                extentH = h;
                hasExtent = true;
                return true;
            }
            return w == extentW && h == extentH;
        };

        for (const AttachmentInfo& color : renderDesc.colorAttachments) {
            if (!checkAttachment(color)) {
                return false;
            }
        }
        if (renderDesc.depthAttachment.textureView && !checkAttachment(renderDesc.depthAttachment)) {
            return false;
        }
        if (!hasExtent || renderDesc.offsetX < 0 || renderDesc.offsetY < 0) {
            return false;
        }

        width = renderDesc.width == 0 ? extentW : renderDesc.width;
        height = renderDesc.height == 0 ? extentH : renderDesc.height;
        return static_cast<uint32_t>(renderDesc.offsetX) + width <= extentW &&
               static_cast<uint32_t>(renderDesc.offsetY) + height <= extentH;
    }

    void Context::WaitResource_Impl(VulkanResource* resource) {
        if (TimelineSemaphore* timeline = GetTimeline(m_core->GetQueueFamilyIndex(resource->lastQueue));
            timeline != nullptr) {
            timeline->Wait(resource->lastTimelineValue);
        }
    }

    VkRenderingAttachmentInfo Context::GetAttachmentInfo(const AttachmentInfo& attachmentInfo) {
        const TextureView* view = m_textureViews.Get(attachmentInfo.textureView);
        if (view == nullptr) {
            throw std::invalid_argument("[RHI Context GetAttachmentInfo]: texture view is invalid");
        }

        VkRenderingAttachmentInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        info.imageView = view->imageView;
        info.imageLayout = GetTextureLayout(view->imageRID);
        info.resolveMode = VK_RESOLVE_MODE_NONE;
        info.loadOp = attachmentInfo.loadOp;
        info.storeOp = attachmentInfo.storeOp;
        info.clearValue = attachmentInfo.clearValue;
        return info;
    }

    RID Context::CreateTexture(const TextureCreateInfo& createInfo) {
        if (createInfo.arrayLayers > 1) {
            throw std::runtime_error("Texture arrays are not supported");
        }
        if (createInfo.width == 0 || createInfo.format == VK_FORMAT_UNDEFINED || createInfo.usage == 0) {
            throw std::runtime_error("Texture create info is invalid");
        }

        VkExtent3D extent{createInfo.width, 1, 1};
        switch (createInfo.type) {
        case VK_IMAGE_TYPE_1D:
            break;
        case VK_IMAGE_TYPE_2D:
            if (createInfo.height == 0) {
                throw std::runtime_error("Texture create info is invalid");
            }
            extent.height = createInfo.height;
            break;
        case VK_IMAGE_TYPE_3D:
            if (createInfo.height == 0 || createInfo.depth == 0) {
                throw std::runtime_error("Texture create info is invalid");
            }
            extent.height = createInfo.height;
            extent.depth = createInfo.depth;
            break;
        default:
            throw std::runtime_error("Invalid texture type");
        }

        VkImageCreateInfo imageCreateInfo{};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = createInfo.type;
        imageCreateInfo.format = createInfo.format;
        imageCreateInfo.extent = extent;
        imageCreateInfo.mipLevels = std::max(createInfo.mipLevels, 1u);
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = createInfo.sampleCount == 0 ? VK_SAMPLE_COUNT_1_BIT : createInfo.sampleCount;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = createInfo.usage;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        uint32_t queueFamilies[] = {
            m_core->GetGraphicsQueueFamilyIndex(),
            m_core->GetComputeQueueFamilyIndex(),
            m_core->GetTransferQueueFamilyIndex()
        };
        std::vector<uint32_t> uniqueQueueFamilies;
        for (uint32_t family : queueFamilies) {
            if (std::find(uniqueQueueFamilies.begin(), uniqueQueueFamilies.end(), family) == uniqueQueueFamilies.end()) {
                uniqueQueueFamilies.push_back(family);
            }
        }
        if (uniqueQueueFamilies.size() > 1) {
            imageCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            imageCreateInfo.queueFamilyIndexCount = static_cast<uint32_t>(uniqueQueueFamilies.size());
            imageCreateInfo.pQueueFamilyIndices = uniqueQueueFamilies.data();
        } else {
            imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VulkanTexture texture{};
        VULKAN_CHECK(vmaCreateImage(
            m_core->GetVmaAllocator(),
            &imageCreateInfo,
            &allocCreateInfo,
            &texture.image,
            &texture.allocation,
            nullptr));

        texture.width = extent.width;
        texture.height = extent.height;
        texture.depth = extent.depth;
        texture.mipLevels = imageCreateInfo.mipLevels;
        texture.arrayLayers = imageCreateInfo.arrayLayers;
        texture.format = createInfo.format;
        texture.type = createInfo.type;
        texture.sampleCount = imageCreateInfo.samples;
        texture.usage = createInfo.usage;
        return m_textures.Add(std::move(texture));
    }

    void Context::DestroyTexture(RID rid) {
        VulkanTexture* texture = m_textures.Get(rid);
        if (texture == nullptr) {
            return;
        }
        //这个地方真要等待吗？
        if (texture->lastTimelineValue != 0) {
            if (TimelineSemaphore* timeline = GetTimeline(m_core->GetQueueFamilyIndex(texture->lastQueue))) {
                timeline->Wait(texture->lastTimelineValue);
            }
        }
        if (!texture->isSwapchain && texture->image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_core->GetVmaAllocator(), texture->image, texture->allocation);
            texture->image = VK_NULL_HANDLE;
            texture->allocation = VK_NULL_HANDLE;
        }
        m_textures.Remove(rid);
    }

    RID Context::CreateTextureView(const RID& textureRID, const ViewRange& viewRange) {
        VulkanTexture* texture = m_textures.Get(textureRID);
        if (texture == nullptr || texture->image == VK_NULL_HANDLE) {
            throw std::runtime_error("CreateTextureView: texture is invalid");
        }
        if (texture->format == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("CreateTextureView: texture has no registered format");
        }
        if (viewRange.mipMapCounts == 0 || viewRange.arrayCounts == 0 ||
            viewRange.baseMipMap + viewRange.mipMapCounts > texture->mipLevels ||
            viewRange.baseArray + viewRange.arrayCounts > texture->arrayLayers) {
            throw std::runtime_error("CreateTextureView: view range is out of the texture's subresources");
        }

        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
        switch (texture->type) {
        case VK_IMAGE_TYPE_1D:
            viewType = viewRange.arrayCounts > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
            break;
        case VK_IMAGE_TYPE_2D:
            viewType = viewRange.arrayCounts > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            break;
        case VK_IMAGE_TYPE_3D:
            viewType = VK_IMAGE_VIEW_TYPE_3D;
            break;
        default:
            throw std::runtime_error("CreateTextureView: unsupported image type");
        }

        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = texture->image;
        createInfo.viewType = viewType;
        createInfo.format = texture->format;
        createInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        createInfo.subresourceRange.aspectMask = ToVkAspectFlags(viewRange.aspectMask);
        createInfo.subresourceRange.baseMipLevel = viewRange.baseMipMap;
        createInfo.subresourceRange.levelCount = viewRange.mipMapCounts;
        createInfo.subresourceRange.baseArrayLayer = viewRange.baseArray;
        createInfo.subresourceRange.layerCount = viewRange.arrayCounts;

        VkImageView imageView = VK_NULL_HANDLE;
        VULKAN_CHECK(vkCreateImageView(m_core->GetVkDevice(), &createInfo, nullptr, &imageView));
        return m_textureViews.Add(TextureView{imageView, textureRID, viewRange});
    }

    void Context::DestroyImageView(RID rid) {
        TextureView* view = m_textureViews.Get(rid);
        if (view == nullptr) {
            return;
        }
        if (view->imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_core->GetVkDevice(), view->imageView, nullptr);
            view->imageView = VK_NULL_HANDLE;
        }
        m_textureViews.Remove(rid);
    }

    void Context::TransitionTextureLayout(RID rid, VkImageLayout newLayout) {
        VulkanTexture* texture = m_textures.Get(rid);
        if (texture == nullptr || texture->image == VK_NULL_HANDLE) {
            throw std::runtime_error("Texture is invalid");
        }
        const VkImageLayout oldLayout = texture->layout;
        if (oldLayout == newLayout) {
            return;
        }

        const uint32_t lastFamily = m_core->GetQueueFamilyIndex(texture->lastQueue);
        const uint32_t graphicsFamily = m_core->GetQueueFamilyIndex(CommandQueueType::GRAPHICS);
        if (texture->lastTimelineValue != 0 && lastFamily != graphicsFamily) {
            if (TimelineSemaphore* timeline = GetTimeline(lastFamily)) {
                timeline->Wait(texture->lastTimelineValue);
            }
        }

        texture = m_textures.Get(rid);
        if (texture == nullptr || texture->image == VK_NULL_HANDLE) {
            throw std::runtime_error("Texture was destroyed");
        }

        CommandContext::CommandBuffer& commandBuffer = GetCommandBuffer(CommandQueueType::GRAPHICS);
        auto barrier = MakeLayoutBarrier(texture->image, texture->format, texture->layout, newLayout,
            texture->mipLevels == 0 ? VK_REMAINING_MIP_LEVELS : texture->mipLevels,
            texture->arrayLayers == 0 ? VK_REMAINING_ARRAY_LAYERS : texture->arrayLayers);
        StartRecording(commandBuffer);


        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer.commandBuffer, &dependency);
        EndRecording(commandBuffer);
        const uint64_t signalValue = SubmitCommandBufferTracked(CommandQueueType::GRAPHICS, {commandBuffer}, 0, {}, {}, {});

        texture = m_textures.Get(rid);
        if (texture == nullptr) {
            throw std::runtime_error("Texture was destroyed");
        }
        texture->layout = newLayout;
        texture->lastQueue = CommandQueueType::GRAPHICS;
        texture->lastTimelineValue = signalValue;
    }

    uint32_t Context::GetTextureSize(RID rid) {
        const VulkanTexture* texture = m_textures.Get(rid);
        if (texture == nullptr || texture->width == 0 || texture->height == 0 || texture->depth == 0) {
            return 0;
        }
        const uint32_t bytesPerTexel = BytesPerTexel(texture->format);
        if (bytesPerTexel == 0) {
            return 0;
        }

        //紧密打包（bufferRowLength = 0）下整张纹理的字节数：所有 mip、所有 array layer 累加
        //交换链图像没有登记尺寸 / format，会走上面的分支返回 0
        const uint32_t mipLevels = std::max(texture->mipLevels, 1u);
        const uint32_t arrayLayers = std::max(texture->arrayLayers, 1u);
        uint64_t totalSize = 0;
        for (uint32_t level = 0; level < mipLevels; ++level) {
            //Mipmap Level 每级 / 2
            const uint64_t mipWidth = level < 32 ? std::max(texture->width >> level, 1u) : 1u;
            const uint64_t mipHeight = level < 32 ? std::max(texture->height >> level, 1u) : 1u;
            const uint64_t mipDepth = level < 32 ? std::max(texture->depth >> level, 1u) : 1u;
            totalSize += mipWidth * mipHeight * mipDepth * bytesPerTexel * arrayLayers;
            if (totalSize > std::numeric_limits<uint32_t>::max()) {
                return 0; //超出返回值能表示的范围
            }
        }
        return static_cast<uint32_t>(totalSize);
    }

    bool Context::CopyTexFromBuffer(RID dstTex, RID srcBuffer) {
        auto* texture = m_textures.Get(dstTex);
        auto* buffer = m_buffers.Get(srcBuffer);
        if (texture == nullptr || buffer == nullptr || texture->image == VK_NULL_HANDLE || buffer->buffer == VK_NULL_HANDLE) {
            std::cerr << "CopyTexFromBuffer: invalid texture or buffer" << std::endl;
            return false;
        }
        if (texture->mipLevels > 1) {
            std::cerr << "Don't Support more mipmap level texture" << std::endl;
            return false;
        }
        if (texture->sampleCount != VK_SAMPLE_COUNT_1_BIT) {
            std::cerr << "CopyTexFromBuffer: multisampled texture is not supported" << std::endl;
            return false;
        }
        if ((texture->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0 ||
            (buffer->usage & VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT) == 0) {
            std::cerr << "CopyTexFromBuffer: texture needs TransferDst usage and buffer needs TransferSrc usage" << std::endl;
            return false;
        }
        const uint32_t textureSize = GetTextureSize(dstTex);
        if (textureSize == 0 || buffer->size < textureSize) {
            std::cerr << "CopyTexFromBuffer: buffer is smaller than the texture" << std::endl;
            return false;
        }

        const VkImage dstImage = texture->image;
        const VkBuffer srcBuf = buffer->buffer;
        const VkFormat format = texture->format;
        const VkImageLayout oldLayout = texture->layout;
        const uint32_t mipLevels = texture->mipLevels;
        const uint32_t arrayLayers = texture->arrayLayers;
        const VkExtent3D extent{texture->width, texture->height, texture->depth};

        //资源上次在别的队列家族用过时先在 CPU 侧等它完成，和 TransitionTextureLayout 的处理一致
        auto waitForPreviousUse = [this](CommandQueueType lastQueue, uint64_t lastTimelineValue) {
            if (lastTimelineValue == 0) {
                return;
            }
            const uint32_t lastFamily = m_core->GetQueueFamilyIndex(lastQueue);
            if (lastFamily != m_core->GetQueueFamilyIndex(CommandQueueType::TRANSFER)) {
                if (TimelineSemaphore* timeline = GetTimeline(lastFamily)) {
                    timeline->Wait(lastTimelineValue);
                }
            }
        };
        waitForPreviousUse(texture->lastQueue, texture->lastTimelineValue);
        waitForPreviousUse(buffer->lastQueue, buffer->lastTimelineValue);

        CommandContext::CommandBuffer& commandBuffer = GetCommandBuffer(CommandQueueType::TRANSFER);
        StartRecording(commandBuffer);

        if (oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            VkImageMemoryBarrier2 barrier = MakeLayoutBarrier(dstImage, format, oldLayout,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels, arrayLayers);
            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commandBuffer.commandBuffer, &dependency);
        }

        VkBufferImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        region.bufferOffset = 0;
        region.bufferRowLength = 0; //紧密打包
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = ImageAspect(format);
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = arrayLayers;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = extent;

        VkCopyBufferToImageInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
        copyInfo.srcBuffer = srcBuf;
        copyInfo.dstImage = dstImage;
        copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;
        vkCmdCopyBufferToImage2(commandBuffer.commandBuffer, &copyInfo);

        EndRecording(commandBuffer);
        const uint64_t signalValue = SubmitCommandBufferTracked(CommandQueueType::TRANSFER, {commandBuffer}, 0, {}, {}, {});

        texture = m_textures.Get(dstTex);
        if (texture != nullptr) {
            texture->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            texture->lastQueue = CommandQueueType::TRANSFER;
            texture->lastTimelineValue = signalValue;
        }
        buffer = m_buffers.Get(srcBuffer);
        if (buffer != nullptr) {
            buffer->lastQueue = CommandQueueType::TRANSFER;
            buffer->lastTimelineValue = signalValue;
        }
        return true;
    }

    bool Context::CopyTexToBuffer(RID srcTex, RID dstBuffer) {
        auto* texture = m_textures.Get(srcTex);
        auto* buffer = m_buffers.Get(dstBuffer);
        if (texture == nullptr || buffer == nullptr || texture->image == VK_NULL_HANDLE || buffer->buffer == VK_NULL_HANDLE) {
            std::cerr << "CopyTexToBuffer: invalid texture or buffer" << std::endl;
            return false;
        }
        if (texture->mipLevels > 1) {
            std::cerr << "Don't Support more mipmap level texture" << std::endl;
            return false;
        }
        if (texture->sampleCount != VK_SAMPLE_COUNT_1_BIT) {
            std::cerr << "CopyTexToBuffer: multisampled texture is not supported" << std::endl;
            return false;
        }
        if ((texture->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0 ||
            (buffer->usage & VK_BUFFER_USAGE_2_TRANSFER_DST_BIT) == 0) {
            std::cerr << "CopyTexToBuffer: texture needs TransferSrc usage and buffer needs TransferDst usage" << std::endl;
            return false;
        }
        const uint32_t textureSize = GetTextureSize(srcTex);
        if (textureSize == 0 || buffer->size < textureSize) {
            std::cerr << "CopyTexToBuffer: buffer is smaller than the texture" << std::endl;
            return false;
        }

        const VkImage srcImage = texture->image;
        const VkBuffer dstBuf = buffer->buffer;
        const VkFormat format = texture->format;
        const VkImageLayout oldLayout = texture->layout;
        const uint32_t mipLevels = texture->mipLevels;
        const uint32_t arrayLayers = texture->arrayLayers;
        const VkExtent3D extent{texture->width, texture->height, texture->depth};
        const bool hostVisible = buffer->hostVisible;

        auto waitForPreviousUse = [this](CommandQueueType lastQueue, uint64_t lastTimelineValue) {
            if (lastTimelineValue == 0) {
                return;
            }
            const uint32_t lastFamily = m_core->GetQueueFamilyIndex(lastQueue);
            if (lastFamily != m_core->GetQueueFamilyIndex(CommandQueueType::TRANSFER)) {
                if (TimelineSemaphore* timeline = GetTimeline(lastFamily)) {
                    timeline->Wait(lastTimelineValue);
                }
            }
        };
        waitForPreviousUse(texture->lastQueue, texture->lastTimelineValue);
        waitForPreviousUse(buffer->lastQueue, buffer->lastTimelineValue);
        //RenderGraph 的提交不按资源登记 timeline, 和 ReadbackBuffer 一样保守地等待计算和图形队列
        for (CommandQueueType queueType : {CommandQueueType::COMPUTE, CommandQueueType::GRAPHICS}) {
            if (TimelineSemaphore* timeline = GetTimeline(m_core->GetQueueFamilyIndex(queueType))) {
                timeline->Wait(timeline->ReadValue());
            }
        }

        CommandContext::CommandBuffer& commandBuffer = GetCommandBuffer(CommandQueueType::TRANSFER);
        StartRecording(commandBuffer);

        if (oldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            VkImageMemoryBarrier2 barrier = MakeLayoutBarrier(srcImage, format, oldLayout,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mipLevels, arrayLayers);
            //专用传输队列不支持图形阶段; 之前的图形 / 计算使用已在上面等待完成, 不需要源阶段依赖
            constexpr VkPipelineStageFlags2 kGraphicsOnlyStages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            if ((barrier.srcStageMask & kGraphicsOnlyStages) != 0) {
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                barrier.srcAccessMask = VK_ACCESS_2_NONE;
            }
            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commandBuffer.commandBuffer, &dependency);
        }

        VkBufferImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        region.bufferOffset = 0;
        region.bufferRowLength = 0; //紧密打包
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = ImageAspect(format);
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = arrayLayers;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = extent;

        VkCopyImageToBufferInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        copyInfo.srcImage = srcImage;
        copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyInfo.dstBuffer = dstBuf;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;
        vkCmdCopyImageToBuffer2(commandBuffer.commandBuffer, &copyInfo);

        if (hostVisible) {
            //让 CPU 后续 Map 读取能看到拷贝结果，和 ReadbackBuffer 的处理一致
            VkBufferMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = dstBuf;
            barrier.offset = 0;
            barrier.size = textureSize;

            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commandBuffer.commandBuffer, &dependency);
        }

        EndRecording(commandBuffer);
        const uint64_t signalValue = SubmitCommandBufferTracked(CommandQueueType::TRANSFER, {commandBuffer}, 0, {}, {}, {});

        texture = m_textures.Get(srcTex);
        if (texture != nullptr) {
            texture->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            texture->lastQueue = CommandQueueType::TRANSFER;
            texture->lastTimelineValue = signalValue;
        }
        buffer = m_buffers.Get(dstBuffer);
        if (buffer != nullptr) {
            buffer->lastQueue = CommandQueueType::TRANSFER;
            buffer->lastTimelineValue = signalValue;
        }
        return true;
    }

    bool Context::CopyTex(RID srcTex, RID dstTex) {
        if (srcTex == dstTex) {
            std::cerr << "CopyTex: source and destination are the same texture" << std::endl;
            return false;
        }
        auto* srcTexture = m_textures.Get(srcTex);
        auto* dstTexture = m_textures.Get(dstTex);
        if (srcTexture == nullptr || dstTexture == nullptr ||
            srcTexture->image == VK_NULL_HANDLE || dstTexture->image == VK_NULL_HANDLE) {
            std::cerr << "CopyTex: invalid texture" << std::endl;
            return false;
        }
        if (srcTexture->mipLevels > 1 || dstTexture->mipLevels > 1) {
            std::cerr << "Don't Support more mipmap level texture" << std::endl;
            return false;
        }
        if (srcTexture->sampleCount != VK_SAMPLE_COUNT_1_BIT || dstTexture->sampleCount != VK_SAMPLE_COUNT_1_BIT) {
            std::cerr << "CopyTex: multisampled texture is not supported" << std::endl;
            return false;
        }
        if ((srcTexture->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0 ||
            (dstTexture->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            std::cerr << "CopyTex: source needs TransferSrc usage and destination needs TransferDst usage" << std::endl;
            return false;
        }
        if (srcTexture->width != dstTexture->width || srcTexture->height != dstTexture->height ||
            srcTexture->depth != dstTexture->depth || srcTexture->format != dstTexture->format ||
            srcTexture->arrayLayers != dstTexture->arrayLayers) {
            std::cerr << "CopyTex: textures must have the same extent, format and array layers" << std::endl;
            return false;
        }
        if (GetTextureSize(srcTex) == 0) {
            std::cerr << "CopyTex: unsupported texture format" << std::endl;
            return false;
        }

        const VkImage srcImage = srcTexture->image;
        const VkImage dstImage = dstTexture->image;
        const VkFormat format = srcTexture->format;
        const VkImageLayout srcOldLayout = srcTexture->layout;
        const VkImageLayout dstOldLayout = dstTexture->layout;
        const uint32_t mipLevels = srcTexture->mipLevels;
        const uint32_t arrayLayers = srcTexture->arrayLayers;
        const VkExtent3D extent{srcTexture->width, srcTexture->height, srcTexture->depth};

        auto waitForPreviousUse = [this](CommandQueueType lastQueue, uint64_t lastTimelineValue) {
            if (lastTimelineValue == 0) {
                return;
            }
            const uint32_t lastFamily = m_core->GetQueueFamilyIndex(lastQueue);
            if (lastFamily != m_core->GetQueueFamilyIndex(CommandQueueType::TRANSFER)) {
                if (TimelineSemaphore* timeline = GetTimeline(lastFamily)) {
                    timeline->Wait(lastTimelineValue);
                }
            }
        };
        waitForPreviousUse(srcTexture->lastQueue, srcTexture->lastTimelineValue);
        waitForPreviousUse(dstTexture->lastQueue, dstTexture->lastTimelineValue);

        CommandContext::CommandBuffer& commandBuffer = GetCommandBuffer(CommandQueueType::TRANSFER);
        StartRecording(commandBuffer);

        std::vector<VkImageMemoryBarrier2> barriers;
        if (srcOldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            barriers.push_back(MakeLayoutBarrier(srcImage, format, srcOldLayout,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mipLevels, arrayLayers));
        }
        if (dstOldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barriers.push_back(MakeLayoutBarrier(dstImage, format, dstOldLayout,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels, arrayLayers));
        }
        if (!barriers.empty()) {
            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            dependency.pImageMemoryBarriers = barriers.data();
            vkCmdPipelineBarrier2(commandBuffer.commandBuffer, &dependency);
        }

        VkImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
        region.srcSubresource.aspectMask = ImageAspect(format);
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = arrayLayers;
        region.srcOffset = {0, 0, 0};
        region.dstSubresource.aspectMask = ImageAspect(format);
        region.dstSubresource.mipLevel = 0;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = arrayLayers;
        region.dstOffset = {0, 0, 0};
        region.extent = extent;

        VkCopyImageInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
        copyInfo.srcImage = srcImage;
        copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyInfo.dstImage = dstImage;
        copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;
        vkCmdCopyImage2(commandBuffer.commandBuffer, &copyInfo);

        EndRecording(commandBuffer);
        const uint64_t signalValue = SubmitCommandBufferTracked(CommandQueueType::TRANSFER, {commandBuffer}, 0, {}, {}, {});

        srcTexture = m_textures.Get(srcTex);
        if (srcTexture != nullptr) {
            srcTexture->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            srcTexture->lastQueue = CommandQueueType::TRANSFER;
            srcTexture->lastTimelineValue = signalValue;
        }
        dstTexture = m_textures.Get(dstTex);
        if (dstTexture != nullptr) {
            dstTexture->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dstTexture->lastQueue = CommandQueueType::TRANSFER;
            dstTexture->lastTimelineValue = signalValue;
        }
        return true;
    }

    std::vector<RID> Context::RegisterSwapchainImages(const std::vector<VkImage>& images) {
        //创建Textures
        std::vector<RID> results;
        for (const auto& image : images) {
            VulkanTexture texture = {};
            texture.image = image;
            //Width 和 Height 不用管理，直接取swapchain中获取
            texture.isSwapchain = true;

            results.push_back(m_textures.Add(std::move(texture)));
        }
        return results;
    }

    void Context::UnRegisterSwapchainImages(const std::vector<RID>& imageRIDs) {
        for (const auto& rid : imageRIDs)
            m_textures.Remove(rid);
    }

    CommandContext::CommandBuffer& Context::GetCommandBuffer(CommandQueueType queueType, uint32_t frameIndex) {
        return m_commandContext->GetCommandBuffer(m_core->GetQueueFamilyIndex(queueType), frameIndex);
    }

    void Context::StartRecording(CommandContext::CommandBuffer& commandBuffer) {
        TimelineSemaphore* timeline = GetTimeline(commandBuffer.queueFamilyIndex);
        const uint64_t pendingValue = timeline != nullptr ? timeline->ReadValue() : 0;
        if (timeline != nullptr) {
            timeline->Wait(pendingValue);
        }

        VULKAN_CHECK(vkResetCommandBuffer(commandBuffer.commandBuffer, 0));
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VULKAN_CHECK(vkBeginCommandBuffer(commandBuffer.commandBuffer, &beginInfo));
    }

    void Context::EndRecording(CommandContext::CommandBuffer& commandBuffer) {
        VULKAN_CHECK(vkEndCommandBuffer(commandBuffer.commandBuffer));
    }

    //非常薄的封装
    VkExtent2D Context::BeginRendering(VkCommandBuffer commandBuffer, const RenderDesc& renderingInfo) {
        uint32_t width{}, height{};
        //Validation
        if (!CheckAndNormalizeRenderDesc(renderingInfo, width, height)) {
            throw std::runtime_error("[RHI Context BeginRendering]: RenderDesc is wrong!");
        }

        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        colorAttachments.reserve(renderingInfo.colorAttachments.size());
        for (const AttachmentInfo& color : renderingInfo.colorAttachments) {
            colorAttachments.push_back(GetAttachmentInfo(color));
        }

        VkRenderingInfo renderInfo = {};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = {{renderingInfo.offsetX, renderingInfo.offsetY}, {width, height}};
        renderInfo.layerCount = renderingInfo.layerCount;
        renderInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        renderInfo.pColorAttachments = colorAttachments.data();

        VkRenderingAttachmentInfo depthAttachment = {};
        if (renderingInfo.depthAttachment.textureView) {
            depthAttachment = GetAttachmentInfo(renderingInfo.depthAttachment);
            renderInfo.pDepthAttachment = &depthAttachment;
        }

        vkCmdBeginRendering(commandBuffer, &renderInfo);
        return renderInfo.renderArea.extent;
    }

    void Context::EndRendering(VkCommandBuffer commandBuffer) {
        vkCmdEndRendering(commandBuffer);
    }

    void Context::SubmitCommandBuffer(CommandQueueType queueType, const std::vector<CommandContext::CommandBuffer>& commandBuffers, uint64_t signalValue, const std::vector<RID>& waitSemaphores, const std::vector<RID>& signalSemaphores, const std::vector<VkPipelineStageFlags2> pipelineStageFlags) {
        SubmitCommandBufferTracked(queueType, commandBuffers, signalValue, waitSemaphores, signalSemaphores, pipelineStageFlags);
    }

    TimelineSemaphore::Signal Context::PrepareFrame() {
        if (!m_settings.bUseSwapChain) {
            throw std::runtime_error("bUseSwapChain is false");
        }

        //这里需要特判第一帧
        //如果是第一帧，直接等待graphics管线的任务，如果不是第一帧，则需要等待先前的任务
        uint32_t graphicsIndex = m_core->GetGraphicsQueueFamilyIndex();
        auto& graphicsTimeline = m_timelines[graphicsIndex];
        TimelineSemaphore::Signal returnSignal = {};
        if (!m_bRenderStarted) {
            returnSignal = graphicsTimeline->BeginSignal();
            m_frameFenceNums[m_currentFrameIndex] = returnSignal.current;
        } else {
            graphicsTimeline->Wait(m_frameFenceNums[m_currentFrameIndex]);
            returnSignal =  graphicsTimeline->BeginSignal();
        }

        //Swapchain
        uint32_t imageIndex = 0;
        m_swapchain->AcquireNextImage(m_presentCompletedSemaphores[m_currentFrameIndex], imageIndex);


        return returnSignal;
    }

    void Context::PresentFrame(TimelineSemaphore::Signal&& signal) {
        if (!m_settings.bUseSwapChain) {
            std::cerr << "bUseSwapChain is false" << std::endl;
            return;
        }

    }


    void Context::DestroyResource(RID rid) {
        if (!rid) {
            return;
        }
        if (rid.type == RID::GetIndex<VulkanBuffer>()) {
            DestroyBuffer(rid);
        } else if (rid.type == RID::GetIndex<VulkanTexture>()) {
            DestroyTexture(rid);
        } else if (rid.type == RID::GetIndex<TextureView>()) {
            DestroyImageView(rid);
        } else if (rid.type == RID::GetIndex<VkSemaphore>()) {
            DestroySemaphore(rid);
        } else if (rid.type == RID::GetIndex<ComputePipeline>()) {
            DestroyComputePipeline(rid);
        } else if (rid.type == RID::GetIndex<GraphicsShaderObject>()) {
            DestroyGraphicsShaderObject(rid);
        } else if (rid.type == RID::GetIndex<DescriptorSlotBinding>()) {
            DestroyDescriptorSlotBinding(rid);
        } else {
            throw std::runtime_error("Invalid resource type");
        }
    }

    uint64_t Context::SubmitCommandBufferTracked(CommandQueueType queueType, const std::vector<CommandContext::CommandBuffer>& commandBuffers, uint64_t signalValue, const std::vector<RID>& waitSemaphores, const std::vector<RID>& signalSemaphores, const std::vector<VkPipelineStageFlags2>& pipelineStageFlags) {
        if (commandBuffers.empty()) {
            return 0;
        }

        const uint32_t queueFamilyIndex = m_core->GetQueueFamilyIndex(queueType);
        std::vector<VkCommandBufferSubmitInfo> commandBufferInfos;
        commandBufferInfos.reserve(commandBuffers.size());
        for (const CommandContext::CommandBuffer& commandBuffer : commandBuffers) {
            if (commandBuffer.queueFamilyIndex != queueFamilyIndex) {
                throw std::runtime_error("Command buffer does not belong to the submit queue");
            }
            VkCommandBufferSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            info.commandBuffer = commandBuffer.commandBuffer;
            commandBufferInfos.push_back(info);
        }

        std::vector<VkSemaphoreSubmitInfo> userWaits;
        userWaits.reserve(waitSemaphores.size());
        for (size_t i = 0; i < waitSemaphores.size(); ++i) {
            VkSemaphore* semaphore = m_semaphores.Get(waitSemaphores[i]);
            if (semaphore == nullptr || *semaphore == VK_NULL_HANDLE) {
                throw std::runtime_error("Invalid wait semaphore");
            }
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = *semaphore;
            info.stageMask = i < pipelineStageFlags.size() ? pipelineStageFlags[i] : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            userWaits.push_back(info);
        }

        std::vector<VkSemaphore> userSignals;
        userSignals.reserve(signalSemaphores.size());
        for (RID rid : signalSemaphores) {
            VkSemaphore* semaphore = m_semaphores.Get(rid);
            if (semaphore == nullptr || *semaphore == VK_NULL_HANDLE) {
                throw std::runtime_error("Invalid signal semaphore");
            }
            userSignals.push_back(*semaphore);
        }

        auto appendWait = [](std::vector<VkSemaphoreSubmitInfo>& waits, VkSemaphore semaphore, uint64_t value) {
            if (semaphore == VK_NULL_HANDLE || value == 0) {
                return;
            }
            for (VkSemaphoreSubmitInfo& wait : waits) {
                if (wait.semaphore == semaphore) {
                    if (value > wait.value) {
                        wait.value = value;
                    }
                    return;
                }
            }
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = semaphore;
            info.value = value;
            info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            waits.push_back(info);
        };

        TimelineSemaphore* timeline = GetTimeline(queueFamilyIndex);
        if (timeline == nullptr) {
            throw std::runtime_error("Queue family has no timeline semaphore");
        }

        TimelineSemaphore::Signal signal = timeline->BeginSignal(signalValue);

        VkSemaphore transferSemaphore = VK_NULL_HANDLE;
        uint64_t transferValue = 0;
        if (TimelineSemaphore* transferTimeline = GetTimeline(m_core->GetQueueFamilyIndex(CommandQueueType::TRANSFER));
            transferTimeline != nullptr && transferTimeline != timeline) {
            transferSemaphore = transferTimeline->GetSemaphore();
            transferValue = transferTimeline->ReadValue();
        }
        //保证异步的上传队列上传完毕后再进行提交

        std::vector<VkSemaphoreSubmitInfo> waits = userWaits;
        appendWait(waits, timeline->GetSemaphore(), signal.previous);
        appendWait(waits, transferSemaphore, transferValue);

        std::vector<VkSemaphoreSubmitInfo> signals;
        signals.reserve(userSignals.size() + 1);
        for (VkSemaphore semaphore : userSignals) {
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = semaphore;
            info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signals.push_back(info);
        }
        VkSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.semaphore = timeline->GetSemaphore();
        timelineInfo.value = signal.current;
        timelineInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signals.push_back(timelineInfo);

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
        submitInfo.pWaitSemaphoreInfos = waits.empty() ? nullptr : waits.data();
        submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandBufferInfos.size());
        submitInfo.pCommandBufferInfos = commandBufferInfos.data();
        submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
        submitInfo.pSignalSemaphoreInfos = signals.data();
        VULKAN_CHECK(vkQueueSubmit2(m_core->GetVkQueue(queueType), 1, &submitInfo, VK_NULL_HANDLE));
        return signal.current;
    }


    RID Context::CreateComputePipeline(const std::string& name, VkShaderModule shaderModule) {
        ComputePipeline pipeline;
        pipeline.Init(m_core->GetVkDevice(), name, shaderModule, DescriptorSlot::GetGlobalPipelineLayout());
        return m_computePipelines.Add(std::move(pipeline));
    }

    RID Context::CreateComputePipeline(const std::string& name, void* shaderCode, uint32_t size) {
        ComputePipeline pipeline;
        VkShaderModule shaderModule{};
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType =  VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = size;
        createInfo.pCode = reinterpret_cast<uint32_t*>(shaderCode);

        VULKAN_CHECK(vkCreateShaderModule(m_core->GetVkDevice(), &createInfo, nullptr, &shaderModule));
        pipeline.Init(m_core->GetVkDevice(), name, shaderModule, DescriptorSlot::GetGlobalPipelineLayout());
        auto rid = m_computePipelines.Add(std::move(pipeline));
        vkDestroyShaderModule(m_core->GetVkDevice(), shaderModule, nullptr);
        return rid;
    }

    void Context::DestroyComputePipeline(RID rid) {
        ComputePipeline* pipeline = m_computePipelines.Get(rid);
        if (pipeline != nullptr) {
            pipeline->Destroy();
        }
        m_computePipelines.Remove(rid);
    }

    VkPipeline Context::GetComputePipeline(RID rid) {
        ComputePipeline* pipeline = m_computePipelines.Get(rid);
        return pipeline != nullptr ? pipeline->GetVkPipeline() : VK_NULL_HANDLE;
    }

    RID Context::CreateGraphicsShaderObject(const std::string& name, void* vertCode, uint32_t vertSize, void* fragCode, uint32_t fragSize) {
        if (vertCode == nullptr || fragCode == nullptr || vertSize == 0 || fragSize == 0 ||
            (vertSize % sizeof(uint32_t)) != 0 || (fragSize % sizeof(uint32_t)) != 0) {
            throw std::invalid_argument("Graphics shader SPIR-V is empty or not 4-byte aligned: " + name);
        }

        const VkDescriptorSetLayout setLayout = DescriptorSlot::GetGlobalDescriptorSetLayout();
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = DESCRIPTOR_SHADER_STAGES;
        pushConstantRange.offset = 0;
        pushConstantRange.size = DESCRIPTOR_PUSH_CONSTANT_SIZE;

        //VS 和 PS 一起创建并 link, 插值接口由驱动在创建时匹配
        std::vector<VkShaderCreateInfoEXT> shaderInfos(2);
        shaderInfos[0].sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
        shaderInfos[0].flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT;
        shaderInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderInfos[0].nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderInfos[0].codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
        shaderInfos[0].codeSize = vertSize;
        shaderInfos[0].pCode = vertCode;
        shaderInfos[0].pName = "main";
        shaderInfos[0].setLayoutCount = 1;
        shaderInfos[0].pSetLayouts = &setLayout;
        shaderInfos[0].pushConstantRangeCount = 1;
        shaderInfos[0].pPushConstantRanges = &pushConstantRange;

        shaderInfos[1] = shaderInfos[0];
        shaderInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderInfos[1].nextStage = 0;
        shaderInfos[1].codeSize = fragSize;
        shaderInfos[1].pCode = fragCode;

        GraphicsShaderObject shaderObject;
        shaderObject.Init(m_core.get(), name, shaderInfos);
        return m_graphicsShaderObjects.Add(std::move(shaderObject));
    }

    void Context::DestroyGraphicsShaderObject(RID rid) {
        GraphicsShaderObject* shaderObject = m_graphicsShaderObjects.Get(rid);
        if (shaderObject != nullptr) {
            shaderObject->Destroy();
        }
        m_graphicsShaderObjects.Remove(rid);
    }

    GraphicsShaderObject* Context::GetGraphicsShaderObject(RID rid) {
        return m_graphicsShaderObjects.Get(rid);
    }

    void Context::BindDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint) const {
        m_descriptorSlot->BindDescriptorSet(commandBuffer, bindPoint);
    }

    RID Context::CreateDescriptorSlotBinding(const DescriptorSlotLayout& descriptorSlotLayout) {
        auto descriptorSlotBinding = m_descriptorSlot->CreateSlotBinding(descriptorSlotLayout);
        return m_descriptorSlots.Add(std::move(descriptorSlotBinding));
    }

    void Context::DestroyDescriptorSlotBinding(RID rid) {
        auto* descriptorSlotBinding = m_descriptorSlots.Get(rid);
        if (descriptorSlotBinding) {
            descriptorSlotBinding->Release();
        }
        m_descriptorSlots.Remove(rid);
    }
}
