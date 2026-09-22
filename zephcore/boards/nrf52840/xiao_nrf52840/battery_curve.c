/*
 * SPDX-License-Identifier: MIT
 */

#include "battery_curve.h"

/*
 * Use 4.17 V as the nominal full-charge point for the battery curve.
 * Keep the generic single-cell LiPo discharge profile below that point.
 */
static const uint16_t ocv_xiao_nrf52840[21] = {
	4170, 4120, 4050, 4020, 3990, /* 100 .. 80% */
	3940, 3890, 3845, 3800, 3760, /*  75 .. 55% */
	3720, 3675, 3630, 3580, 3530, /*  50 .. 30% */
	3475, 3420, 3360, 3300, 3200, /*  25 ..  5% */
	3100,                          /*   0%       */
};

const battery_curve_t battery_curve_default = {
	.ocv_mv     = ocv_xiao_nrf52840,
	.num_points = 21,
	.num_cells  = 1,
};
