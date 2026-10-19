#pragma once
#include "RefCounted.hpp"
#include <concepts>
#include "RHI/Context.h"

namespace Ccino {
template<class T>
concept IsCcinoResource =
    std::derived_from<T, RefCounted> &&
    requires(RHI::Context& context,
             const typename T::CreateArgs& createArgs) {
        {
            T::Create(context, createArgs)
        } -> std::same_as<Ref<T>>;
    };
}