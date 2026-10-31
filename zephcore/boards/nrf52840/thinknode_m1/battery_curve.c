/*
 * SPDX-License-Identifier: MIT
 */

#include <helpers/battery_curve.h>

/*
 * ThinkNode M1 uses the same single-cell LiPo profile as ThinkNode M5.
 * The board's charger/ADC path reaches about 4.10–4.15 V at full charge,
 * so the 4.19 V generic curve would under-report a fully charged pack.
 */
static const uint16_t ocv_thinknode_m1[11] = {
	4100, 4050, 3990, 3890, 3800,
	3720, 3630, 3530, 3420, 3300,
	3100,
};

const battery_curve_t battery_curve_default = {
	.ocv_mv     = ocv_thinknode_m1,
	.num_points = 11,
	.num_cells  = 1,
};
