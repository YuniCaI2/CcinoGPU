#include "ASViewer.h"

namespace Ccino {
    ASViewer::ASViewer(const Ref<TLAS>& tlas)
        : m_resource(tlas.IsValid() ? tlas->GetRHI() : RID::Invalid()) {}
}