/*
 * SPDX-License-Identifier: MIT
 */

#include <helpers/battery_curve.h>

/*
 * Heltec T114 charger reaches 4.15 V at a completed 1S LiPo charge.
 * The generic 4.19 V top point therefore reported 96% on a full pack.
 */
static const uint16_t ocv_heltec_t114[21] = {
	4150, 4120, 4050, 4020, 3990, /* 100 .. 80% */
	3940, 3890, 3845, 3800, 3760, /*  75 .. 55% */
	3720, 3675, 3630, 3580, 3530, /*  50 .. 30% */
	3475, 3420, 3360, 3300, 3200, /*  25 ..  5% */
	3200,                          /*   0%       */
};

const battery_curve_t battery_curve_default = {
	.ocv_mv     = ocv_heltec_t114,
	.num_points = 21,
	.num_cells  = 1,
};
