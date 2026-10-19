#pragma once
#include <vector>
#include "Core/ResourceConcept.h"
#include "RHI/Context.h"

namespace Ccino {
    template<IsCcinoResource ResourceType>
    struct TransientHandle {
        uint32_t index = 0; //TransientPool中的索引
        uint32_t generation = 0;
        typename ResourceType::CreateArgs createArgs;
    };

    template<IsCcinoResource ResourceType>
    class TransientPool {
    public:
        void Init(RHI::Context* m_context);
        
        TransientHandle<ResourceType> Create(const typename ResourceType::CreateArgs& m_createArgs) {
             
        }

    private:
        RHI::Context* m_context = nullptr;

        std::vector<Ref<ResourceType>> m_resourcePool;
        typename ResourceType::CreateArgs m_createArgs;
    };
}