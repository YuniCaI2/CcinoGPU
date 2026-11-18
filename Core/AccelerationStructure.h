#pragma once

#include <cstdint>
#include <vector>
#include "Core/Buffer.h"
#include "Core/CcinoType.h"
#include "Core/RefCounted.hpp"
#include "RHI/Context.h"

namespace Ccino {
    class BLAS : public RefCounted {
    public:
        virtual ~BLAS() {
            Delete();
        }

        struct CreateArgs {
            RHI::VertexFormat format = RHI::VertexFormat::R32G32B32_SFLOAT;
            Ref<Buffer> vertexBuffer;
            uint32_t vertexOffset = 0;
            uint32_t vertexStride = 0;
            uint32_t vertexCount = 0;
            RHI::IndexType indexType = RHI::IndexType::e32;
            Ref<Buffer> indexBuffer;
            uint32_t indexOffset = 0;
            uint32_t indexCount = 0;
            RHI::GeometryFlags geometryFlags{};
            RHI::ASFlag asFlag = RHI::ASFlag::FAST_TRACE;
        };

        static Ref<BLAS> Create(RHI::Context& context, const CreateArgs& createArgs) {
            auto blas = Ref<BLAS>::Make();
            blas->CreateImpl(context, createArgs);
            return blas;
        }

        void Delete();

        RID GetRHI() const { return m_rhiBLAS; }
        uint64_t GetDeviceAddress() const;
        const CreateArgs& GetCreateArgs() const { return m_createArgs; }

    private:
        friend class Ref<BLAS>;
        BLAS() = default;

        void CreateImpl(RHI::Context& context, const CreateArgs& createArgs);

        RID m_rhiBLAS = RID::Invalid();
        RHI::Context* m_context = nullptr;
        CreateArgs m_createArgs{};
    };

    class TLAS : public RefCounted {
    public:
        virtual ~TLAS() {
            Delete();
        }

        struct InstanceInfo {
            Vec3 position{};
            Ref<BLAS> blas;
            uint32_t customIndex = 0;
            bool bCulling = false;
        };

        struct CreateArgs {
            std::vector<InstanceInfo> instances;
            RHI::ASFlag asFlag = RHI::ASFlag::FAST_TRACE;
        };

        static Ref<TLAS> Create(RHI::Context& context, const CreateArgs& createArgs) {
            auto tlas = Ref<TLAS>::Make();
            tlas->CreateImpl(context, createArgs);
            return tlas;
        }

        void Delete();

        RID GetRHI() const { return m_rhiTLAS; }
        uint64_t GetDeviceAddress() const;
        const CreateArgs& GetCreateArgs() const { return m_createArgs; }

    private:
        friend class Ref<TLAS>;
        TLAS() = default;

        void CreateImpl(RHI::Context& context, const CreateArgs& createArgs);

        RID m_rhiTLAS = RID::Invalid();
        RHI::Context* m_context = nullptr;
        CreateArgs m_createArgs{};
    };
}
