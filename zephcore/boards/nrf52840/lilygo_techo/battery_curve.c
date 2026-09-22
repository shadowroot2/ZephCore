/*
 * SPDX-License-Identifier: MIT
 */

#include <helpers/battery_curve.h>

/* Same 1S LiPo curve as ThinkNode M1. The T-ECHO charger reaches
 * approximately 4.10–4.15 V at a completed charge. */
static const uint16_t ocv_lilygo_techo[11] = {
	4100, 4050, 3990, 3890, 3800,
	3720, 3630, 3530, 3420, 3300,
	3100,
};

const battery_curve_t battery_curve_default = {
	.ocv_mv     = ocv_lilygo_techo,
	.num_points = 11,
	.num_cells  = 1,
};
