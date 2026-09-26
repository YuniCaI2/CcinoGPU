#pragma once
#include "RHI/RHI.h"
#include "RHI/Vulkan/Core.h"

namespace Ccino::RHI::Vulkan {
    class Context : public RHI::Context {
    public:
        Context() = default;
        ~Context() = default;

        void Init(const RHISettings& settings) override;
        void Destroy() override;
    private:
        Core m_core;
    };
}