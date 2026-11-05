#pragma once
#include "Flags.h"
#include "RHI/RHIStruct.h"
#include "RHI/GraphicsShaderObject.h"
#include "ShaderStruct/ShaderCommon.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ccino {
    enum class RGPassTypeFlagBits : uint32_t {
        Compute = 1 << 0,
        Graphics = 1 << 1 
    };

    using RGPassTypeFlags = Flags<RGPassTypeFlagBits>;

    //和 RHI::ShaderObjectBindingSetting 对应的部分. 枚举能表达的状态放在这里, 渲染目标尺寸由纹理决定, 绑定时再填
    enum class PrimitiveTopology : uint32_t {
        TriangleList,
        LineList,
        PointList,
    };

    enum class PolygonMode : uint32_t {
        Fill,
        Line, //需要设备开启 fillModeNonSolid
        Point,
    };

    enum class CullMode : uint32_t {
        None,
        Front,
        Back,
    };

    enum class FrontFace : uint32_t {
        CounterClockwise,
        Clockwise,
    };

    enum class CompareOp : uint32_t {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };

    //混合方程收成预设: None 关闭混合, Alpha 是常规透明, Additive 是加法
    enum class BlendMode : uint32_t {
        None,
        Alpha,
        Additive,
    };

    enum class ColorWriteFlagBits : uint32_t {
        R = 1u << 0,
        G = 1u << 1,
        B = 1u << 2,
        A = 1u << 3,
    };
    using ColorWriteFlags = Flags<ColorWriteFlagBits>;

    struct GraphicsSetting {
        uint32_t width {};
        uint32_t height {};
        uint32_t drawCounts{}; //这个控制了顶点的数量
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        PolygonMode polygonMode = PolygonMode::Fill;
        CullMode cullMode = CullMode::None;
        FrontFace frontFace = FrontFace::CounterClockwise;
        bool depthTestEnable = false;
        bool depthWriteEnable = false;
        CompareOp depthCompareOp = CompareOp::LessOrEqual;
        BlendMode blendMode = BlendMode::None;
        ColorWriteFlags colorWriteMask = ColorWriteFlags(ColorWriteFlagBits::R) | ColorWriteFlagBits::G |
                                         ColorWriteFlagBits::B | ColorWriteFlagBits::A;
    };

    //width / height 来自 RenderTarget, 写入 BindingSetting::extent
    inline RHI::ShaderObjectBindingSetting ToShaderObjectBindingSetting(const GraphicsSetting& setting) {
        auto topology = [](PrimitiveTopology value) {
            switch (value) {
            case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            }
            throw std::invalid_argument("Unknown primitive topology");
        };
        auto polygonMode = [](PolygonMode value) {
            switch (value) {
            case PolygonMode::Fill: return VK_POLYGON_MODE_FILL;
            case PolygonMode::Line: return VK_POLYGON_MODE_LINE;
            case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
            }
            throw std::invalid_argument("Unknown polygon mode");
        };
        auto cullMode = [](CullMode value) {
            switch (value) {
            case CullMode::None: return VK_CULL_MODE_NONE;
            case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
            case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
            }
            throw std::invalid_argument("Unknown cull mode");
        };
        auto frontFace = [](FrontFace value) {
            switch (value) {
            case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
            case FrontFace::Clockwise: return VK_FRONT_FACE_CLOCKWISE;
            }
            throw std::invalid_argument("Unknown front face");
        };
        auto compareOp = [](CompareOp value) {
            switch (value) {
            case CompareOp::Never: return VK_COMPARE_OP_NEVER;
            case CompareOp::Less: return VK_COMPARE_OP_LESS;
            case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
            case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
            case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
            case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
            }
            throw std::invalid_argument("Unknown compare op");
        };
        auto colorWriteMask = [](ColorWriteFlags mask) {
            VkColorComponentFlags flags = 0;
            if (mask & ColorWriteFlagBits::R) flags |= VK_COLOR_COMPONENT_R_BIT;
            if (mask & ColorWriteFlagBits::G) flags |= VK_COLOR_COMPONENT_G_BIT;
            if (mask & ColorWriteFlagBits::B) flags |= VK_COLOR_COMPONENT_B_BIT;
            if (mask & ColorWriteFlagBits::A) flags |= VK_COLOR_COMPONENT_A_BIT;
            return flags;
        };

        RHI::ShaderObjectBindingSetting binding{};
        binding.extent = {setting.width, setting.height};
        binding.topology = topology(setting.topology);
        binding.polygonMode = polygonMode(setting.polygonMode);
        binding.cullMode = cullMode(setting.cullMode);
        binding.frontFace = frontFace(setting.frontFace);
        binding.depthTestEnable = setting.depthTestEnable;
        binding.depthWriteEnable = setting.depthWriteEnable;
        binding.depthCompareOp = compareOp(setting.depthCompareOp);
        binding.colorWriteMask = colorWriteMask(setting.colorWriteMask);

        switch (setting.blendMode) {
        case BlendMode::None:
            binding.blendEnable = false;
            break;
        case BlendMode::Alpha:
            binding.blendEnable = true;
            binding.blendEquation = {VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
                                     VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD};
            break;
        case BlendMode::Additive:
            binding.blendEnable = true;
            binding.blendEquation = {VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD,
                                     VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD};
            break;
        }
        return binding;
    }

    struct RGPass {
        //具体执行，这里和普通的RGPass不同的是这里直接消费IR从固定地址.Ccino/RG/{PassName}_IR.json
        std::string passName;
        RGPassTypeFlagBits passType = RGPassTypeFlagBits::Compute;
        RID m_pipleineRID = RID::Invalid();
        RID m_descriptorSlotRID = RID::Invalid();
        // void* lambdaPtr = nullptr; //不使用这个是因为临时的lambda可能会出现悬空
        //Compute
        std::vector<char> lambda; //这里直接实现拷贝 这里指CS的Lambda

        //Graphics
        std::vector<char> vsLambda;
        std::vector<char> fsLambda;

        //依赖的GPU资源表
        std::unordered_map<std::string, RID> resourceMap; //这里记录了Accessor处理的对象
        //Compute Info
        WorkGroupSize workGroupSize;
        //Graphics Info
        GraphicsSetting graphicsSetting{};
        std::vector<RID> renderTargets; //下标对应FS 的 out location
    };
}
