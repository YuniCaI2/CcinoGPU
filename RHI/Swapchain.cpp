#include "RHI/Swapchain.h"
#include "Core.h"
#include "RHI/Context.h"
#include "RHI/RHIStruct.h"
#include <algorithm>
#include <vulkan/vulkan_core.h>


namespace Ccino::RHI {
    void Swapchain::Init(RHI::Context* context) {
        m_context = context;
        m_core = &context->GetCore();
        m_surface = m_core->GetSurface();
        //创建Swapchain和Surface

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

        //如果交换链被重新创建，销毁旧的交换链以及资源
        if (swapchain != VK_NULL_HANDLE) {
            //这里ImageView不管理
            vkDestroySwapchainKHR(m_core->GetVkDevice(), swapchain, nullptr);
            m_context->UnRegisterSwapchainImages(m_textures);
        }
        imageCount = 0;
        VULKAN_CHECK(vkGetSwapchainImagesKHR(m_core->GetVkDevice(), m_vulkanSwapchain, &imageCount, nullptr));
        m_images.resize(imageCount);
        VULKAN_CHECK(vkGetSwapchainImagesKHR(m_core->GetVkDevice(), m_vulkanSwapchain, &imageCount, m_images.data()));
        m_textures = m_context->RegisterSwapchainImages(m_images); //获得注册
    }

    void Swapchain::PresentFrame(uint32_t imageIndex, VkSemaphore imageReadySemaphore) {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &imageReadySemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_vulkanSwapchain;
        presentInfo.pImageIndices = &imageIndex;

        vkQueuePresentKHR(m_core->GetVkQueue(CommandQueueType::GRAPHICS), &presentInfo);
    }

    VkResult Swapchain::AcquireNextImage(VkSemaphore presentCompleteSemaphore, uint32_t& imageIndex) {
        auto result = vkAcquireNextImageKHR(m_core->GetVkDevice(), m_vulkanSwapchain, UINT64_MAX, presentCompleteSemaphore, VK_NULL_HANDLE, &imageIndex);
        return result;
    }
}
