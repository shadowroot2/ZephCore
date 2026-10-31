/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>

static inline bool gps_switch_effective_enabled(bool switch_present,
						 bool switch_on, bool saved_enabled)
{
	return switch_present ? switch_on : saved_enabled;
}
