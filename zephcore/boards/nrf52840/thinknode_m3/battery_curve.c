/*
 * ThinkNode M3 single-cell LiPo OCV curve.
 *
 * On the tested M3 the magnetic charger settles at 4.139 V after absorption.
 * Treat 4.13 V as full and preserve a smooth top segment below it.
 */

#include <helpers/battery_curve.h>

static const uint16_t ocv_thinknode_m3[21] = {
	4130, 4100, 4050, 4020, 3990, /* 100 .. 80% */
	3940, 3890, 3845, 3800, 3760, /*  75 .. 55% */
	3720, 3675, 3630, 3580, 3530, /*  50 .. 30% */
	3475, 3420, 3360, 3300, 3200, /*  25 ..  5% */
	3100,                          /*   0% */
};

const battery_curve_t battery_curve_default = {
	.ocv_mv     = ocv_thinknode_m3,
	.num_points = 21,
	.num_cells  = 1,
};
