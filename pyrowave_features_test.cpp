// Copyright (c) 2026 AlkaidLab contributors
// SPDX-License-Identifier: MIT AND GPL-3.0-only
#include "pyrowave.h"
#include "pyrowave_features.hpp"
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Failed line %d: %s\n", __LINE__, #x); return 1; } } while (false)

static unsigned loader_calls, queue_calls;
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_instance_proc_addr(VkInstance, const char *)
{
	++loader_calls;
	return nullptr;
}
static void queue_callback(void *)
{
	++queue_calls;
}

int main()
{
	VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	CHECK(!pyrowave_has_enabled_float16(nullptr));
	CHECK(!pyrowave_has_enabled_float16(&device_info));
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	VkPhysicalDevice16BitStorageFeatures unrelated = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES };
	VkPhysicalDeviceVulkan12Features core = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	VkPhysicalDeviceShaderFloat16Int8Features extension = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES };
	features.pNext = &unrelated;
	device_info.pNext = &features;
	CHECK(!pyrowave_has_enabled_float16(&device_info));

	pyrowave_device_create_info info = {};
	info.GetInstanceProcAddr = get_instance_proc_addr;
	info.queue_lock_callback = queue_callback;
	info.queue_unlock_callback = queue_callback;
	auto sentinel = reinterpret_cast<pyrowave_device>(&info);
	pyrowave_device output = sentinel;
	CHECK(pyrowave_create_device(nullptr, &output) == PYROWAVE_ERROR_INVALID_ARGUMENT);
	CHECK(pyrowave_create_device(&info, nullptr) == PYROWAVE_ERROR_INVALID_ARGUMENT);
	CHECK(pyrowave_create_device(&info, &output) == PYROWAVE_ERROR_INVALID_ARGUMENT);
	info.device_create_info = &device_info;
	CHECK(pyrowave_create_device(&info, nullptr) == PYROWAVE_ERROR_INVALID_ARGUMENT);
	device_info.pNext = nullptr;
	CHECK(pyrowave_create_device(&info, &output) == PYROWAVE_ERROR_NO_VULKAN);
	device_info.pNext = &features;
	CHECK(pyrowave_create_device(&info, &output) == PYROWAVE_ERROR_NO_VULKAN);
	for (unsigned form = 0; form < 2; ++form)
	{
		unrelated.pNext = form ? static_cast<void *>(&extension) : static_cast<void *>(&core);
		CHECK(!pyrowave_has_enabled_float16(&device_info));
		CHECK(pyrowave_create_device(&info, &output) == PYROWAVE_ERROR_NO_VULKAN);
		CHECK(output == sentinel && loader_calls == 0 && queue_calls == 0);
		if (form)
			extension.shaderFloat16 = VK_TRUE;
		else
			core.shaderFloat16 = VK_TRUE;
		CHECK(pyrowave_has_enabled_float16(&device_info));
	}
	CHECK(output == sentinel && loader_calls == 0 && queue_calls == 0);
	std::puts("FEATURES_PASS (no Vulkan device or loader used)");
	return 0;
}
