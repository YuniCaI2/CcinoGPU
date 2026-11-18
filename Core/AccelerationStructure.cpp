#include "Core/AccelerationStructure.h"
#include <stdexcept>

namespace Ccino {
    void BLAS::CreateImpl(RHI::Context& context, const CreateArgs& createArgs) {
        if (!createArgs.vertexBuffer || createArgs.vertexBuffer->GetRHI() == RID::Invalid()) {
            throw std::invalid_argument("[Core BLAS]: vertexBuffer is invalid");
        }
        if (!createArgs.indexBuffer || createArgs.indexBuffer->GetRHI() == RID::Invalid()) {
            throw std::invalid_argument("[Core BLAS]: indexBuffer is invalid");
        }

        m_context = &context;
        m_createArgs = createArgs;

        RHI::ASGeometryInfo geoInfo{};
        geoInfo.format = createArgs.format;
        geoInfo.vertexBuffer = createArgs.vertexBuffer->GetRHI();
        geoInfo.vertexOffset = createArgs.vertexOffset;
        geoInfo.vertexStride = createArgs.vertexStride;
        geoInfo.vertexCount = createArgs.vertexCount;
        geoInfo.indexType = createArgs.indexType;
        geoInfo.indexBuffer = createArgs.indexBuffer->GetRHI();
        geoInfo.indexOffset = createArgs.indexOffset;
        geoInfo.indexCount = createArgs.indexCount;
        geoInfo.flags = createArgs.geometryFlags;

        m_rhiBLAS = context.CreateBLAS(geoInfo, createArgs.asFlag);
    }

    void BLAS::Delete() {
        if (m_rhiBLAS == RID::Invalid()) {
            return;
        }
        m_context->DeleteResource(m_rhiBLAS);
        m_rhiBLAS = RID::Invalid();
        m_createArgs = {};
    }

    uint64_t BLAS::GetDeviceAddress() const {
        if (m_context == nullptr || m_rhiBLAS == RID::Invalid()) {
            throw std::runtime_error("[Core BLAS]: BLAS is invalid");
        }
        return m_context->GetASAddress(m_rhiBLAS);
    }

    void TLAS::CreateImpl(RHI::Context& context, const CreateArgs& createArgs) {
        m_context = &context;
        m_createArgs = createArgs;

        std::vector<RHI::ASInstanceInfo> rhiInstances;
        rhiInstances.reserve(createArgs.instances.size());
        for (const InstanceInfo& instance : createArgs.instances) {
            if (!instance.blas || instance.blas->GetRHI() == RID::Invalid()) {
                throw std::invalid_argument("[Core TLAS]: instance blas is invalid");
            }
            RHI::ASInstanceInfo rhiInstance{};
            rhiInstance.position = instance.position;
            rhiInstance.blas = instance.blas->GetRHI();
            rhiInstance.customIndex = instance.customIndex;
            rhiInstance.bCulling = instance.bCulling;
            rhiInstances.push_back(rhiInstance);
        }

        m_rhiTLAS = context.CreateTLAS(rhiInstances, createArgs.asFlag);
    }

    void TLAS::Delete() {
        if (m_rhiTLAS == RID::Invalid()) {
            return;
        }
        m_context->DeleteResource(m_rhiTLAS);
        m_rhiTLAS = RID::Invalid();
        m_createArgs = {};
    }

    uint64_t TLAS::GetDeviceAddress() const {
        if (m_context == nullptr || m_rhiTLAS == RID::Invalid()) {
            throw std::runtime_error("[Core TLAS]: TLAS is invalid");
        }
        return m_context->GetASAddress(m_rhiTLAS);
    }
}
