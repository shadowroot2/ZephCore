/*
 * SPDX-License-Identifier: MIT
 */

#include <helpers/battery_curve.h>

/*
 * Elecrow ThinkNode M5 follows Meshtastic's board-specific OCV table. Its
 * charger/ADC path commonly tops out around 4.10-4.15 V, so using the generic
 * 4.19 V curve makes a fully charged pack look like ~95%.
 */
static const uint16_t ocv_thinknode_m5[11] = {
	4100, 4050, 3990, 3890, 3800,
	3720, 3630, 3530, 3420, 3300,
	3100,
};

const battery_curve_t battery_curve_default = {
	.ocv_mv     = ocv_thinknode_m5,
	.num_points = 11,
	.num_cells  = 1,
};
