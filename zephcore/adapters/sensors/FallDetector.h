#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*fall_detector_callback_t)(void);

/* Starts the board accelerometer sampler. The callback is executed from the
 * Zephyr system workqueue and must only signal work to the application thread. */
void fall_detector_begin(fall_detector_callback_t callback);
/* Called after all Fall checks have passed, immediately before alarm dispatch. */
void fall_detector_set_prealert_callback(fall_detector_callback_t callback);
bool fall_detector_is_available(void);
bool fall_detector_set_enabled(bool enabled);
bool fall_detector_is_enabled(void);
/* 1 = most sensitive, 3 = M3 production profile (default), 5 = strict. */
bool fall_detector_set_sensitivity(uint8_t sensitivity);
uint8_t fall_detector_get_sensitivity(void);
/* Rearm immediately after the user acknowledges a fall alarm. */
void fall_detector_reset(void);

#ifdef __cplusplus
}
#endif
