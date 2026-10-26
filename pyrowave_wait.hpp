// Copyright (c) 2026 AlkaidLab contributors
// SPDX-License-Identifier: MIT AND GPL-3.0-only
#pragma once

#include <cstdint>

template<class Fence>
bool pyrowave_wait_for_readback(const Fence &fence)
{
	return !fence || fence->wait_timeout(1000000000ull);
}
