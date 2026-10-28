#pragma once
#include "Accessor.h"
#include "AccessorEnums.h"
#include "ShaderStruct/ShaderIR.h"
#include "RGResource.h"
#include "RHI/RHIStruct.h"
#include "RG/Pass.h"
#include "RG/Viewer.h"
#include "Core/ThreadPoo.h"
#include "RefCounted.hpp"
#include "ShaderStruct/ShaderCommon.h"
#include <concepts>
#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_map>


namespace Ccino {

    //单条,当前的pass如何处理这个资源
    struct RGEdge {
        RID resourceRID;
        RID passRID;
        uint32_t version;
        ResourceAccessFlags resourceAccessFlags;   
    };

    struct RGPassDependency {
        RID srcPassRID;
        RID dstPassRID;
        RID resourceRID;
        uint32_t dstResourceVersion; //这里表示在dstPass视角下这个资源的Verison是
        ResourceAccessFlags srcAccessFlags;
        ResourceAccessFlags dstAccessFlags;
    };

    //Buffer和Image需要分别处理
    struct RGBufferBarrier {
        BufferAccessFlags srcFlags;
        BufferAccessFlags dstFlags;
        RGPassTypeFlags srcPassType;
        RGPassTypeFlags dstPassType;
        RID resourceRID;
    };

    //TODO:完善TextureBarrier这个结构体，其中还缺少很多对象
    struct RGTextureBarrier {
        TextureAccessFlags srcFlags;
        TextureAccessFlags dstFlags;
    };

    class RGBuilder {
    public:
        void Init(RHI::Context* context);
        void Destroy();

        template<FixedString passName, typename Func>
        requires std::is_invocable_v<Func, RGBuilder&, RID&>
        RGBuilder& AddPass(Func&& func) {
            //处理Accessor 并且处理Builder 内部Shader的翻译和绑定
            RGPass pass;
            pass.passName = passName.Str();
            auto passRID = m_passes.Add(std::move(pass));
            m_currentPass = passRID;
            func(*this, passRID);
            return *this;
        }



        //TODO: 定义外部RenderTarget 隐藏RHI RID
        template<typename VS, typename PS>
        void AddVSAndPS(uint32_t vertexCount, RID renderTargetRID, VS&& vs, PS&& ps) {
            //处理VS和PS的翻译和绑定
        }

        template<ThreadGroupSize threadGroupSize, typename CS> //ThreadGraphSize一定要作为编译器的,这样可以被ClangAST看到
        requires std::invocable<CS>
        void AddCS(CS&& cs, const WorkGroupSize& workGroupSize) {
            //这里的CS是一个Dummy，需要在编译时将其翻译成Pipeline 对象和ShaderModule
            //所以这里需要留一个槽位方便后续绑定使用
            //所以这里的CS需要是一个Pipeline 其中有Shader对象并且还有其周边的设置Meta数据
            //TODO:这里会在每帧调用不确定性能是否可靠！
            auto pass = m_passes.Get(m_currentPass);
            pass->passType = RGPassTypeFlagBits::Compute;
            pass->workGroupSize = workGroupSize; //记录录制Info
            //这里动态的生产管线和shaderIR的绑定

            //不太对，因为每帧都会调用这个代码，所以这里有两个选择： 1. 统一注册 2. 短路
            //此处直接短路即可
            if (m_context != nullptr && ShaderIRRegistry::Instance().Contains(pass->passName)) {
                auto& shaderIR = ShaderIRRegistry::Instance()[pass->passName];
                if (pass->m_pipleineRID == RID::Invalid()) {
                    pass->m_pipleineRID = m_context->CreateComputePipeline(pass->passName, reinterpret_cast<char*>(shaderIR.glsl.data()), shaderIR.glsl.size());
                }
                if (pass->m_descriptorSlotRID == RID::Invalid()) {
                    pass->m_descriptorSlotRID = m_context->CreateDescriptorSlotBinding(shaderIR.descriptorSlotLayout);
                }
            }

            using LambdaType = std::remove_cvref_t<CS>;
            pass->lambda.resize(sizeof(LambdaType));
            std::memcpy(pass->lambda.data(), &cs, sizeof(LambdaType)); //持久映射这块内存,并且这里默认Lambda存储的对象都不需要严格调用析构
        }


        template<typename BufferResourceType>
        Accessor<BufferResourceType, BufferAccessFlagBits::Read> Read(RID& resource, RID& passRID) {
            if (!CheckResourceType(resource, ResourceType::Buffer, "Read")) {
                RID invalid = RID::Invalid();
                return Accessor<BufferResourceType, BufferAccessFlagBits::Read>(invalid);
            }
            uint32_t version = m_edgeNums[resource]; //Read不会改变版本，RWR所读的资源都一致
            m_edges.push_back(RGEdge{resource, passRID, version, BufferAccessFlagBits::Read});
            return Accessor<BufferResourceType, BufferAccessFlagBits::Read>(resource);
        }

        template<typename BufferResourceType>
        Accessor<BufferResourceType, BufferAccessFlagBits::Write> Write(RID& resource, RID& passRID) {
            if (!CheckResourceType(resource, ResourceType::Buffer, "Write")) {
                RID invalid = RID::Invalid();
                return Accessor<BufferResourceType, BufferAccessFlagBits::Write>(invalid);
            }
            uint32_t version = m_edgeNums[resource]++;
            m_edges.push_back(RGEdge{resource, passRID, version, BufferAccessFlagBits::Write});
            return Accessor<BufferResourceType, BufferAccessFlagBits::Write>(resource);
        }

        template<typename BufferResourceType>
        Accessor<BufferResourceType, BufferAccessFlagBits::ReadWrite> ReadWrite(RID& resource, RID& passRID) {
            if (!CheckResourceType(resource, ResourceType::Buffer, "ReadWrite")) {
                RID invalid = RID::Invalid();
                return Accessor<BufferResourceType, BufferAccessFlagBits::ReadWrite>(invalid);
            }
            uint32_t version = m_edgeNums[resource]++;
            m_edges.push_back(RGEdge{resource, passRID, version, BufferAccessFlagBits::ReadWrite});
            return Accessor<BufferResourceType, BufferAccessFlagBits::ReadWrite>(resource);
        }

        //StorageImage 只读访问, 语义同 Read: 不递增版本
        template<ViewType viewType>
        Viewer<TextureAccessFlagBits::StorageRead, viewType> LoadUAV(RID& resource, RID& passRID, ViewRange viewRange = {}) {
            if (!CheckResourceType(resource, ResourceType::Texture, "LoadUAV")) {
                return Viewer<TextureAccessFlagBits::StorageRead, viewType>(RID::Invalid(), viewRange);
            }
            uint32_t version = m_edgeNums[resource];
            m_edges.push_back(RGEdge{resource, passRID, version, TextureAccessFlagBits::StorageRead});
            return Viewer<TextureAccessFlagBits::StorageRead, viewType>(resource, viewRange);
        }

        //StorageImage 只写访问, 语义同 Write: 产生新版本
        template<ViewType viewType>
        Viewer<TextureAccessFlagBits::StorageWrite, viewType> StoreUAV(RID& resource, RID& passRID, ViewRange viewRange = {}) {
            if (!CheckResourceType(resource, ResourceType::Texture, "StoreUAV")) {
                return Viewer<TextureAccessFlagBits::StorageWrite, viewType>(RID::Invalid(), viewRange);
            }
            uint32_t version = m_edgeNums[resource]++;
            m_edges.push_back(RGEdge{resource, passRID, version, TextureAccessFlagBits::StorageWrite});
            return Viewer<TextureAccessFlagBits::StorageWrite, viewType>(resource, viewRange);
        }

        //StorageImage 读写访问, 语义同 ReadWrite: 产生新版本
        template<ViewType viewType>
        Viewer<TextureAccessFlagBits::StorageReadWrite, viewType> LoadStoreUAV(RID& resource, RID& passRID, ViewRange viewRange = {}) {
            if (!CheckResourceType(resource, ResourceType::Texture, "LoadStoreUAV")) {
                return Viewer<TextureAccessFlagBits::StorageReadWrite, viewType>(RID::Invalid(), viewRange);
            }
            uint32_t version = m_edgeNums[resource]++;
            m_edges.push_back(RGEdge{resource, passRID, version, TextureAccessFlagBits::StorageReadWrite});
            return Viewer<TextureAccessFlagBits::StorageReadWrite, viewType>(resource, viewRange);
        }

        //SampledImage 采样访问, 相当于Read: 不递增版本
        template<ViewType viewType>
        Viewer<TextureAccessFlagBits::Sampled, viewType> Sample(RID& resource, RID& passRID, ViewRange viewRange = {}) {
            if (!CheckResourceType(resource, ResourceType::Texture, "Sample")) {
                return Viewer<TextureAccessFlagBits::Sampled, viewType>(RID::Invalid(), viewRange);
            }
            uint32_t version = m_edgeNums[resource];
            m_edges.push_back(RGEdge{resource, passRID, version, TextureAccessFlagBits::Sampled});
            return Viewer<TextureAccessFlagBits::Sampled, viewType>(resource, viewRange);
        }


        //处理Texture依赖
        void BuildGraph();
        void Run(); //这里传入一个分配的池

        //需要实现循环运行，这个和游戏渲染循环一致,并且注入一个lambda用来处理当前是否需要退出
        template<typename Exit>
        requires std::is_same_v<bool, std::invoke_result<std::remove_cvref_t<Exit>>>
        void LoopRun(Exit&& exitFunc) {
            if (exitFunc()) {
                //退出循环
                return;
            }

            //这里的LoopRun就是为了图像服务的，不是纯计算
            auto commandBuffer = m_context->GetCommandBuffer(CommandQueueType::GRAPHICS, m_context->GetCurrentFrameIndex());
            m_context->StartRecording(commandBuffer);
            m_context->EndRecording(commandBuffer);
            
        } 

        RID CreateTransientBuffer(typename Buffer::CreateArgs createArgs);
        RID CreateExternalBuffer(const Ref<Buffer>& buffer);
        RID CreateTransientTexture2D(typename Texture2D::CreateArgs createArgs);
        RID CreateExternalTexture2D(const Ref<Texture2D>& texture);

    private:
        //运行期资源类型检查(热路径, 不抛异常): 类型不符时cerr 报错并返回false, 调用方跳过建边并返回无效句柄
        bool CheckResourceType(RID resourceRID, ResourceType expected, const char* accessName);

        //先不考虑资源的别名和并行录制
        void CreateGPUResource();
        void ReleaseGPUResource();

        void ReleasePass();

        void GenerateCommands(VkCommandBuffer buffer); //现在先单线程处理后续增加多线程

        //使用擦除类型句柄管理
        TRIDArray<RGPass> m_passes;
        TRIDArray<RGResource> m_resources;


        //线程不安全，考虑是否加锁
        RID m_currentPass = RID::Invalid();
        std::vector<RGEdge> m_edges;  //当前的Edge可能对应多个资源
        std::unordered_map<RID, uint32_t> m_edgeNums; 

        std::vector<std::vector<RID>> m_timeline;


        //类型粒度分开
        std::vector<std::vector<RGBufferBarrier>> m_bufferBarriers; //这个和timeline需要对齐 | timeline 0 || barrier 0 || timeline 1 || barrier 1 || timeline2
        std::vector<RGTextureBarrier> m_textureBarriers; 


        RHI::Context* m_context = nullptr;
    };
}