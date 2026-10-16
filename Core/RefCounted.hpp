#pragma once

#include <atomic>
#include <concepts>
#include <utility>

namespace Ccino {
    class RefCounted;

    template<typename T>
    class Ref;

    class RefCounted {
    protected:
        RefCounted() = default;
        virtual ~RefCounted() = default;

    public:
        void Reference() {
            m_refCount.fetch_add(1, std::memory_order_relaxed);
        }

    private:
        template<typename T>
        friend class Ref;

        bool UnReference() {
            return m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1;
        }

        int GetRefCount() const {
            return m_refCount.load(std::memory_order_relaxed);
        }

        std::atomic<int> m_refCount{};
    };

    template<typename T>
    class Ref {
    public:
        static_assert(std::derived_from<T, RefCounted>, "T must be derived from RefCounted");

        template<typename... Args>
        static Ref Make(Args&&... args) {
            return Ref(new T(std::forward<Args>(args)...));
        }

        Ref() = default;

        explicit Ref(T* ptr)
            : m_reference(ptr) {
            if (m_reference) {
                m_reference->Reference();
            }
        }

        Ref(const Ref& other)
            : m_reference(other.m_reference) {
            if (m_reference) {
                m_reference->Reference();
            }
        }

        Ref(Ref&& other) noexcept
            : m_reference(other.m_reference) {
            other.m_reference = nullptr;
        }

        //派生类到基类的隐式向上转换(如 Ref<CaITexture2DResource> -> Ref<CaITextureResource>)
        template<typename U>
        requires std::convertible_to<U*, T*>
        Ref(const Ref<U>& other)
            : m_reference(other.Get()) {
            if (m_reference) {
                m_reference->Reference();
            }
        }

        template<typename U>
        requires std::convertible_to<U*, T*>
        Ref(Ref<U>&& other) noexcept
            : m_reference(other.Release()) {}

        Ref& operator=(const Ref& other) {
            if (this != &other) {
                Reset();
                m_reference = other.m_reference;
                if (m_reference) {
                    m_reference->Reference();
                }
            }
            return *this;
        }

        Ref& operator=(Ref&& other) noexcept {
            if (this != &other) {
                Reset();
                m_reference = other.m_reference;
                other.m_reference = nullptr;
            }
            return *this;
        }

        ~Ref() {
            Reset();
        }

        void Reset() {
            UnRef();
        }

        void UnRef() {
            auto* reference = m_reference;
            if (!reference) {
                return;
            }

            m_reference = nullptr;
            if (reference->UnReference()) {
                delete reference;
            }
        }

        T* operator->() const {
            return m_reference;
        }

        T& operator*() const {
            return *m_reference;
        }

        T* Get() const {
            return m_reference;
        }

        //释放所有权而不减少引用计数,用于向上转换的移动构造
        T* Release() {
            auto* reference = m_reference;
            m_reference = nullptr;
            return reference;
        }

        bool IsValid() const {
            return m_reference != nullptr;
        }

        explicit operator bool() const {
            return IsValid();
        }

    private:
        template<typename U>
        friend class Ref;

        T* m_reference = nullptr;
    };
}
