#include "RenderGraph.h"
#include "Accessor.h"
#include "AccessorEnums.h"
#include "Pass.h"
#include "RGResource.h"
#include "RHI/Context.h"
#include "ResourceConcept.h"
#include "ShaderStruct/ShaderCommon.h"
#include "ShaderStruct/UniformData.h"
#include "ThirdPart/SDL/src/video/khronos/vulkan/vulkan_core.h"
#include "ThreadPoo.h"
#include "Viewer.h"
#include <algorithm>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <iostream>
namespace {
    VkAttachmentLoadOp ToVkLoadOp(Ccino::LoadOp loadOp) {
        switch (loadOp) {
        case Ccino::LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
        case Ccino::LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case Ccino::LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
        throw std::invalid_argument("Unknown load op");
    }

    VkAttachmentStoreOp ToVkStoreOp(Ccino::StoreOp storeOp) {
        switch (storeOp) {
        case Ccino::StoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
        case Ccino::StoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        throw std::invalid_argument("Unknown store op");
    }

    VkAccessFlags2 ToVkBufferAccessFlags2(BufferAccessFlags flags) {
        VkAccessFlags2 access = VK_ACCESS_2_NONE;
        if (flags & BufferAccessFlagBits::Read) {
            access |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        }
        if (flags & BufferAccessFlagBits::Write) {
            access |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }
        return access;
    }

    VkPipelineStageFlags2 ToVkPipelineStageFlags2(Ccino::RGPassTypeFlags passTypeFlags) {
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
        if (passTypeFlags & Ccino::RGPassTypeFlagBits::Compute) {
            stage |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }
        if (passTypeFlags & Ccino::RGPassTypeFlagBits::Graphics) {
            stage |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        }
        if (stage == VK_PIPELINE_STAGE_2_NONE) {
            stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
        return stage;
    }

    //Barrier转换
    VkBufferMemoryBarrier2 GenerateVkBufferBarrier(
        BufferAccessFlags srcFlags,
        BufferAccessFlags dstFlags,
        Ccino::RHI::Context* context,
        RID bufferID,
        Ccino::RGPassTypeFlags srcPassType = Ccino::RGPassTypeFlagBits::Compute,
        Ccino::RGPassTypeFlags dstPassType = Ccino::RGPassTypeFlagBits::Compute) {
        VkBufferMemoryBarrier2 bufferBarrier{};
        bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        bufferBarrier.pNext = nullptr;
        bufferBarrier.srcStageMask = ToVkPipelineStageFlags2(srcPassType);
        bufferBarrier.srcAccessMask = ToVkBufferAccessFlags2(srcFlags);
        bufferBarrier.dstStageMask = ToVkPipelineStageFlags2(dstPassType);
        bufferBarrier.dstAccessMask = ToVkBufferAccessFlags2(dstFlags);
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.buffer = context->GetVkBuffer(bufferID);
        bufferBarrier.offset = 0;
        bufferBarrier.size = context->GetBufferSize(bufferID);
        return bufferBarrier;
    }

    VkAccessFlags2 ToVkTextureAccessFlags2(TextureAccessFlags flags) {
        VkAccessFlags2 access = VK_ACCESS_2_NONE;
        if (HasTextureAccess(flags, TextureAccessFlagBits::Sampled)) {
            access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }
        if (HasTextureAccess(flags, TextureAccessFlagBits::StorageRead)) {
            access |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        }
        if (HasTextureAccess(flags, TextureAccessFlagBits::StorageWrite)) {
            access |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }
        if (HasTextureAccess(flags, TextureAccessFlagBits::RenderTarget)) {
            access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        return access;
    }

    VkImageLayout ToVkImageLayout(TextureAccessFlags flags) {
        if (HasTextureAccess(flags, TextureAccessFlagBits::StorageRead) ||
            HasTextureAccess(flags, TextureAccessFlagBits::StorageWrite)) {
            return VK_IMAGE_LAYOUT_GENERAL;
        }
        if (HasTextureAccess(flags, TextureAccessFlagBits::Sampled)) {
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        if (HasTextureAccess(flags, TextureAccessFlagBits::RenderTarget)) {
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        return VK_IMAGE_LAYOUT_GENERAL;
    }

    VkImageAspectFlags ImageAspectFromVkFormat(VkFormat format) {
        if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM) {
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkImageMemoryBarrier2 GenerateVkTextureBarrier(
        TextureAccessFlags srcFlags,
        TextureAccessFlags dstFlags,
        Ccino::RHI::Context* context,
        RID textureID,
        Ccino::RGPassTypeFlags srcPassType = Ccino::RGPassTypeFlagBits::Compute,
        Ccino::RGPassTypeFlags dstPassType = Ccino::RGPassTypeFlagBits::Compute) {
        const VkImageLayout oldLayout = context->GetTextureLayout(textureID);
        const VkImageLayout newLayout = ToVkImageLayout(dstFlags);
        const VkFormat format = context->GetFormat(textureID);

        VkImageMemoryBarrier2 imageBarrier{};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                        ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
                                        : ToVkPipelineStageFlags2(srcPassType);
        imageBarrier.srcAccessMask = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                         ? VK_ACCESS_2_NONE
                                         : ToVkTextureAccessFlags2(srcFlags);
        imageBarrier.dstStageMask = ToVkPipelineStageFlags2(dstPassType);
        imageBarrier.dstAccessMask = ToVkTextureAccessFlags2(dstFlags);
        imageBarrier.oldLayout = oldLayout;
        imageBarrier.newLayout = newLayout;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = context->GetVkImage(textureID);
        imageBarrier.subresourceRange.aspectMask = ImageAspectFromVkFormat(format);
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

        context->SetTextureLayout(textureID, newLayout);
        return imageBarrier;
    }
}

namespace Ccino {
    bool IsWritable(ResourceAccessFlags accessFlags) {
        bool isWritable = false;
        std::visit([&](const auto& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, BufferAccessFlags>) {
                isWritable = static_cast<bool>(arg & BufferAccessFlagBits::Write);
            } else if constexpr (std::is_same_v<T, TextureAccessFlags>) {
                isWritable = HasTextureAccess(arg, TextureAccessFlagBits::StorageWrite) ||
                             HasTextureAccess(arg, TextureAccessFlagBits::RenderTarget);
            }
        }, accessFlags);
        return isWritable;
    }

    bool IsReadable(ResourceAccessFlags accessFlags) {
        bool isReadable = false;
        std::visit([&](const auto& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, BufferAccessFlags>) {
                isReadable = static_cast<bool>(arg & BufferAccessFlagBits::Read);
            } else if constexpr (std::is_same_v<T, TextureAccessFlags>) {
                isReadable = HasTextureAccess(arg, TextureAccessFlagBits::StorageRead) ||
                             HasTextureAccess(arg, TextureAccessFlagBits::Sampled);
            }
        }, accessFlags);
        return isReadable;
    }

    bool RGBuilder::CheckResourceType(RID resourceRID, ResourceType expected, const char* accessName) {
        //热路径检查: 只做一次资源查找和类型比较, 失败不抛异常, 只cerr 记录并让调用方跳过这次访问
        auto* resource = m_resources.Get(resourceRID);
        if (resource == nullptr || resource->m_handle == nullptr) {
            std::cerr << "RGBuilder::" << accessName << ": RG resource not found (index "
                      << resourceRID.index << "), access skipped\n";
            return false;
        }
        const ResourceType actual = resource->GetResourceType();
        if (actual != expected) {
            std::cerr << "RGBuilder::" << accessName << ": resource type mismatch (index "
                      << resourceRID.index << "), expected "
                      << (expected == ResourceType::Buffer ? "Buffer" : "Texture") << " but got "
                      << (actual == ResourceType::Buffer ? "Buffer" : "Texture")
                      << ", access skipped\n";
            return false;
        }
        return true;
    }

    bool RGBuilder::CheckTextureFormat(RID resourceRID, ViewType viewType, const char* accessName) {
        auto* resource = m_resources.Get(resourceRID);
        const auto* createArgs = (resource != nullptr && resource->m_handle != nullptr)
                                     ? resource->GetCreateArgs<Texture2D>()
                                     : nullptr;
        if (createArgs == nullptr) {
            std::cerr << "RGBuilder::" << accessName << ": resource is not a Texture2D (index "
                      << resourceRID.index << "), access skipped\n";
            return false;
        }
        if (TextureFormatToViewType(createArgs->format) != viewType) {
            std::cerr << "RGBuilder::" << accessName << ": texture format does not match ViewType (index "
                      << resourceRID.index << "), access skipped\n";
            return false;
        }
        return true;
    }

    void RGBuilder::Init(RHI::Context* context) {
        m_context = context;
    }

    void RGBuilder::Destroy() {
        ReleaseRenderTargetViews();
        ReleaseGPUResource();
        ReleasePass();
    }

    void RGBuilder::BuildGraph() {

        //TODO: (P2) 将这里的堆分配容器放到栈上
        std::unordered_map<RID, std::vector<RID>>  dependecyGraph;
        std::vector<RGPassDependency> passDependencies;
        std::unordered_map<RID, uint32_t> passInDegrees;

        std::unordered_map<RID, std::unordered_map<uint32_t,std::pair<RID, ResourceAccessFlags>>> producers; //[resourceRID][version] -> passRID, ResourceAccessFlags
        std::unordered_map<RID, std::unordered_map<uint32_t,std::vector<std::pair<RID, ResourceAccessFlags>>>> readers; // [resourceRID][version] -> {passRID, ResourceAccessFlags}

        for (auto& edge : m_edges) {
            if (IsWritable(edge.resourceAccessFlags)) {
                producers[edge.resourceRID][edge.version] = {edge.passRID, edge.resourceAccessFlags};
            } else if (IsReadable(edge.resourceAccessFlags)) {
                readers[edge.resourceRID][edge.version].push_back({edge.passRID, edge.resourceAccessFlags});
            }
        }

        //WAR
        //这里的含义是： 当前的所有的Producer 的资源会对应一个消费者也就是Reader，并且Producer 的PassRID是唯一的也就是writer.first 的PassRID 找到对应的Reader
        for (auto& [resourceRID, versionMap] : producers) {
            for(auto& [version, writer] : versionMap) {
                for (const auto& [readerPassRID, readerAccessFlags] : readers[resourceRID][version + 1]) {
                    passDependencies.push_back(RGPassDependency{writer.first, readerPassRID, resourceRID, version + 1, writer.second, readerAccessFlags});
                }
            }
        }        

        //RAW
        for (auto& [resourceRID, versionMap] : readers) {
            for (auto& [version, passUses] : versionMap) {
                for (const auto& [passRID, accessFlags] : passUses) {
                    if (producers[resourceRID].contains(version)) {
                        const auto& writer = producers[resourceRID][version];
                        passDependencies.push_back(RGPassDependency{passRID, writer.first, resourceRID, version, accessFlags, writer.second});
                    }
                }
            }
        }

        //WRW
        for (auto& [resourceRID, versionMap] : producers) {
            for(auto& [version, writer] : versionMap) {
                if (producers[resourceRID].contains(version + 1)) {
                    const auto& nextWriter = producers[resourceRID][version + 1];
                    passDependencies.push_back(RGPassDependency{writer.first, nextWriter.first, resourceRID, version + 1, writer.second, nextWriter.second});
                }
            }
        }        
        
        //Buffer & Texture Barrier
        std::unordered_map<RID, std::vector<RGBufferBarrier>> bufferBarrierMap; 
        std::unordered_map<RID, std::vector<RGTextureBarrier>> textureBarrierMap;

        // 初始化所有的Pass的入度 
        for (const auto& dependency : passDependencies) {
            if (dependency.srcPassRID != RID::Invalid() && dependency.dstPassRID != RID::Invalid()) {
                dependecyGraph[dependency.srcPassRID].push_back(dependency.dstPassRID);
                passInDegrees[dependency.dstPassRID]++; //入度+1
                //Barrier注入 这里先不做，后续注册, 需要向RHI转换
                auto* resource = m_resources.Get(dependency.resourceRID);
                if (resource != nullptr && resource->GetResourceType() == ResourceType::Buffer) {
                    RGBufferBarrier bufferBarrier;
                    std::visit([&](const auto& resourceFlags){
                        if constexpr (std::is_same_v<BufferAccessFlags, std::decay_t<decltype(resourceFlags)>>) {
                            bufferBarrier.dstFlags = resourceFlags;
                        }
                    }, dependency.dstAccessFlags);
                    
                    std::visit([&](const auto& resourceFlags){
                        if constexpr (std::is_same_v<BufferAccessFlags, std::decay_t<decltype(resourceFlags)>>) {
                            bufferBarrier.srcFlags = resourceFlags;
                        }
                    }, dependency.srcAccessFlags);
                    bufferBarrier.srcPassType = m_passes.Get(dependency.srcPassRID)->passType;
                    bufferBarrier.dstPassType = m_passes.Get(dependency.dstPassRID)->passType;

                    bufferBarrier.resourceRID = dependency.resourceRID;

                    bufferBarrierMap[dependency.dstPassRID].push_back(std::move(bufferBarrier));
                } else if (resource != nullptr && resource->GetResourceType() == ResourceType::Texture) {
                    RGTextureBarrier textureBarrier;
                    std::visit([&](const auto& resourceFlags){
                        if constexpr (std::is_same_v<TextureAccessFlags, std::decay_t<decltype(resourceFlags)>>) {
                            textureBarrier.dstFlags = resourceFlags;
                        }
                    }, dependency.dstAccessFlags);

                    std::visit([&](const auto& resourceFlags){
                        if constexpr (std::is_same_v<TextureAccessFlags, std::decay_t<decltype(resourceFlags)>>) {
                            textureBarrier.srcFlags = resourceFlags;
                        }
                    }, dependency.srcAccessFlags);
                    textureBarrier.srcPassType = m_passes.Get(dependency.srcPassRID)->passType;
                    textureBarrier.dstPassType = m_passes.Get(dependency.dstPassRID)->passType;
                    textureBarrier.resourceRID = dependency.resourceRID;

                    textureBarrierMap[dependency.dstPassRID].push_back(std::move(textureBarrier));
                }

            }
        }


        m_timeline.clear();
        m_bufferBarriers.clear();
        m_textureBarriers.clear();
        std::vector<RID> currentLayer;

        for (const auto& passRID : m_passes.GetAllRIDs()) {
            if (passInDegrees[passRID] == 0) {
                currentLayer.push_back(passRID);
            }
        }

        uint32_t scheduledCount = 0;

        while (!currentLayer.empty()) {
            scheduledCount += static_cast<uint32_t>(currentLayer.size());
            m_timeline.push_back(currentLayer);


            std::vector<RID> nextLayer;
            std::unordered_map<RID, RGBufferBarrier> bufferBarrierLayer;
            std::unordered_map<RID, RGTextureBarrier> textureBarrierLayer;

            for (const auto& passRID : currentLayer) {
                for (const auto& dstPassRID : dependecyGraph[passRID]) {
                    passInDegrees[dstPassRID]--;
                    if (passInDegrees[dstPassRID] == 0) {
                        for (auto& bufferBarrier : bufferBarrierMap[dstPassRID]) {
                            if (bufferBarrierLayer.contains(bufferBarrier.resourceRID)) {
                                bufferBarrierLayer[bufferBarrier.resourceRID].srcFlags |= bufferBarrier.srcFlags;
                                bufferBarrierLayer[bufferBarrier.resourceRID].dstFlags |= bufferBarrier.dstFlags;
                                bufferBarrierLayer[bufferBarrier.resourceRID].srcPassType |= bufferBarrier.srcPassType;
                                bufferBarrierLayer[bufferBarrier.resourceRID].dstPassType |= bufferBarrier.dstPassType;
                            } else {
                                bufferBarrierLayer[bufferBarrier.resourceRID] = bufferBarrier;
                            }
                        }
                        for (auto& textureBarrier : textureBarrierMap[dstPassRID]) {
                            if (textureBarrierLayer.contains(textureBarrier.resourceRID)) {
                                textureBarrierLayer[textureBarrier.resourceRID].srcFlags |= textureBarrier.srcFlags;
                                textureBarrierLayer[textureBarrier.resourceRID].dstFlags |= textureBarrier.dstFlags;
                                textureBarrierLayer[textureBarrier.resourceRID].srcPassType |= textureBarrier.srcPassType;
                                textureBarrierLayer[textureBarrier.resourceRID].dstPassType |= textureBarrier.dstPassType;
                            } else {
                                textureBarrierLayer[textureBarrier.resourceRID] = textureBarrier;
                            }
                        }
                        nextLayer.push_back(dstPassRID);
                    }
                }
            }

            if (!nextLayer.empty()) {
                std::vector<RGBufferBarrier> currentBufferBarriers;
                for (auto& [_, barrier] : bufferBarrierLayer) {
                    currentBufferBarriers.push_back(std::move(barrier));
                }
                m_bufferBarriers.push_back(std::move(currentBufferBarriers));

                std::vector<RGTextureBarrier> currentTextureBarriers;
                for (auto& [_, barrier] : textureBarrierLayer) {
                    currentTextureBarriers.push_back(std::move(barrier));
                }
                m_textureBarriers.push_back(std::move(currentTextureBarriers));
            }
            currentLayer = std::move(nextLayer);


        }

        if (scheduledCount < m_passes.Size()) {
            for (const auto& [passRID, inDegree] : passInDegrees) {
                if (inDegree > 0) {
                    std::cerr << "RenderGraph build failed, Build a cycle dependency pass: " << passRID.index << std::endl;
                    return;
                }
            }
        }   
    }

    void RGBuilder::Run() {
        BuildGraph();
        CreateGPUResource();
        if (m_context != nullptr && !m_timeline.empty()) {
            m_context->ExecuteCommand([this](RHI::CommandContext::CommandBuffer& commandBuffer) {
                GenerateCommands(commandBuffer.commandBuffer);
            }, CommandQueueType::GRAPHICS);
        }

        //释放
        ReleaseRenderTargetViews();
    }



    RID RGBuilder::CreateTransientBuffer(typename Buffer::CreateArgs createArgs) {
        return m_resources.Add(RGResource::TransientBuffer(std::move(createArgs)));
    }

    RID RGBuilder::CreateExternalBuffer(const Ref<Buffer>& buffer) {
        return m_resources.Add(RGResource::ExternalBuffer(buffer));
    }

    RID RGBuilder::CreateTransientTexture2D(typename Texture2D::CreateArgs createArgs) {
        return m_resources.Add(RGResource::TransientTexture2D(std::move(createArgs)));
    }

    RID RGBuilder::CreateExternalTexture2D(const Ref<Texture2D>& texture) {
        return m_resources.Add(RGResource::ExternalTexture2D(texture));
    }

    void RGBuilder::CreateGPUResource() {
        // 创建资源: 具体类型由 RGResource 内部的多态句柄分发
        if (m_context == nullptr) return;
        m_resources.ForEach([this](RGResource& resource){
            if (resource.isExternal) return;
            resource.Create(*m_context);
        });
    }

    void RGBuilder::ReleaseGPUResource() {
        m_resources.ForEach([](RGResource& resource){
            if (resource.isExternal) return;
            resource.Release();
        });
    }

    void RGBuilder::GenerateCommands(VkCommandBuffer commandBuffer) {
        if (m_timeline.empty()) {
            return;
        }
        //TODO: 不全部绑定
        m_context->BindDescriptorSet(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
        m_context->BindDescriptorSet(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS);

        //处理时间线，然后生成对应Barrier和bind
        for (size_t i = 0; i < m_timeline.size(); ++i) {
            std::vector<VkBufferMemoryBarrier2> bufferBarriers;
            std::vector<VkImageMemoryBarrier2> imageBarriers;

            if (i > 0) {
                if ((i - 1) < m_bufferBarriers.size()) {
                    for (auto& accessBarrier : m_bufferBarriers[i - 1]) {
                        auto barrier = GenerateVkBufferBarrier(accessBarrier.srcFlags, accessBarrier.dstFlags, m_context,
                            m_resources.Get(accessBarrier.resourceRID)->GetRHI(), accessBarrier.srcPassType, accessBarrier.dstPassType);
                        bufferBarriers.push_back(std::move(barrier));
                    }
                }
                if ((i - 1) < m_textureBarriers.size()) {
                    for (auto& accessBarrier : m_textureBarriers[i - 1]) {
                        auto barrier = GenerateVkTextureBarrier(accessBarrier.srcFlags, accessBarrier.dstFlags, m_context,
                            m_resources.Get(accessBarrier.resourceRID)->GetRHI(), accessBarrier.srcPassType, accessBarrier.dstPassType);
                        imageBarriers.push_back(std::move(barrier));
                    }
                }
            }

            // 首次使用的 Texture 若仍处于 VK_IMAGE_LAYOUT_UNDEFINED，在本层执行前转换至目标 Layout
            for (const auto& passRID : m_timeline[i]) {
                auto* pass = m_passes.Get(passRID);
                for (const auto& edge : m_edges) {
                    if (edge.passRID != passRID) continue;
                    auto* res = m_resources.Get(edge.resourceRID);
                    if (!res || res->GetResourceType() != ResourceType::Texture) continue;
                    RID rhiTex = res->GetRHI();
                    if (m_context->GetTextureLayout(rhiTex) == VK_IMAGE_LAYOUT_UNDEFINED) {
                        std::visit([&](const auto& flags) {
                            if constexpr (std::is_same_v<TextureAccessFlags, std::decay_t<decltype(flags)>>) {
                                imageBarriers.push_back(GenerateVkTextureBarrier(
                                    TextureAccessFlagBits::None, flags, m_context, rhiTex, pass->passType, pass->passType));
                            }
                        }, edge.resourceAccessFlags);
                    }
                }
            }

            if (!bufferBarriers.empty() || !imageBarriers.empty()) {
                VkDependencyInfo dependecyInfo = {};
                dependecyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dependecyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
                dependecyInfo.pBufferMemoryBarriers = bufferBarriers.data();
                dependecyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
                dependecyInfo.pImageMemoryBarriers = imageBarriers.data();
                vkCmdPipelineBarrier2(commandBuffer, &dependecyInfo);
            }

            for (auto& passRID : m_timeline[i]) {
                auto pass = m_passes.Get(passRID);
                //资源映射
                auto& shaderIR = ShaderIRRegistry::Instance()[pass->passName];
                auto descriptorSlot = m_context->GetDescriptorSlotBinding(pass->m_descriptorSlotRID);

                //偏移映射: 按 ShaderIR 记录的偏移, 从 Lambda 闭包里取出捕获的资源和常量
                auto bindUniforms = [&](const std::vector<ShaderUniformDataBinding>& bindings, std::vector<char>& lambdaData) {
                    for (auto& data : bindings) {
                        char* field = lambdaData.data() + data.offset;
                        if (data.uniformDataType == UniformDataType::Buffer) {
                            //直接得到Accessor第一个元素 rid , lambda实例化后相当于是一个struct
                            auto* rid = reinterpret_cast<RID*>(field);
                            RID rhiBufferRID = m_resources.Get(*rid)->GetRHI();
                            descriptorSlot->BindBuffer(data.name, rhiBufferRID);
                        } else if (data.uniformDataType == UniformDataType::SampledImage) {
                            auto* rid = reinterpret_cast<RID*>(field);
                            auto* viewRange = reinterpret_cast<ViewRange*>(field + sizeof(RID)); // RID中有3个uint64的对象，能保证对齐
                            RID rhiTextureRID = m_resources.Get(*rid)->GetRHI();
                            descriptorSlot->BindTexture2DSRV(data.name, rhiTextureRID, *viewRange);
                        } else if (data.uniformDataType == UniformDataType::StorageImage) {
                            auto* rid = reinterpret_cast<RID*>(field);
                            auto* viewRange = reinterpret_cast<ViewRange*>(field + sizeof(RID)); // RID中有3个uint64的对象，能保证对齐
                            RID rhiTextureRID = m_resources.Get(*rid)->GetRHI();
                            descriptorSlot->BindTexture2DUAV(data.name, rhiTextureRID, *viewRange);
                        } else {
                            descriptorSlot->BindParam(data.name, data.uniformDataType, field);
                        }
                    }
                };

                //管线绑定与派发
                if (pass->passType == RGPassTypeFlagBits::Graphics) {
                    const bool hasInvalidTarget = std::any_of(pass->renderTargets.begin(), pass->renderTargets.end(),
                        [this](const RenderTarget& renderTarget) { return m_resources.Get(renderTarget.m_resource) == nullptr; });
                    if (hasInvalidTarget || pass->renderTargets.empty()) {
                        std::cerr << "RGBuilder: graphics pass " << pass->passName << " has no valid render target, draw skipped\n";
                        continue;
                    }

                    bindUniforms(shaderIR.vsUniformDataBindings, pass->vsLambda);
                    bindUniforms(shaderIR.fsUniformDataBindings, pass->fsLambda);

                    RHI::RenderDesc renderDesc = {};
                    renderDesc.colorAttachments.resize(pass->renderTargets.size());
                    for (size_t i = 0; i < pass->renderTargets.size(); i++) {
                        const RenderTarget& renderTarget = pass->renderTargets[i];
                        auto& attachment = renderDesc.colorAttachments[i];
                        attachment.textureView = CreateRenderTargetView(m_resources.Get(renderTarget.m_resource)->GetRHI(), renderTarget.m_viewRange);
                        attachment.loadOp = ToVkLoadOp(renderTarget.m_loadOp);
                        attachment.storeOp = ToVkStoreOp(renderTarget.m_storeOp);
                        std::copy_n(renderTarget.m_clear.color.begin(), 4, attachment.clearValue.color.float32);
                    }

                    const VkExtent2D extent = m_context->BeginRendering(commandBuffer, renderDesc);
                    RHI::ShaderObjectBindingSetting bindingSetting = ToShaderObjectBindingSetting(pass->graphicsSetting);
                    bindingSetting.extent = extent;
                    auto shaderObject = m_context->GetGraphicsShaderObject(pass->m_pipleineRID);
                    shaderObject->Bind(commandBuffer, bindingSetting);
                    descriptorSlot->Push(commandBuffer);
                    //TODO:更加灵活的派发，可以将这些配置放到Setting
                    vkCmdDraw(commandBuffer, pass->graphicsSetting.drawCounts, 1, 0, 0);
                    m_context->EndRendering(commandBuffer);
                } else if (pass->passType == RGPassTypeFlagBits::Compute) {
                    bindUniforms(shaderIR.csUniformDataBindings, pass->lambda);
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_context->GetComputePipeline(pass->m_pipleineRID));
                    descriptorSlot->Push(commandBuffer);
                    vkCmdDispatch(commandBuffer, pass->workGroupSize.x, pass->workGroupSize.y, pass->workGroupSize.z);
                }
            }
        }
    }

    RID RGBuilder::CreateRenderTargetView(const RID& renderTargetRHIRID, const ViewRange& viewRange) {
        if (m_context == nullptr) {
            return RID::Invalid();
        }
        //附件 View 只能覆盖一个 mip, 单层渲染也只覆盖一个 layer
        ViewRange attachmentRange = viewRange;
        attachmentRange.mipMapCounts = 1;
        attachmentRange.arrayCounts = 1;

        RID view = m_context->CreateTextureView(renderTargetRHIRID, attachmentRange);
        m_renderTargetViews.push_back(view);
        return view;
    }

    void RGBuilder::ReleaseRenderTargetViews() {
        if (m_context != nullptr) {
            for (const RID& view : m_renderTargetViews) {
                m_context->DeleteResource(view);
            }
        }
        m_renderTargetViews.clear();
    }

    void RGBuilder::ReleasePass() {
        m_passes.ForEach([this](RGPass& pass){
            pass.lambda.clear();
            if (m_context != nullptr) {
                if (pass.m_pipleineRID != RID::Invalid()) {
                    m_context->DeleteResource(pass.m_pipleineRID);
                    pass.m_pipleineRID = RID::Invalid();
                }
                if (pass.m_descriptorSlotRID != RID::Invalid()) {
                    m_context->DeleteResource(pass.m_descriptorSlotRID);
                    pass.m_descriptorSlotRID = RID::Invalid();
                }
            }
        });
    }


}