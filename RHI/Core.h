#pragma once
//这里实现Vulkan的基础功能
#include <vulkan/vulkan.h>
#include <vector>
#include <vk_mem_alloc.h>

#include "RHI/RHIStruct.h"

#ifdef NDEBUG
    #define USE_VALIDATION_LAYERS 0
#else
    #define USE_VALIDATION_LAYERS 1
#endif

#define VULKAN_CHECK(expr) do { \
    auto result = (expr); \
    if (result != VK_SUCCESS) { \
        throw std::runtime_error("Vulkan error: " #expr); \
    } \
} while (0)

namespace Ccino::RHI {
    class Core {
    public:
        Core() = default;
        ~Core() = default;

        void Init(const RHISettings& settings);
        void Destroy();

        VkDevice GetVkDevice() const { return m_device; }
        VkPhysicalDevice GetVkPhysicalDevice() const { return m_physicalDevice; }
        VkInstance GetVkInstance() const { return m_instance; }
        VmaAllocator GetVmaAllocator() const { return m_vmaAllocator; }
        uint32_t GetGraphicsQueueFamilyIndex() const { return m_queueFamilyIndices.graphics; }
        uint32_t GetComputeQueueFamilyIndex() const { return m_queueFamilyIndices.compute; }
        uint32_t GetTransferQueueFamilyIndex() const { return m_queueFamilyIndices.transfer; }
        const VkPhysicalDeviceProperties& GetPhysicalDeviceProperties() const { return m_properties; }
        const VkPhysicalDeviceDescriptorBufferPropertiesEXT& GetDescriptorBufferProp() const { return m_descriptorBufferProps; }
        PFN_vkGetDescriptorSetLayoutSizeEXT GetVkGetDescriptorSetLayoutSizeEXT() const { return m_vkGetDescriptorSetLayoutSizeEXT; }
        PFN_vkGetDescriptorSetLayoutBindingOffsetEXT GetVkGetDescriptorSetLayoutBindingOffsetEXT() const { return m_vkGetDescriptorSetLayoutBindingOffsetEXT; }
        PFN_vkGetDescriptorEXT GetVkGetDescriptorEXT() const { return m_vkGetDescriptorEXT; }
        PFN_vkCmdBindDescriptorBuffersEXT GetVkCmdBindDescriptorBuffersEXT() const { return m_vkCmdBindDescriptorBuffersEXT; }
        PFN_vkCmdSetDescriptorBufferOffsetsEXT GetVkCmdSetDescriptorBufferOffsetsEXT() const { return m_vkCmdSetDescriptorBufferOffsetsEXT; }

    private:
        VkInstance m_instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;

        struct {
            uint32_t graphics;
            uint32_t compute;
            uint32_t transfer;
        } m_queueFamilyIndices{};

        VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;

        VkPhysicalDeviceFeatures2 m_enabledFeatures{};
        VkPhysicalDeviceProperties2 m_properties2{};
        VkPhysicalDeviceProperties m_properties{};
        VkPhysicalDeviceDescriptorBufferPropertiesEXT m_descriptorBufferProps{};

        // VK_EXT_descriptor_buffer 扩展函数指针
        PFN_vkGetDescriptorSetLayoutSizeEXT m_vkGetDescriptorSetLayoutSizeEXT = nullptr;
        PFN_vkGetDescriptorSetLayoutBindingOffsetEXT m_vkGetDescriptorSetLayoutBindingOffsetEXT = nullptr;
        PFN_vkGetDescriptorEXT m_vkGetDescriptorEXT = nullptr;
        PFN_vkCmdBindDescriptorBuffersEXT m_vkCmdBindDescriptorBuffersEXT = nullptr;
        PFN_vkCmdSetDescriptorBufferOffsetsEXT m_vkCmdSetDescriptorBufferOffsetsEXT = nullptr;

#ifndef NDEBUG
        PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
        PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
#endif

    private:
        void CreateInstance(const std::vector<const char*>& requiredExtensions);
        void DestroyInstance();

        void CreateDevice(VkInstance instance, VkPhysicalDeviceFeatures features, const std::vector<const char*>& extensions, void* pNext);
        void DestroyDevice();

        void CreateSurface();
        void DestroySurface();

        //Dynamic Function Pointers
        void InitDynamicFunctions();

        void CreateVmaAllocator();
        void DestroyVmaAllocator();

        //Debug
        void SetDebugging(VkInstance instance);
        void DestroyDebugCallback(VkInstance instance);
    };
}
