// Copyright (c) 2026 AlkaidLab contributors
// SPDX-License-Identifier: MIT AND GPL-3.0-only
#include "pyrowave_features.hpp"
#include "pyrowave_wait.hpp"
#include <stdio.h>

static int check(bool condition, const char *description)
{
	if (condition)
		return 0;
	fprintf(stderr, "FAIL: %s\n", description);
	return 1;
}

struct FakeFence
{
	bool result;
	unsigned calls = 0;
	uint64_t timeout = 0;
	bool wait_timeout(uint64_t value) { ++calls; timeout = value; return result; }
};

static int check_readback_wait()
{
	FakeFence fence{true};
	int failures = 0;
	failures += check(pyrowave_wait_for_readback(static_cast<FakeFence *>(nullptr)), "null fence succeeds");
	failures += check(fence.calls == 0, "null fence does not wait");
	failures += check(pyrowave_wait_for_readback(&fence), "successful fence wait");
	failures += check(fence.calls == 1 && fence.timeout == 1000000000ull, "one bounded wait");
	fence.result = false;
	fence.timeout = 0;
	failures += check(!pyrowave_wait_for_readback(&fence), "failed wait propagated");
	failures += check(fence.calls == 2 && fence.timeout == 1000000000ull, "failed wait uses same bound");
	return failures;
}

int main()
{
	using namespace PyroWave;
	constexpr VkFormatFeatureFlags2 flags[] = {
		VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT, VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT,
		VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT, VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT
	};
	constexpr auto all = flags[0] | flags[1] | flags[2] | flags[3];
	int failures = check_readback_wait();
	failures += check(native_fp16_supported(true, true, all), "valid native FP16");
	failures += check(!native_fp16_supported(false, true, all), "missing shaderFloat16");
	failures += check(!native_fp16_supported(true, false, all), "missing writeWithoutFormat");
	failures += check(!native_fp16_supported(false, false, all), "both features missing");
	for (auto flag : flags)
	{
		failures += check(!native_fp16_supported(true, true, all & ~flag), "missing format flag");
		failures += check(!wavelet_format_supported(all & ~flag, true), "legacy missing format flag");
	}
	failures += check(!native_fp16_supported(true, true, 0), "no format support");
	failures += check(wavelet_format_supported(all, true), "valid legacy storage");
	failures += check(!wavelet_format_supported(all, false), "legacy requires writeWithoutFormat");
	return failures ? 1 : 0;
}
