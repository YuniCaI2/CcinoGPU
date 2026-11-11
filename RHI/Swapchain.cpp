#include "RHI/Swapchain.h"
#include "Core.h"
#include "RHI/Context.h"
#include "RHI/RHIStruct.h"
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>


namespace Ccino::RHI {
    void Swapchain::Init(RHI::Context* context) {
        m_context = context;
        m_core = &context->GetCore();
        m_surface = m_core->GetSurface();
        if (m_surface == VK_NULL_HANDLE) {
            throw std::runtime_error("Swapchain requires a valid surface");
        }
        SelectSurfaceFormat();
    }

    //优先SRGB 格式, 着色器输出线性颜色由硬件做Gamma 编码; 都不支持时退回设备返回的第一个格式
    void Swapchain::SelectSurfaceFormat() {
        const VkPhysicalDevice physicalDevice = m_core->GetVkPhysicalDevice();
        uint32_t formatCount = 0;
        VULKAN_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, nullptr));
        if (formatCount == 0) {
            throw std::runtime_error("Surface does not report any supported format");
        }
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        VULKAN_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, formats.data()));

        //只返回一个UNDEFINED 表示Surface 不限制格式
        if (formatCount == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
            m_colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
            m_colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            return;
        }

        constexpr VkFormat preferredFormats[] = {
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
        };
        for (VkFormat preferred : preferredFormats) {
            for (const VkSurfaceFormatKHR& format : formats) {
                if (format.format == preferred && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    m_colorFormat = format.format;
                    m_colorSpace = format.colorSpace;
                    return;
                }
            }
        }

        m_colorFormat = formats[0].format;
        m_colorSpace = formats[0].colorSpace;
    }

    void Swapchain::Destroy() {
        if (m_vulkanSwapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(m_core->GetVkDevice(), m_vulkanSwapchain, nullptr);
    }

    //此处负责Swapchain的建立和重建
    void Swapchain::Create(uint32_t width, uint32_t height, bool vsync) {
        auto swapchain = m_vulkanSwapchain; //记录老Swapchain

        VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_core->GetVkPhysicalDevice(), m_surface, &surfaceCapabilities);

        VkExtent2D swapchainExtent = {};
        if (surfaceCapabilities.currentExtent.width == UINT32_MAX) {
            swapchainExtent.width  = std::clamp(width,
                surfaceCapabilities.minImageExtent.width,
                surfaceCapabilities.maxImageExtent.width);
            swapchainExtent.height = std::clamp(height,
                surfaceCapabilities.minImageExtent.height,
                surfaceCapabilities.maxImageExtent.height);
        } else {
            swapchainExtent = surfaceCapabilities.currentExtent;
        }

        m_extent = swapchainExtent;

        VkPresentModeKHR swapChainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

        if(!vsync) {
            uint32_t presentModeCount;
            vkGetPhysicalDeviceSurfacePresentModesKHR(m_core->GetVkPhysicalDevice(), m_surface, &presentModeCount, nullptr);
            std::vector<VkPresentModeKHR> presentModes(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(m_core->GetVkPhysicalDevice(), m_surface, &presentModeCount, presentModes.data());
            for (size_t i = 0; i < presentModes.size(); i++) {
                if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    swapChainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }
                else if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    swapChainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                }
            }
        }

        // 确定图片的数量
        uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
        if (surfaceCapabilities.maxImageCount > 0 && imageCount >
            surfaceCapabilities.maxImageCount) {
            imageCount = surfaceCapabilities.maxImageCount;
        }
        m_minImageCount = imageCount; //实际请求的最小值

        //选择变换
        VkSurfaceTransformFlagBitsKHR preTransform;
        if (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
            //别转
            preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        }
        else {
            preTransform = surfaceCapabilities.currentTransform;
        }

        //选择合适的alpha混合格式(这里可以让渲染的窗口直接透明等等)
        VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, // 预先乘上alpha通道
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,// 后乘alpha通道
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
        };
        for (auto &alphaFlag: compositeAlphaFlags) {
            if (surfaceCapabilities.supportedCompositeAlpha & alphaFlag) {
                compositeAlpha = alphaFlag;
                break;
            }
        }
        VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
        swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChainCreateInfo.surface = m_surface;
        swapChainCreateInfo.minImageCount = imageCount;
        swapChainCreateInfo.imageFormat = m_colorFormat;
        swapChainCreateInfo.imageColorSpace = m_colorSpace;
        swapChainCreateInfo.imageExtent = {m_extent.width, m_extent.height};
        swapChainCreateInfo.imageArrayLayers = 1;
        swapChainCreateInfo.queueFamilyIndexCount = 0;
        swapChainCreateInfo.presentMode = swapChainPresentMode;
        swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapChainCreateInfo.preTransform = preTransform;
        swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // 表示共享
        swapChainCreateInfo.oldSwapchain = swapchain;
        swapChainCreateInfo.compositeAlpha = compositeAlpha;
        //Vulkan 可以丢弃被系统UI遮挡的部分（有点意思）
        swapChainCreateInfo.clipped = VK_TRUE;

        if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
            swapChainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        //传输的目标
        if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
            swapChainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        VULKAN_CHECK(vkCreateSwapchainKHR(m_core->GetVkDevice(), &swapChainCreateInfo, nullptr, &m_vulkanSwapchain));
        m_imageUsage = swapChainCreateInfo.imageUsage;

        //如果交换链被重新创建，销毁旧的交换链以及资源
        if (swapchain != VK_NULL_HANDLE) {
            for (const RID& viewRID : m_textureViews) {
                m_context->DestroyImageView(viewRID);
            }
            m_textureViews.clear();
            vkDestroySwapchainKHR(m_core->GetVkDevice(), swapchain, nullptr);
            m_context->UnRegisterSwapchainImages(m_textures);
        }
        imageCount = 0;
        VULKAN_CHECK(vkGetSwapchainImagesKHR(m_core->GetVkDevice(), m_vulkanSwapchain, &imageCount, nullptr));
        m_images.resize(imageCount);
        VULKAN_CHECK(vkGetSwapchainImagesKHR(m_core->GetVkDevice(), m_vulkanSwapchain, &imageCount, m_images.data()));
        m_textures = m_context->RegisterSwapchainImages(m_images); //获得注册
        m_textureViews.reserve(m_textures.size());
        for (const RID& textureRID : m_textures) {
            m_textureViews.push_back(m_context->CreateTextureView(textureRID, ViewRange{}));
        }
    }

    VkResult Swapchain::PresentFrame(uint32_t imageIndex, VkSemaphore presentReadySemaphore) {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &presentReadySemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_vulkanSwapchain;
        presentInfo.pImageIndices = &imageIndex;

        return vkQueuePresentKHR(m_core->GetVkQueue(CommandQueueType::GRAPHICS), &presentInfo);
    }

    VkResult Swapchain::AcquireNextImage(VkSemaphore presentCompleteSemaphore, uint32_t& imageIndex) {
        auto result = vkAcquireNextImageKHR(m_core->GetVkDevice(), m_vulkanSwapchain, UINT64_MAX, presentCompleteSemaphore, VK_NULL_HANDLE, &imageIndex);
        return result;
    }
}
