#pragma once
#include "Core/AccelerationStructure.h"
#include "RG/AccessorEnums.h"

namespace Ccino {
    class RGBuilder;

    class ASViewer {
    public:
        explicit ASViewer(RID resource)
            : m_resource(resource) {}
        ASViewer(const Ref<TLAS>& ref); //这里就是直接绑定

        RID GetResource() const { return m_resource; }

    private:
        friend class RGBuilder;
        RID m_resource = RID::Invalid();
        ASAccessFlags m_accessFlags = ASAccessFlagBits::Read;
    };
}