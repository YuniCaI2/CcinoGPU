#pragma once
#include "Core/Buffer.h"
#include "Core/Texture2D.h"
#include "Core/ResourceConcept.h"
#include "RHI/Context.h"

#include <memory>
#include <type_traits>
#include <utility>

namespace Ccino {
    //运行期多态接口: RG 内部只通过这组虚函数访问资源, 不关心具体是 Buffer 还是 Texture2D
    struct IRGResource {
        virtual ~IRGResource();
        virtual ResourceType Type() const = 0;
        virtual RID GetRHI() const = 0;
        virtual void Create(RHI::Context& context) = 0; //已持有对象(外部资源/已创建)时不重复创建
        virtual size_t GetCreateArgsHash() const = 0;
        virtual void Release() = 0;
    };

    namespace Impl {
        template<typename T>
        constexpr ResourceType RGResourceTypeOf() {
            if constexpr (std::is_same_v<T, Buffer>) {
                return ResourceType::Buffer;
            } else if constexpr (std::is_same_v<T, Texture2D>) {
                return ResourceType::Texture;
            } else {
                static_assert(sizeof(T) == 0, "Unsupported RG resource type");
            }
        }
    }

    //具体资源类型的擦除实现: 描述(CreateArgs)和对象(Ref<T>)绑在同一个实现里, 不可能错位
    template<typename T>
    requires IsCcinoResource<T>
    class RGResourceHandle final : public IRGResource {
    public:
        RGResourceHandle(typename T::CreateArgs createArgs, Ref<T> ref)
            : m_createArgs(std::move(createArgs)), m_ref(std::move(ref)) {}

        ResourceType Type() const override { return Impl::RGResourceTypeOf<T>(); }

        RID GetRHI() const override {
            return m_ref.IsValid() ? m_ref->GetRHI() : RID::Invalid();
        }

        void Create(RHI::Context& context) override {
            if (!m_ref.IsValid()) {
                m_ref = T::Create(context, m_createArgs);
            }
        }

        size_t GetCreateArgsHash() const override {
            return std::hash<typename T::CreateArgs>{}(m_createArgs);
        }

        void Release() override { m_ref.UnRef(); }


        const typename T::CreateArgs& GetCreateArgs() const { return m_createArgs; }
        Ref<T> GetRef() const { return m_ref; }

    private:
        typename T::CreateArgs m_createArgs;
        Ref<T> m_ref;
    };

    //类型擦除外壳: TRIDArray 里存的就是这个, 移动语义(unique_ptr), 不可拷贝
    struct RGResource {
        RGResource() = default;
        RGResource(std::unique_ptr<IRGResource> handle, bool external);

        //兼容按 (类型, 描述, 是否外部) 直接构造 transient 资源
        RGResource(ResourceType type, Buffer::CreateArgs createArgs, bool external);
        RGResource(ResourceType type, Texture2D::CreateArgs createArgs, bool external);

        static RGResource TransientBuffer(Buffer::CreateArgs createArgs);
        static RGResource ExternalBuffer(const Ref<Buffer>& buffer);
        static RGResource TransientTexture2D(Texture2D::CreateArgs createArgs);
        static RGResource ExternalTexture2D(const Ref<Texture2D>& texture);

        ResourceType GetResourceType() const;
        RID GetRHI() const;
        void Create(RHI::Context& context);
        void Release();
        size_t GetHash() const;

        //TODO:有RTTI，可能需要优化
        //按具体类型取回句柄(类型不对返回nullptr), 供需要描述/强类型校验的地方用
        template<typename T>
        RGResourceHandle<T>* GetHandle() {
            return dynamic_cast<RGResourceHandle<T>*>(m_handle.get());
        }

        template<typename T>
        const RGResourceHandle<T>* GetHandle() const {
            return dynamic_cast<const RGResourceHandle<T>*>(m_handle.get());
        }

        std::unique_ptr<IRGResource> MoveOutHandle();

        template<typename T>
        const typename T::CreateArgs* GetCreateArgs() const {
            const auto* handle = GetHandle<T>();
            return handle != nullptr ? &handle->GetCreateArgs() : nullptr;
        }

        std::unique_ptr<IRGResource> m_handle;
        bool isExternal = false; //是否是外部资源 如果是外部资源这里的仅仅为占位
    };

    //资源缓冲池，用来管理待释放资源
    //这里的资源池的目的是为了存储即将释放的资源，并且防止其重复的多次的创建，他会保留一个飞行帧周期
    class RGTransientResourceReleasePool {
    public:
        std::unique_ptr<IRGResource> GetResource(const size_t& hash);

        void InputResource(std::unique_ptr<IRGResource>&& resource);

        void ReleaseAll();

    private:
        //这里会存储一个回合
        std::unordered_map<size_t, std::unique_ptr<IRGResource>> m_resources;
    };

}
