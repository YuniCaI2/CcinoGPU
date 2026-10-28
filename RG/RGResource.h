#pragma once
#include "Core/Buffer.h"
#include "Core/Texture2D.h"
#include "Core/ResourceConcept.h"
#include "RHI/Context.h"

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Ccino {
    //运行期多态接口: RG 内部只通过这组虚函数访问资源, 不关心具体是 Buffer 还是 Texture2D
    struct IRGResource {
        virtual ~IRGResource() = default;
        virtual ResourceType Type() const = 0;
        virtual RID GetRHI() const = 0;
        virtual void Create(RHI::Context& context) = 0; //已持有对象(外部资源/已创建)时不重复创建
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
        RGResource(std::unique_ptr<IRGResource> handle, bool external)
            : m_handle(std::move(handle)), isExternal(external) {}

        //兼容按 (类型, 描述, 是否外部) 直接构造 transient 资源
        RGResource(ResourceType type, Buffer::CreateArgs createArgs, bool external)
            : m_handle(std::make_unique<RGResourceHandle<Buffer>>(std::move(createArgs), Ref<Buffer>{})),
              isExternal(external) {
            if (type != ResourceType::Buffer) {
                throw std::invalid_argument("RGResource type does not match Buffer create args");
            }
        }

        RGResource(ResourceType type, Texture2D::CreateArgs createArgs, bool external)
            : m_handle(std::make_unique<RGResourceHandle<Texture2D>>(std::move(createArgs), Ref<Texture2D>{})),
              isExternal(external) {
            if (type != ResourceType::Texture) {
                throw std::invalid_argument("RGResource type does not match Texture2D create args");
            }
        }

        static RGResource TransientBuffer(Buffer::CreateArgs createArgs) {
            return RGResource{std::make_unique<RGResourceHandle<Buffer>>(std::move(createArgs), Ref<Buffer>{}), false};
        }

        static RGResource ExternalBuffer(const Ref<Buffer>& buffer) {
            if (!buffer.IsValid()) {
                throw std::invalid_argument("External buffer is null");
            }
            return RGResource{std::make_unique<RGResourceHandle<Buffer>>(buffer->GetCreateArgs(), buffer), true};
        }

        static RGResource TransientTexture2D(Texture2D::CreateArgs createArgs) {
            return RGResource{std::make_unique<RGResourceHandle<Texture2D>>(std::move(createArgs), Ref<Texture2D>{}), false};
        }

        static RGResource ExternalTexture2D(const Ref<Texture2D>& texture) {
            if (!texture.IsValid()) {
                throw std::invalid_argument("External texture is null");
            }
            return RGResource{std::make_unique<RGResourceHandle<Texture2D>>(texture->GetCreateArgs(), texture), true};
        }

        ResourceType GetResourceType() const {
            if (m_handle == nullptr) {
                throw std::runtime_error("RGResource has no handle");
            }
            return m_handle->Type();
        }

        RID GetRHI() const {
            return m_handle != nullptr ? m_handle->GetRHI() : RID::Invalid();
        }

        void Create(RHI::Context& context) {
            if (m_handle != nullptr) {
                m_handle->Create(context);
            }
        }

        void Release() {
            if (m_handle != nullptr) {
                m_handle->Release();
            }
        }

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

        template<typename T>
        const typename T::CreateArgs* GetCreateArgs() const {
            const auto* handle = GetHandle<T>();
            return handle != nullptr ? &handle->GetCreateArgs() : nullptr;
        }

        std::unique_ptr<IRGResource> m_handle;
        bool isExternal = false; //是否是外部资源 如果是外部资源这里的仅仅为占位
    };

}
