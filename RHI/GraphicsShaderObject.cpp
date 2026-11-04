#include "RHI/GraphicsShaderObject.h"
#include "RHI/Core.h"

#include <algorithm>
#include <stdexcept>

namespace {
    //图形阶段的固定顺序, GraphicsShaderObject::m_stageShaders 的下标与之一一对应
    constexpr std::array<VkShaderStageFlagBits, 5> kGraphicsStages = {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    //返回阶段在 kGraphicsStages 中的下标, 不是图形阶段则返回 -1
    int GraphicsStageSlot(VkShaderStageFlagBits stage) {
        const auto it = std::find(kGraphicsStages.begin(), kGraphicsStages.end(), stage);
        return it == kGraphicsStages.end() ? -1 : static_cast<int>(it - kGraphicsStages.begin());
    }
}

namespace Ccino::RHI {
    void GraphicsShaderObject::Init(Core* core, const std::string& name, const std::vector<VkShaderCreateInfoEXT>& shaderInfo) {
        if (core == nullptr) {
            throw std::invalid_argument("GraphicsShaderObject::Init needs a valid Core: " + name);
        }
        m_core = core;
        m_name = name;

        //创建前先校验: 只接受图形阶段, 且每个阶段最多一个 Shader (失败时还没有创建任何句柄, 不会泄漏)
        std::array<bool, kGraphicsStages.size()> stageUsed{};
        for (const VkShaderCreateInfoEXT& info : shaderInfo) {
            const int slot = GraphicsStageSlot(info.stage);
            if (slot < 0 || stageUsed[slot]) {
                throw std::invalid_argument("GraphicsShaderObject accepts at most one shader per graphics stage: " + m_name);
            }
            stageUsed[slot] = true;
        }

        std::vector<VkShaderEXT> created(shaderInfo.size(), VK_NULL_HANDLE);
        VULKAN_CHECK(m_core->vkCreateShadersEXT(m_core->GetVkDevice(), static_cast<uint32_t>(shaderInfo.size()),
            shaderInfo.data(), nullptr, created.data()));

        //放到各自的阶段槽位, 没用到的阶段保持 VK_NULL_HANDLE
        m_stageShaders.fill(VK_NULL_HANDLE);
        for (size_t i = 0; i < shaderInfo.size(); ++i) {
            m_stageShaders[GraphicsStageSlot(shaderInfo[i].stage)] = created[i];
        }
    }

    void GraphicsShaderObject::Bind(VkCommandBuffer commandBuffer, const ShaderObjectBindingSetting& setting) const {
        if (m_core == nullptr) {
            throw std::runtime_error("GraphicsShaderObject is not initialized: " + m_name);
        }
        if (setting.extent.width == 0 || setting.extent.height == 0) {
            throw std::invalid_argument("ShaderObjectBindingSetting::extent must be non-zero: " + m_name);
        }

        //1. Shader: 五个图形阶段一起给出, 未使用的阶段是 VK_NULL_HANDLE, 会被解绑, 不残留上一次 Draw 的 Shader
        m_core->vkCmdBindShadersEXT(commandBuffer, static_cast<uint32_t>(kGraphicsStages.size()),
            kGraphicsStages.data(), m_stageShaders.data());

        //2. 顶点输入: 顶点数据全部通过 BDA 读取, 不使用 VertexBuffer
        m_core->vkCmdSetVertexInputEXT(commandBuffer, 0, nullptr, 0, nullptr);

        //3. 图元装配
        vkCmdSetPrimitiveTopology(commandBuffer, setting.topology);
        vkCmdSetPrimitiveRestartEnable(commandBuffer, VK_FALSE);

        //4. 视口 / 裁剪
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(setting.extent.width),
            static_cast<float>(setting.extent.height), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, setting.extent};
        vkCmdSetViewportWithCount(commandBuffer, 1, &viewport);
        vkCmdSetScissorWithCount(commandBuffer, 1, &scissor);

        //5. 光栅化
        vkCmdSetRasterizerDiscardEnable(commandBuffer, VK_FALSE);
        m_core->vkCmdSetPolygonModeEXT(commandBuffer, setting.polygonMode);
        vkCmdSetCullMode(commandBuffer, setting.cullMode);
        vkCmdSetFrontFace(commandBuffer, setting.frontFace);
        vkCmdSetDepthBiasEnable(commandBuffer, VK_FALSE);
        vkCmdSetLineWidth(commandBuffer, 1.0f);

        //6. 多重采样 (当前只使用单采样)
        constexpr VkSampleMask sampleMask = 0xFFFFFFFFu;
        m_core->vkCmdSetRasterizationSamplesEXT(commandBuffer, VK_SAMPLE_COUNT_1_BIT);
        m_core->vkCmdSetSampleMaskEXT(commandBuffer, VK_SAMPLE_COUNT_1_BIT, &sampleMask);
        m_core->vkCmdSetAlphaToCoverageEnableEXT(commandBuffer, VK_FALSE);

        //7. 深度 / 模板
        vkCmdSetDepthTestEnable(commandBuffer, setting.depthTestEnable ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthWriteEnable(commandBuffer, setting.depthWriteEnable ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthCompareOp(commandBuffer, setting.depthCompareOp);
        vkCmdSetStencilTestEnable(commandBuffer, VK_FALSE);

        //8. 颜色混合 / 写掩码
        const VkBool32 blendEnable = setting.blendEnable ? VK_TRUE : VK_FALSE;
        m_core->vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &blendEnable);
        if (setting.blendEnable) {
            m_core->vkCmdSetColorBlendEquationEXT(commandBuffer, 0, 1, &setting.blendEquation);
        }
        m_core->vkCmdSetColorWriteMaskEXT(commandBuffer, 0, 1, &setting.colorWriteMask);
    }

    void GraphicsShaderObject::Destroy() {
        if (m_core != nullptr) {
            for (VkShaderEXT& shader : m_stageShaders) {
                if (shader != VK_NULL_HANDLE) {
                    m_core->vkDestroyShaderEXT(m_core->GetVkDevice(), shader, nullptr);
                    shader = VK_NULL_HANDLE;
                }
            }
        }
        m_core = nullptr;
    }
}
