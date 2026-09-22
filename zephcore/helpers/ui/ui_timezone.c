/*
 * ZephCore - UI timezone offset helper
 * SPDX-License-Identifier: MIT
 */

#include "ui_timezone.h"

#include <stdio.h>

static int16_t s_timezone_offset_minutes = CONFIG_ZEPHCORE_UI_TIMEZONE_OFFSET_MINUTES;

int16_t ui_get_timezone_offset_minutes(void)
{
	return s_timezone_offset_minutes;
}

void ui_set_timezone_offset_minutes(int16_t offset_minutes)
{
	if (offset_minutes < -1439) {
		offset_minutes = -1439;
	} else if (offset_minutes > 1439) {
		offset_minutes = 1439;
	}
	s_timezone_offset_minutes = offset_minutes;
}

uint32_t ui_local_epoch(uint32_t utc_epoch)
{
	int64_t local = (int64_t)utc_epoch + ((int64_t)s_timezone_offset_minutes * 60);

	if (local < 0) {
		local = 0;
	}
	return (uint32_t)local;
}

uint32_t ui_local_day_seconds(uint32_t utc_epoch)
{
	return ui_local_epoch(utc_epoch) % 86400U;
}

void ui_timezone_format_label(char *out, size_t out_size)
{
	if (out_size == 0) {
		return;
	}

	int offset = s_timezone_offset_minutes;
	if (offset == 0) {
		snprintf(out, out_size, "UTC");
		return;
	}

	char sign = offset < 0 ? '-' : '+';
	int abs_min = offset < 0 ? -offset : offset;
	int hours = abs_min / 60;
	int minutes = abs_min % 60;

	if (minutes == 0) {
		snprintf(out, out_size, "GMT%c%d", sign, hours);
	} else {
		snprintf(out, out_size, "GMT%c%d:%02d", sign, hours, minutes);
	}
}
