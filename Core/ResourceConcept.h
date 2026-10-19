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

    //这里是强资源类型，不确定是否区分Texture和StorageImage
    enum class ResourceType {
        Buffer,
        Texture,
    };
}