// Copyright (c) 2026 AlkaidLab contributors
// SPDX-License-Identifier: MIT AND GPL-3.0-only
#pragma once

#include <vulkan/vulkan.h>

namespace PyroWave
{
constexpr bool wavelet_format_supported(VkFormatFeatureFlags2 flags, bool write_without_format) noexcept
{
	constexpr VkFormatFeatureFlags2 required =
		VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
		VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
	return write_without_format && (flags & required) == required;
}

constexpr bool native_fp16_supported(bool shader_float16, bool write_without_format,
                                     VkFormatFeatureFlags2 r16_optimal_features) noexcept
{
	return shader_float16 && wavelet_format_supported(r16_optimal_features, write_without_format);
}
}
