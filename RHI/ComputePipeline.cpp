#include "ComputePipeline.h"
#include "RHI/Core.h"

namespace Ccino::RHI {
    void ComputePipeline::Init(VkDevice device, const std::string& name, VkShaderModule shaderModule) {
        m_device = device;
        m_name = name;

        VkPipelineShaderStageCreateInfo shaderStageInfo = {};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        //Shader 直接访问Descriptor Heap, 管线不能带Pipeline Layout
        VkPipelineCreateFlags2CreateInfo flagsInfo{};
        flagsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO;
        flagsInfo.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &flagsInfo;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = VK_NULL_HANDLE;

        VULKAN_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));
    }
    void ComputePipeline::Destroy() {
        if (m_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
            m_pipeline = VK_NULL_HANDLE;
        }
    }

}
