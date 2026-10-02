#pragma once
#include <vulkan/vulkan_core.h>

#define PUSH_CONSTANT_RANGE_SIZE 128

inline VkPushConstantRange CreatePushConstantRange(VkShaderStageFlags stageFlags) {
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = stageFlags;
    pushConstantRange.offset = 0;
    pushConstantRange.size = PUSH_CONSTANT_RANGE_SIZE;
    return pushConstantRange;
}