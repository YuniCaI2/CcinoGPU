#pragma once
#include "Accessor.h"
#include "AccessorEnums.h"
#include "CodeGenerator/ShaderIR.h"
#include "ComputePass.h"
#include "Pass.h"
#include "RGResource.h"
#include "RHI/RHIStruct.h"
#include "RG/ComputePass.h"
#include "RG/Pass.h"
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
        void Init(RHI::Context* context, const std::vector<ShaderIR>& shaderIRs);

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
        requires std::invocable<CS, ComputeGlobalID>
        void AddCS(CS&& cs, const WorkGroupSize& workGroupSize) {
            //这里的CS是一个Dummy，需要在编译时将其翻译成Pipeline 对象和ShaderModule
            //所以这里需要留一个槽位方便后续绑定使用
            //所以这里的CS需要是一个Pipeline 其中有Shader对象并且还有其周边的设置Meta数据
            //TODO:这里会在每帧调用不确定性能是否可靠！
            auto pass = m_passes.Get(m_currentPass);
            pass->workGroupSize = workGroupSize; //记录录制Info
            using LambdaType = std::remove_cvref_t<CS>;
            pass->lambda.resize(sizeof(LambdaType));
            std::memcpy(pass->lambda, &cs, sizeof(LambdaType)); //持久映射这块内存
        }


        template<typename BufferResourceType>
        Accessor<BufferResourceType, BufferAccessFlagBits::Read> Read(RID& resource, RID& passRID) {
            uint32_t version = m_edgeNums[resource]; //Read不会改变版本，RWR所读的资源都一致
            m_edges.push_back(RGEdge{resource, passRID, version, BufferAccessFlagBits::Read});
            return Accessor<BufferResourceType, BufferAccessFlagBits::Read>(resource);
        }

        template<typename BufferResourceType>
        Accessor<BufferResourceType, BufferAccessFlagBits::Write> Write(RID& resource, RID& passRID) {
            uint32_t version = m_edgeNums[resource]++;
            m_edges.push_back(RGEdge{resource, passRID, version, BufferAccessFlagBits::Write});
            return Accessor<BufferResourceType, BufferAccessFlagBits::Write>(resource);
        }

        template<typename BufferResourceType>
        Accessor<BufferResourceType, BufferAccessFlagBits::ReadWrite> ReadWrite(RID& resource, RID& passRID) {
            uint32_t version = m_edgeNums[resource]++;
            m_edges.push_back(RGEdge{resource, passRID, version, BufferAccessFlagBits::ReadWrite});
            return Accessor<BufferResourceType, BufferAccessFlagBits::ReadWrite>(resource);
        }

        //处理Texture依赖
        void BuildGraph();
        void Run();

        RID CreateTransientBuffer(typename Buffer::CreateArgs createArgs);
        RID CreateExternalBuffer(const Ref<Buffer>& buffer);

    private:
        //先不考虑资源的别名和并行录制
        void CreateGPUResource();
        void ReleaseGPUResource();

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

        //IR 产物
        std::unordered_map<std::string, ShaderIR> m_shaderIRMap; //passName -> ShaderIR 

        RHI::Context* m_context;
    };
}