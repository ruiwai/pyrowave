// Copyright (c) 2026 AlkaidLab contributors
// SPDX-License-Identifier: MIT AND GPL-3.0-only
#pragma once

#include <vulkan/vulkan.h>

inline bool pyrowave_has_enabled_float16(const VkDeviceCreateInfo *info) noexcept
{
	if (!info)
		return false;
	for (auto *feature = static_cast<const VkBaseInStructure *>(info->pNext);
	     feature; feature = feature->pNext)
	{
		if (feature->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
			return reinterpret_cast<const VkPhysicalDeviceVulkan12Features *>(feature)->shaderFloat16 != VK_FALSE;
		if (feature->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES)
			return reinterpret_cast<const VkPhysicalDeviceShaderFloat16Int8Features *>(feature)->shaderFloat16 != VK_FALSE;
	}
	return false;
}
