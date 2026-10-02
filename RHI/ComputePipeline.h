#pragma once
#include <string>
#include <vulkan/vulkan_core.h>

//Compute Pipeline 快速实现计算原型

namespace Ccino::RHI {
    class ComputePipeline {
    public:
        void Init(VkDevice device, const std::string& name, VkShaderModule shaderModule);
        void Destroy();
    private:
        std::string m_name;

        VkPipeline m_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;

        bool m_Async = false; //是否是异步计算， 是则使用异步计算队列
    };
}