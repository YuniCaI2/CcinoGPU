#pragma once
#include <stdint.h>
#include <vulkan/vulkan_core.h>
#include <vector>
#include <RHI/RHIStruct.h>

namespace Ccino::RHI {
    class Core;
    class Context;
    class Swapchain {
    public:
        void Init(RHI::Context* context);
        void Destroy();

        //此处负责Swapchain的建立和重建
        void Create(uint32_t width, uint32_t height, bool vsync); //先使用这三个选项

        std::vector<RID>& GetTextures() {return m_textures;}
        VkFormat GetColorFormat() const { return m_colorFormat; }
        VkColorSpaceKHR GetColorSpace() const { return m_colorSpace; }
        VkExtent2D GetExtent() const { return m_extent; }

        VkResult GetNextImage(VkSemaphore presentCompleteSemaphore, uint32_t& imageIndex); //获取下一个可用的交换链索引,这里不使用RID，减少索引开销
        //处理当前的Present
        VkResult PresentFrame(uint32_t imageIndex, VkSemaphore presentReadySemaphore);
        VkResult AcquireNextImage(VkSemaphore presentCompleteSemaphore, uint32_t& imageIndex);

    private:
        friend class RHI::Context;

        void SelectSurfaceFormat();

        RHI::Core* m_core{};
        RHI::Context* m_context;
        VkSwapchainKHR m_vulkanSwapchain{};
        VkSurfaceKHR m_surface{};
        VkFormat m_colorFormat{};
        VkColorSpaceKHR m_colorSpace{};
        VkExtent2D m_extent{0, 0};
        VkImageUsageFlags m_imageUsage{};

        uint32_t m_minImageCount{UINT32_MAX};

        //这里的Image应该如何管理呢？我认为应该托管到Context 变成RID，但这个RID不会直接释放
        std::vector<VkImage> m_images;
        std::vector<RID> m_textures; //直接是images的RID句柄
    };
}
