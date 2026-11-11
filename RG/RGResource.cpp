#include "RGResource.h"

#include <memory>
#include <stdexcept>
#include <utility>

namespace Ccino {
    IRGResource::~IRGResource() = default;

    RGResource::RGResource(std::unique_ptr<IRGResource> handle, bool external)
        : m_handle(std::move(handle)), isExternal(external) {}

    RGResource::RGResource(ResourceType type, Buffer::CreateArgs createArgs, bool external)
        : m_handle(std::make_unique<RGResourceHandle<Buffer>>(std::move(createArgs), Ref<Buffer>{})),
          isExternal(external) {
        if (type != ResourceType::Buffer) {
            throw std::invalid_argument("RGResource type does not match Buffer create args");
        }
    }

    RGResource::RGResource(ResourceType type, Texture2D::CreateArgs createArgs, bool external)
        : m_handle(std::make_unique<RGResourceHandle<Texture2D>>(std::move(createArgs), Ref<Texture2D>{})),
          isExternal(external) {
        if (type != ResourceType::Texture) {
            throw std::invalid_argument("RGResource type does not match Texture2D create args");
        }
    }

    RGResource RGResource::TransientBuffer(Buffer::CreateArgs createArgs) {
        return RGResource{std::make_unique<RGResourceHandle<Buffer>>(std::move(createArgs), Ref<Buffer>{}), false};
    }

    RGResource RGResource::ExternalBuffer(const Ref<Buffer>& buffer) {
        if (!buffer.IsValid()) {
            throw std::invalid_argument("External buffer is null");
        }
        return RGResource{std::make_unique<RGResourceHandle<Buffer>>(buffer->GetCreateArgs(), buffer), true};
    }

    RGResource RGResource::TransientTexture2D(Texture2D::CreateArgs createArgs) {
        return RGResource{std::make_unique<RGResourceHandle<Texture2D>>(std::move(createArgs), Ref<Texture2D>{}), false};
    }

    RGResource RGResource::ExternalTexture2D(const Ref<Texture2D>& texture) {
        if (!texture.IsValid()) {
            throw std::invalid_argument("External texture is null");
        }
        return RGResource{std::make_unique<RGResourceHandle<Texture2D>>(texture->GetCreateArgs(), texture), true};
    }

    ResourceType RGResource::GetResourceType() const {
        if (m_handle == nullptr) {
            throw std::runtime_error("RGResource has no handle");
        }
        return m_handle->Type();
    }

    RID RGResource::GetRHI() const {
        return m_handle != nullptr ? m_handle->GetRHI() : RID::Invalid();
    }

    void RGResource::Create(RHI::Context& context) {
        if (m_handle != nullptr) {
            m_handle->Create(context);
        }
    }

    void RGResource::Release() {
        if (m_handle != nullptr) {
            m_handle->Release();
        }
    }

    size_t RGResource::GetHash() const {
        return m_handle->GetCreateArgsHash();
    }

    std::unique_ptr<IRGResource> RGResource::MoveOutHandle() {
        std::unique_ptr<IRGResource> result{};
        std::swap(result, m_handle);
        return result;
    }

    std::unique_ptr<IRGResource> RGTransientResourceReleasePool::GetResource(const size_t& hash) {
        std::unique_ptr<IRGResource> result{};
        if (m_resources.contains(hash)) {
            m_resources[hash]->Release(); //资源释放
            std::swap(result, m_resources[hash]);
        }
        return result;
    }

    void RGTransientResourceReleasePool::InputResource(std::unique_ptr<IRGResource>&& resource) {
        auto hash = resource->GetCreateArgsHash();
        if (m_resources.contains(hash)) {
            m_resources[hash]->Release(); //资源释放
            std::swap(resource, m_resources[hash]);
        }
    }

    void RGTransientResourceReleasePool::ReleaseAll() {
        for (auto& [_, r] : m_resources){
            r->Release();
        }
        m_resources.clear();
    }

}
