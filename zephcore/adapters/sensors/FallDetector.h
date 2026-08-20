#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*fall_detector_callback_t)(void);

/* Starts the board accelerometer sampler. The callback is executed from the
 * Zephyr system workqueue and must only signal work to the application thread. */
void fall_detector_begin(fall_detector_callback_t callback);
bool fall_detector_is_available(void);

#ifdef __cplusplus
}
#endif
