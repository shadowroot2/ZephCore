/*
 * SPDX-License-Identifier: MIT
 *
 * pico-sdk's sleep_until() for the Zephyr build of the RP2040 port.
 *
 * entropy_rpi_pico wraps pico_rand, whose ROSC sampler sleeps between bit
 * samples. The rpi_pico hal glue compiles hardware_timer (busy_wait) but not
 * pico_time, so sleep_until() is the one undefined pico-sdk symbol. This is
 * the whole shim: a Zephyr sleep to the absolute target. Only pico_rand calls
 * in (PICO_RAND_MIN_ROSC_BIT_SAMPLE_TIME_US, tens of microseconds).
 */

#include <pico/time.h>
#include <zephyr/kernel.h>

void sleep_until(absolute_time_t target)
{
	int64_t delta_us = absolute_time_diff_us(get_absolute_time(), target);

	if (delta_us > 0) {
		k_usleep((uint32_t)delta_us);
	}
}
