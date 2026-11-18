/* SPDX-License-Identifier: MIT */
#pragma once

static constexpr const char *COMPANION_SOS_CANCELED_TEXT = "❌ SOS отменён";

static inline bool companion_sos_button_cancelable(bool pending, bool fall)
{
	return pending && !fall;
}
