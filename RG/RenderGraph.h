#pragma once
#include "Accessor.h"
#include "Pass.h"
#include "RGResource.h"
#include "RHI/RHIStruct.h"
#include "RG/ComputePass.h"
#include "RG/Pass.h"
#include "RefCounted.hpp"


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

    class RGBuilder {
    public:
        void Init(RHI::Context* context);

        template<typename Func>
        requires std::is_invocable_v<Func, RGBuilder&, RID&>
        RGBuilder& AddPass(Func&& func) {
            //处理Accessor 并且处理Builder 内部Shader的翻译和绑定
            auto passRID = m_passes.Add(RGPass{});
            func(*this, passRID);
            return *this;
        }



        //TODO: 定义外部RenderTarget 隐藏RHI RID
        template<typename VS, typename PS>
        void AddVSAndPS(uint32_t vertexCount, RID renderTargetRID, VS&& vs, PS&& ps) {
            //处理VS和PS的翻译和绑定
        }

        template<ThreadGroupSize threadGroupSize, typename CS> //ThreadGraphSize一定要作为编译器的,这样可以被ClangAST看到
        void AddCS(CS&& cs) {
            //这里的CS是一个Dummy，需要在编译时将其翻译成Pipeline 对象和ShaderModule
            //所以这里需要留一个槽位方便后续绑定使用
            //所以这里的CS需要是一个Pipeline 其中有Shader对象并且还有其周边的设置Meta数据
        }

        //依赖创建(外部资源) 分两步，注册为RG的外部资源然后创建依赖
        template<typename BufferResourceType>
        Accessor<BufferResourceType, BufferAccessFlagBits::Read> Read(Ref<Buffer> buffer, RID& passRID);

        template<typename BufferResourceType>
        Accessor<BufferResourceType, BufferAccessFlagBits::Write> Write(Ref<Buffer> buffer, RID& passRID);

        //依赖创建(内部资源)
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

        RID CreateTransientBuffer(typename Buffer::CreateArgs createArgs);
        RID CreateExternalBuffer(const Ref<Buffer>& buffer);

    private:
        //使用擦除类型句柄管理
        TRIDArray<RGPass> m_passes;
        TRIDArray<RGResource> m_resources;

        //线程不安全，考虑是否加锁
        std::vector<RGEdge> m_edges;  //当前的Edge可能对应多个资源
        std::unordered_map<RID, uint32_t> m_edgeNums; 

        std::vector<std::vector<RID>> m_timeline;

        RHI::Context* m_context;
    };
}