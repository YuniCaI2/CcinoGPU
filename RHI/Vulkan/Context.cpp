#include "RHI/Vulkan/Context.h"

namespace Ccino::RHI::Vulkan {
    void Context::Init(const RHISettings& settings) {
        m_core.Init(settings);
    }


    void Context::Destroy() {
        m_core.Destroy();
    }
}