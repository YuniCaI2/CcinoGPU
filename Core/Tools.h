#pragma once
#include "Core/RefCounted.hpp"
namespace Ccino {
    template<typename... Objects>
    requires ((std::is_base_of_v<RefCounted, Objects> && requires(Objects& object) { object.Flush(); }) && ...)
    inline void FlushAll(Ref<Objects>&... objects) {
        (objects.Flush(), ...);
    }
}