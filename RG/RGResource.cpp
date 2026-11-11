#include "RGResource.h"

#include <memory>
#include <stdexcept>
#include <unordered_map>
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
        if (m_handle == nullptr) {
            throw std::runtime_error("RGResource has no handle");
        }
        return m_handle->GetCreateArgsHash();
    }

    std::unique_ptr<IRGResource> RGResource::MoveOutHandle() {
        return m_handle != nullptr ? m_handle->TakeResource() : nullptr;
    }

    void RGTransientResourceReleasePool::Init(uint32_t frameCount) {
        // MSVC 的 vector::resize 在元素移动构造非 noexcept 时会实例化拷贝路径。
        // unordered_map 的移动构造没有 noexcept，拷贝又会碰到 unique_ptr。
        m_resources = decltype(m_resources)(frameCount);
        m_frameIndex = 0;
        m_frameCount = frameCount;
    }

    void RGTransientResourceReleasePool::Update() {
        if (m_frameCount == 0) {
            return;
        }
        m_frameIndex = (m_frameIndex + 1) % m_frameCount;
    }

    void RGTransientResourceReleasePool::Destroy() {
        for (auto& resources : m_resources) {
            for (auto& [_, bucket] : resources) {
                for (auto& r : bucket) {
                    if (r) {
                        r->Release();
                    }
                }
            }
            resources.clear();
        }
        m_resources.clear();
        m_frameIndex = 0;
        m_frameCount = 0;
    }

    std::unique_ptr<IRGResource> RGTransientResourceReleasePool::GetResource(const size_t& hash) {
        if (m_resources.empty()) {
            return nullptr;
        }
        auto& framePool = m_resources[m_frameIndex];
        auto it = framePool.find(hash);
        if (it == framePool.end() || it->second.empty()) {
            return nullptr;
        }
        std::unique_ptr<IRGResource> result = std::move(it->second.back());
        it->second.pop_back();
        if (it->second.empty()) {
            framePool.erase(it);
        }
        return result;
    }

    void RGTransientResourceReleasePool::InputResource(std::unique_ptr<IRGResource>&& resource) {
        if (!resource) {
            return;
        }
        if (m_resources.empty()) {
            resource->Release();
            return;
        }
        auto hash = resource->GetCreateArgsHash();
        m_resources[m_frameIndex][hash].push_back(std::move(resource));
    }

    void RGTransientResourceReleasePool::ReleaseAll() {
        if (m_resources.empty()) {
            return;
        }
        for (auto& [_, bucket] : m_resources[m_frameIndex]) {
            for (auto& r : bucket) {
                if (r) {
                    r->Release();
                }
            }
        }
        m_resources[m_frameIndex].clear();
    }

}
