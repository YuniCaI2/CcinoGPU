#pragma once
#include <array>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace Ccino::RHI {
    class Core;
    //动态配置方便后续动态调整: 每次 Bind 时录制到 CommandBuffer, 不参与 Shader 编译, 可以每帧修改
    struct ShaderObjectBindingSetting {
        VkExtent2D extent{};                                                //渲染目标尺寸, 用于 Viewport / Scissor, 必须非零
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;                   //VK_POLYGON_MODE_LINE (线框) 需要 fillModeNonSolid
        VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        bool depthTestEnable = false;
        bool depthWriteEnable = false;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        bool blendEnable = false;                                           //只有为 true 时才会录制 blendEquation
        VkColorBlendEquationEXT blendEquation{VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
                                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD};
        VkColorComponentFlags colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    };

    //这里可能有多个Shader多个Shader可以被link。 link需要VK_SHADER_CREATE_LINK_STAGE_BIT_EXT
    class GraphicsShaderObject {
    public:
        //shaderInfo 只在创建时使用 (pCode / pSetLayouts 等), 调用返回后即可释放
        void Init(Core* core, const std::string& name, const std::vector<VkShaderCreateInfoEXT>& shaderInfo);
        //录制到 CommandBuffer: 绑定图形阶段的 Shader (未使用的阶段解绑) 并设置动态状态, 状态不变时不需要每个 Draw 都调用
        void Bind(VkCommandBuffer commandBuffer, const ShaderObjectBindingSetting& setting) const;

        void Destroy();

    private:
        Core* m_core = nullptr;
        std::string m_name;
        //按图形阶段固定排列 (VS / TCS / TES / GS / FS), 未使用的阶段为 VK_NULL_HANDLE 这里这样做是为了保证Shader的顺序的正确 , 
        //其实这里只支持VS和PS, 本质上GS TCS 和TES都能被CS完美代替
        std::array<VkShaderEXT, 5> m_stageShaders{};
    };
}
