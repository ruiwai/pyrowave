// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT
#include "pyrowave_common.hpp"
#include "pyrowave_features.hpp"

namespace PyroWave
{
using namespace Vulkan;

void WaveletBuffers::init_samplers()
{
	SamplerCreateInfo samp = {};
	samp.address_mode_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samp.address_mode_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samp.address_mode_w = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samp.min_filter = VK_FILTER_NEAREST;
	samp.mag_filter = VK_FILTER_NEAREST;
	samp.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	mirror_repeat_sampler = device->create_sampler(samp);

	samp.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samp.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samp.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samp.border_color = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	border_sampler = device->create_sampler(samp);
}

bool WaveletBuffers::allocate_images()
{
	auto info = ImageCreateInfo::immutable_2d_image(
			aligned_width / 2, aligned_height / 2,
			VK_FORMAT_R16_SFLOAT);
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.layers = NumFrequencyBandsPerLevel * NumComponents;
	info.layout = ImageLayout::General;
	info.levels = DecompositionLevels;

	wavelet_img_high_res = device->create_image(info);
	if (!wavelet_img_high_res)
		return false;
	device->set_name(*wavelet_img_high_res, "wavelet-buffer-high-res");

	for (int level = 0; level < DecompositionLevels; level++)
	{
		ImageViewCreateInfo view_info = {};
		view_info.levels = 1;
		view_info.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

		view_info.base_level = level;
		view_info.image = wavelet_img_high_res.get();

		for (int component = 0; component < NumComponents; component++)
		{
			view_info.base_layer = 4 * component;

			view_info.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			view_info.layers = 4;
			component_layer_views[component][level] = device->create_image_view(view_info);
			if (!component_layer_views[component][level])
				return false;

			view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;
			view_info.layers = 1;
			component_ll_views[component][level] = device->create_image_view(view_info);
			if (!component_ll_views[component][level])
				return false;
		}
	}
	return true;
}

void WaveletBuffers::accumulate_block_mapping(int blocks_x_8x8, int blocks_y_8x8)
{
	int blocks_x_32x32 = (blocks_x_8x8 + 3) / 4;
	int blocks_y_32x32 = (blocks_y_8x8 + 3) / 4;

	for (int y = 0; y < blocks_y_32x32; y++)
	{
		for (int x = 0; x < blocks_x_32x32; x++)
		{
			BlockMapping mapping = {};
			mapping.block_offset_8x8 = block_count_8x8 + 4 * y * blocks_x_8x8 + 4 * x;
			mapping.block_stride_8x8 = blocks_x_8x8;
			mapping.block_width_8x8 = std::min<int>(4, blocks_x_8x8 - 4 * x);
			mapping.block_height_8x8 = std::min<int>(4, blocks_y_8x8 - 4 * y);
			block_32x32_to_8x8_mapping.push_back(mapping);
			block_count_32x32++;
		}
	}

	block_count_8x8 += blocks_x_8x8 * blocks_y_8x8;
}

void WaveletBuffers::init_block_meta()
{
	for (int level = DecompositionLevels - 1; level >= 0; level--)
	{
		for (int component = 0; component < NumComponents; component++)
		{
			// Ignore top-level CbCr when doing 420 subsampling.
			if (level == 0 && component != 0 && chroma == ChromaSubsampling::Chroma420)
				continue;

			for (int band = (level == DecompositionLevels - 1 ? 0 : 1); band < 4; band++)
			{
				uint32_t level_width = wavelet_img_high_res->get_width(level);
				uint32_t level_height = wavelet_img_high_res->get_height(level);

				int blocks_x_8x8 = (level_width + 7) / 8;
				int blocks_y_8x8 = (level_height + 7) / 8;
				int blocks_x_32x32 = (level_width + 31) / 32;
				int blocks_y_32x32 = (level_height + 31) / 32;

				block_meta[component][level][band] = {
					block_count_8x8, blocks_x_8x8,
					block_count_32x32, blocks_x_32x32,
					blocks_x_32x32 * blocks_y_32x32
				};

				accumulate_block_mapping(blocks_x_8x8, blocks_y_8x8);
			}
		}
	}
}

bool WaveletBuffers::init(Device *device_, int width_, int height_, ChromaSubsampling chroma_, bool fragment_path_)
{
	if (!device_ || !device_->get_device_features().vk12_features.shaderFloat16 || fragment_path_)
	{
		LOGE("PyroWave requires a non-null shaderFloat16 device and compute path; fragment_path is unsupported.\n");
		return false;
	}

	VkFormatProperties3KHR properties = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3_KHR };
	device_->get_format_properties(VK_FORMAT_R16_SFLOAT, &properties);
	if (!pyrowave_wavelet_format_supported(properties.optimalTilingFeatures,
	        device_->get_device_features().enabled_features.shaderStorageImageWriteWithoutFormat))
	{
		LOGE("PyroWave requires R16_SFLOAT sampled/storage/transfer support and storage writes without format.\n");
		return false;
	}
	device = device_;
	width = width_;
	height = height_;
	chroma = chroma_;

	aligned_width = align(width, Alignment);
	aligned_height = align(height, Alignment);
	aligned_width = std::max<int>(aligned_width, MinimumImageSize);
	aligned_height = std::max<int>(aligned_height, MinimumImageSize);

	init_samplers();
	if (!allocate_images())
	{
		LOGE("PyroWave wavelet image or view allocation failed.\n");
		return false;
	}

	init_block_meta();

	Vulkan::ResourceLayout layout;

	// If the GPU is sufficiently competent with texel buffers, we can use that as a fallback to 8-bit storage.
	if (device->get_gpu_properties().limits.maxTexelBufferElements >= 16 * 1024 * 1024)
	{
		auto vendor_id = device->get_gpu_properties().vendorID;
		if (!device->get_device_features().vk12_features.storageBuffer8BitAccess ||
		    (vendor_id != VENDOR_ID_AMD && vendor_id != VENDOR_ID_INTEL && vendor_id != VENDOR_ID_NVIDIA &&
		     device->get_device_features().driver_id != VK_DRIVER_ID_SAMSUNG_PROPRIETARY))
		{
			use_readonly_texel_buffer = true;
		}
	}

	if (use_readonly_texel_buffer)
		LOGI("Using texel buffers instead of SSBO.\n");

	shaders = Shaders<>(*device, layout, nullptr);

	return true;
}
}
