/*
 * ZephCore - UI timezone offset helper
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t ui_get_timezone_offset_minutes(void);
void ui_set_timezone_offset_minutes(int16_t offset_minutes);
uint32_t ui_local_epoch(uint32_t utc_epoch);
uint32_t ui_local_day_seconds(uint32_t utc_epoch);
void ui_timezone_format_label(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
