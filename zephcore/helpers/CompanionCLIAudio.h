/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>

static inline bool companion_cli_silent_notifications(uint32_t sender_timestamp)
{
	return sender_timestamp != 0;
}
