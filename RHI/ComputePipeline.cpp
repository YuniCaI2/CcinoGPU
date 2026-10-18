#include "ComputePipeline.h"
#include "RHI/Core.h"
#include <stdexcept>

namespace Ccino::RHI {
    void ComputePipeline::Init(VkDevice device, const std::string& name, VkShaderModule shaderModule, VkPipelineLayout pipelineLayout) {
        if (pipelineLayout == VK_NULL_HANDLE) {
            throw std::runtime_error("Compute pipeline needs the global pipeline layout");
        }
        m_device = device;
        m_name = name;
        m_pipelineLayout = pipelineLayout;

        VkPipelineShaderStageCreateInfo shaderStageInfo = {};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        //所有管线共用全局PipelineLayout: set 0 是全局DescriptorSet, PushConstant 是UniformBuffer 的BDA
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = m_pipelineLayout;

        VULKAN_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));
    }
    void ComputePipeline::Destroy() {
        if (m_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
            m_pipeline = VK_NULL_HANDLE;
        }
        m_pipelineLayout = VK_NULL_HANDLE; //全局共享, 由DescriptorSlot::DestroyGlobal 销毁
    }

}
