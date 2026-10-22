#pragma once
#include "RG/RenderGraph.h"

namespace Ccino {
    class Context {
    public:
        RGBuilder& GetRGBuilder() {
            return m_rgBuilder;
        }   
        
        void Run();
    private:
        RGBuilder m_rgBuilder;
    };
};