#pragma once
#include "Accessor.h"
#include "AccessorEnums.h"
#include "RHI/Context.h"
#include "ShaderStruct/ShaderIR.h"
#include "RGResource.h"
#include "RHI/RHIStruct.h"
#include "RG/Pass.h"
#include "RG/Viewer.h"
#include "RefCounted.hpp"
#include "ShaderStruct/ShaderCommon.h"
#include "Texture2D.h"
#include <algorithm>
#include <concepts>
#include <cstring>
#include <functional>
#include <future>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>

union SDL_Event;


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

    struct RGTextureBarrier {
        TextureAccessFlags srcFlags;
        TextureAccessFlags dstFlags;
        RGPassTypeFlags srcPassType;
        RGPassTypeFlags dstPassType;
        RID resourceRID;
    };

    // 帧循环上下文: 由 LoopRun 每帧维护并传给各阶段 Hook
    struct FrameContext {
        uint64_t frameIndex = 0;   // 当前帧序号(从 0 开始, 每完成一帧递增)
        uint32_t flightIndex = 0;  // 当前飞行帧槽位索引
        float dt = 0.0f;           // 本帧与上一帧的时间间隔(秒)
        float totalTime = 0.0f;    // LoopRun 启动以来的累计时间(秒)
        uint32_t width = 0;        // 当前交换链像素宽度
        uint32_t height = 0;       // 当前交换链像素高度
        bool shouldExit = false;   // 置为 true 时退出 LoopRun
    };

    // LoopRun 四个阶段的 Hook 回调集合
    struct LoopHooks {
        // Hook 1: 事件回调, 框架每帧轮询 SDL 事件队列并逐条分发; 返回 true 立即退出循环
        std::function<bool(SDL_Event&)> onEvent;
        // Hook 2: 交换链尺寸变化回调, 在交换链重建后、onUpdate 与 RecordPasses 前触发
        std::function<void(uint32_t, uint32_t)> onResize;
        // Hook 3: 帧更新回调, 在每帧录制 Pass 前触发(用于更新被 Pass 捕获的 Uniform 状态)
        std::function<void(FrameContext&)> onUpdate;
        // Hook 4: 帧尾回调, 在每帧 PresentFrame 与 Tick 完成后触发(用于 FPS 统计、回读或帧数限制退出)
        std::function<void(FrameContext&)> onPostFrame;
    };


    class IRGScheduler {
    public:
        virtual ~IRGScheduler() = default;

        template <typename F>
        std::future<void> Enqueue(F&& f) {
            return EnqueueImpl(std::function<void()>(std::forward<F>(f)));
        }

    protected:
        virtual std::future<void> EnqueueImpl(std::function<void()>&& fn) = 0;
    };

    class RGBuilder {
    public:
        void Init(RHI::Context* context, std::unique_ptr<IRGScheduler>&& sheduler);
        void Init(RHI::Context* context);
        void Destroy();

        //func 会被保存下来, LoopRun 每帧重新调用一次来刷新捕获值和依赖边, 所以资源要在func 外面创建
        template<FixedString passName, typename Func>
        requires std::is_invocable_v<Func, RGBuilder&, RID&> && std::copy_constructible<std::remove_cvref_t<Func>>
        RGBuilder& AddPass(Func&& func) {
            //处理Accessor 并且处理Builder 内部Shader的翻译和绑定
            RGPass pass;
            pass.passName = passName.Str();
            auto passRID = m_passes.Add(std::move(pass));
            m_currentPass = passRID;
            func(*this, passRID);
            m_passRecorders.emplace_back(passRID, std::forward<Func>(func)); //这里保存的是外层的Lambda 也就是AddPass的Lambda 其中的数据是引用，所以尺寸也不大
            return *this;
        }



        //这里VS和PS VS的输入类型就是out FS的输入类型就是in, 这里规定如果传值就是说in 传引用就是out. fs的out作为renderTarget的一部分，直接写到RenderTarget中. 两者需要对齐. 处理输入的RenderTarget
        template<typename VS, typename FS>
        void AddVSAndFS(VS&& vs, FS&& fs ,const GraphicsSetting& setting, const std::vector<RenderTarget>& renderTargets) { //RenderTarget 由 RTView 创建, 写依赖已经在 RTView 中建好
            //处理VS和PS的翻译和绑定
            auto pass = m_passes.Get(m_currentPass);
            pass->passType = RGPassTypeFlagBits::Graphics;
            pass->graphicsSetting = setting;
            //此时 transient 纹理还没有创建 RHI 对象, View 在 GenerateCommands 中创建
            pass->renderTargets = renderTargets;

            //此处直接短路即可
            if (m_context != nullptr && ShaderIRRegistry::Instance().Contains(pass->passName)) {
                auto& shaderIR = ShaderIRRegistry::Instance()[pass->passName];
                if (pass->m_pipleineRID == RID::Invalid()) {
                    pass->m_pipleineRID = m_context->CreateGraphicsShaderObject(pass->passName, reinterpret_cast<char*>(shaderIR.vs.data()), shaderIR.vs.size(),
                    reinterpret_cast<char*>(shaderIR.fs.data()), shaderIR.fs.size());
                }
                if (pass->m_descriptorSlotRID == RID::Invalid()) {
                    pass->m_descriptorSlotRID = m_context->CreateDescriptorSlotBinding(shaderIR.descriptorSlotLayout);
                }
            }

            using VSLambda = std::remove_cvref_t<VS>;
            using FSLambda = std::remove_cvref_t<FS>;

            pass->vsLambda.resize(sizeof(VSLambda));
            pass->fsLambda.resize(sizeof(FSLambda));
            std::memcpy(pass->vsLambda.data(), &vs, sizeof(VSLambda)); //持久映射这块内存,并且这里默认Lambda存储的对象都不需要严格调用析构
            std::memcpy(pass->fsLambda.data(), &fs, sizeof(FSLambda));
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
                    pass->m_pipleineRID = m_context->CreateComputePipeline(pass->passName, reinterpret_cast<char*>(shaderIR.cs.data()), shaderIR.cs.size());
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
            if (!CheckResourceType(resource, ResourceType::Texture, "LoadUAV") ||
                !CheckTextureFormat(resource, viewType, "LoadUAV")) {
                return Viewer<TextureAccessFlagBits::StorageRead, viewType>(RID::Invalid(), viewRange);
            }
            uint32_t version = m_edgeNums[resource];
            m_edges.push_back(RGEdge{resource, passRID, version, TextureAccessFlagBits::StorageRead});
            return Viewer<TextureAccessFlagBits::StorageRead, viewType>(resource, viewRange);
        }

        //StorageImage 只写访问, 语义同 Write: 产生新版本
        template<ViewType viewType>
        Viewer<TextureAccessFlagBits::StorageWrite, viewType> StoreUAV(RID& resource, RID& passRID, ViewRange viewRange = {}) {
            if (!CheckResourceType(resource, ResourceType::Texture, "StoreUAV") ||
                !CheckTextureFormat(resource, viewType, "StoreUAV")) {
                return Viewer<TextureAccessFlagBits::StorageWrite, viewType>(RID::Invalid(), viewRange);
            }
            uint32_t version = m_edgeNums[resource]++;
            m_edges.push_back(RGEdge{resource, passRID, version, TextureAccessFlagBits::StorageWrite});
            return Viewer<TextureAccessFlagBits::StorageWrite, viewType>(resource, viewRange);
        }

        //StorageImage 读写访问, 语义同 ReadWrite: 产生新版本
        template<ViewType viewType>
        Viewer<TextureAccessFlagBits::StorageReadWrite, viewType> LoadStoreUAV(RID& resource, RID& passRID, ViewRange viewRange = {}) {
            if (!CheckResourceType(resource, ResourceType::Texture, "LoadStoreUAV") ||
                !CheckTextureFormat(resource, viewType, "LoadStoreUAV")) {
                return Viewer<TextureAccessFlagBits::StorageReadWrite, viewType>(RID::Invalid(), viewRange);
            }
            uint32_t version = m_edgeNums[resource]++;
            m_edges.push_back(RGEdge{resource, passRID, version, TextureAccessFlagBits::StorageReadWrite});
            return Viewer<TextureAccessFlagBits::StorageReadWrite, viewType>(resource, viewRange);
        }

        //SampledImage 采样访问, 相当于Read: 不递增版本
        template<ViewType viewType>
        Viewer<TextureAccessFlagBits::Sampled, viewType> Sample(RID& resource, RID& passRID, ViewRange viewRange = {}) {
            if (!CheckResourceType(resource, ResourceType::Texture, "Sample") ||
                !CheckTextureFormat(resource, viewType, "Sample")) {
                return Viewer<TextureAccessFlagBits::Sampled, viewType>(RID::Invalid(), viewRange);
            }
            uint32_t version = m_edgeNums[resource];
            m_edges.push_back(RGEdge{resource, passRID, version, TextureAccessFlagBits::Sampled});
            return Viewer<TextureAccessFlagBits::Sampled, viewType>(resource, viewRange);
        }

        //RenderTarget 写访问, 产生新版本. clear 只在 loadOp 为 Clear 时生效
        template<ViewType viewType = ViewType::Float>
        RenderTarget RTView(RID& resource, RID& passRID, LoadOp loadOp, ClearColor clear = {},
                            StoreOp storeOp = StoreOp::Store, ViewRange viewRange = {}) {
            if (!CheckResourceType(resource, ResourceType::Texture, "RTView") ||
                !CheckTextureFormat(resource, viewType, "RTView")) {
                return RenderTarget{};
            }
            uint32_t version = m_edgeNums[resource]++;
            m_edges.push_back(RGEdge{resource, passRID, version, TextureAccessFlagBits::RenderTarget});
            return RenderTarget(resource, viewRange, loadOp, storeOp, clear);
        }

        //处理 Texture依赖
        void BuildGraph();
        void Run(); //这里传入一个分配的池

        // Hook 1: 注册单条 SDL 事件回调(框架负责 while(SDL_PollEvent) 循环与默认关窗事件; 回调返回 true 时退出 LoopRun)
        template<typename Func>
        requires std::is_invocable_r_v<bool, Func, SDL_Event&>
        RGBuilder& OnEvent(Func&& func) {
            m_loopHooks.onEvent = std::forward<Func>(func);
            return *this;
        }

        template<typename Func>
        requires std::is_invocable_r_v<bool, Func, SDL_Event&>
        RGBuilder& PollEvent(Func&& func) {
            return OnEvent(std::forward<Func>(func));
        }

        // Hook 2: 注册交换链尺寸变化回调(在交换链重建后、录制 Pass 前触发)
        template<typename Func>
        requires std::is_invocable_v<Func, uint32_t, uint32_t>
        RGBuilder& OnResize(Func&& func) {
            m_loopHooks.onResize = std::forward<Func>(func);
            return *this;
        }

        // Hook 3: 注册每帧录制前更新回调(支持返回 void, 或返回 bool 表示是否退出 LoopRun)
        template<typename Func>
        requires std::is_invocable_v<Func, FrameContext&>
        RGBuilder& OnUpdate(Func&& func) {
            using ReturnType = std::invoke_result_t<Func, FrameContext&>;
            if constexpr (std::is_same_v<ReturnType, bool>) {
                m_loopHooks.onUpdate = [fn = std::forward<Func>(func)](FrameContext& frame) mutable {
                    if (fn(frame)) {
                        frame.shouldExit = true;
                    }
                };
            } else {
                m_loopHooks.onUpdate = std::forward<Func>(func);
            }
            return *this;
        }

        // Hook 4: 注册每帧 PresentFrame 与 Tick 完成后的帧尾回调(支持返回 void, 或返回 bool 表示是否退出 LoopRun)
        template<typename Func>
        requires std::is_invocable_v<Func, FrameContext&>
        RGBuilder& OnPostFrame(Func&& func) {
            using ReturnType = std::invoke_result_t<Func, FrameContext&>;
            if constexpr (std::is_same_v<ReturnType, bool>) {
                m_loopHooks.onPostFrame = [fn = std::forward<Func>(func)](FrameContext& frame) mutable {
                    if (fn(frame)) {
                        frame.shouldExit = true;
                    }
                };
            } else {
                m_loopHooks.onPostFrame = std::forward<Func>(func);
            }
            return *this;
        }

        //按注册或传入的 LoopHooks 运行交换链渲染循环:
        //  1. PollEvents(onEvent)  -> 2. SyncSwapchain(onResize) -> 3. onUpdate
        //  -> 4. PrepareFrame + RecordPasses + BuildGraph + Blit + Present + Tick -> 5. onPostFrame
        void LoopRun(RID presentTexture, const LoopHooks& hooks = {});

        //兼容仅传入无参退出判断 Lambda 的简写形式: exitFunc 每帧录制前调用一次, 返回 true 退出
        template<typename Exit>
        requires std::is_same_v<bool, std::invoke_result_t<Exit&>>
        void LoopRun(RID presentTexture, Exit&& exitFunc) {
            LoopHooks hooks = m_loopHooks;
            auto prevUpdate = std::move(hooks.onUpdate);
            hooks.onUpdate = [prev = std::move(prevUpdate), exit = std::forward<Exit>(exitFunc)](FrameContext& frame) mutable {
                if (exit()) {
                    frame.shouldExit = true;
                    return;
                }
                if (prev) {
                    prev(frame);
                }
            };
            LoopRun(presentTexture, hooks);
        }

        RID CreateTransientBuffer(typename Buffer::CreateArgs createArgs);
        RID CreateExternalBuffer(const Ref<Buffer>& buffer);
        bool UpdateExternalBuffer(RID resourceRID, const Ref<Buffer>& buffer);


        RID CreateTransientTexture2D(typename Texture2D::CreateArgs createArgs);
        RID CreateExternalTexture2D(const Ref<Texture2D>& texture);
        bool UpdateExternalTexture2D(RID resourceRID, const Ref<Texture2D>& texture);

    private:
        //运行期资源类型检查(热路径, 不抛异常): 类型不符时cerr 报错并返回false, 调用方跳过建边并返回无效句柄
        bool CheckResourceType(RID resourceRID, ResourceType expected, const char* accessName);
        bool CheckTextureFormat(RID resourceRID, ViewType viewType, const char* accessName);

        //先不考虑资源的别名和并行录制
        void CreateGPUResource();
        void ReleaseGPUResource();

        void ReleasePass();
        void RecordPasses(); //清空依赖边后按添加顺序重新调用每个Pass 的func

        void GenerateCommands(VkCommandBuffer buffer); //现在先单线程处理后续增加多线程

        RID CreateRenderTargetView(const RID& renderTargetRHIRID, const ViewRange& viewRange);
        //批量释放，帧尾部
        void ReleaseRenderTargetViews();
        //使用擦除类型句柄管理
        TRIDArray<RGPass> m_passes;
        TRIDArray<RGResource> m_resources;
        std::vector<std::pair<RID, std::function<void(RGBuilder&, RID&)>>> m_passRecorders;


        //线程不安全，考虑是否加锁
        RID m_currentPass = RID::Invalid();
        std::vector<RGEdge> m_edges;  //当前的Edge可能对应多个资源
        std::unordered_map<RID, uint32_t> m_edgeNums;

        std::vector<std::vector<RID>> m_timeline;


        //类型粒度分开
        std::vector<std::vector<RGBufferBarrier>> m_bufferBarriers; //这个和timeline需要对齐 | timeline 0 || barrier 0 || timeline 1 || barrier 1 || timeline2
        std::vector<std::vector<RGTextureBarrier>> m_textureBarriers;

        //RenderTarget由RGBuilder管理View
        std::vector<RID> m_renderTargetViews;

        RHI::Context* m_context = nullptr;
        LoopHooks m_loopHooks;

        //调度器
        std::unique_ptr<IRGScheduler> m_scheduler;
    };
}
