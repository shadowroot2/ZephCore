/* SPDX-License-Identifier: MIT */
#pragma once

static constexpr const char *COMPANION_SOS_CANCELED_TEXT = "❌ SOS отменён";

static inline bool companion_sos_button_cancelable(bool pending, bool fall)
{
	return pending && !fall;
}

/* The final emergency message is the point of the whole sequence: when the
 * radio queue refuses it the state stays and the send retries on the waiting
 * cadence — once at the fix, then at most once per waiting repeat, never a
 * tight loop and never dropped. */
static inline bool companion_sos_final_send_due(bool final_failed, bool waiting_due)
{
	return !final_failed || waiting_due;
}

/* `fall off` must reach a fall emergency live at any stage: the repeating
 * alarm outlives the sos state (the final send clears it) and is meant to
 * ring until acknowledged, so the alarm flag alone can be all that is left. */
static inline bool companion_fall_off_live(bool alarm_active, bool sos_pending,
					   bool sos_is_fall)
{
	return alarm_active || (sos_pending && sos_is_fall);
}
