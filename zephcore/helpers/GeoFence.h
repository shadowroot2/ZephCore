/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "TrackingMessage.h"

/* Geofence ("поводок"): the node itself versus one saved point (ADR 0011).
 * Pure logic — the caller feeds fresh fixes only (age <= GEO_FIX_MAX_AGE_MS)
 * and owns the GPS duty. Exit fires on the GEO_EXIT_CONFIRM_FIXES-th
 * consecutive fix beyond the radius; return fires once at
 * radius - GEO_REENTER_HYST_M and re-arms the exit trigger. */

#define GEO_EXIT_CONFIRM_FIXES	1
#define GEO_REENTER_HYST_M	50.0
#define GEO_DUTY_SEC		500U
#define GEO_FIX_MAX_AGE_MS	30000U
#define GEO_RADIUS_M_DEFAULT	3000U
#define GEO_RADIUS_M_MIN	50U
#define GEO_RADIUS_M_MAX	50000U
/* The default saved point (Abay × Baitursynov, Almaty): home slots and the
 * geofence point all start here. Degrees x 1e7. */
#define ZC_POINT_DEFAULT_LAT_E7	432411170 /* 43.241117 */
#define ZC_POINT_DEFAULT_LON_E7	769273010 /* 76.927301 */
#define GEO_LAT7_DEFAULT	ZC_POINT_DEFAULT_LAT_E7
#define GEO_LON7_DEFAULT	ZC_POINT_DEFAULT_LON_E7

#define GEO_EMOJI_EXIT		"🚪"
#define GEO_EMOJI_ENTER		"✅"

enum geofence_event {
	GEO_EVENT_NONE = 0,
	GEO_EVENT_EXIT,
	GEO_EVENT_REENTER,
};

struct geofence_state {
	uint8_t outside;   /* alerted and still out; re-arms on return */
	uint8_t beyond;	   /* consecutive fixes past the radius */
	uint8_t boot_fix;  /* next fix reports the zone state once (post-boot) */
};

static inline void geofence_reset(struct geofence_state *g)
{
	g->outside = 0;
	g->beyond = 0;
	g->boot_fix = 0;
}

/* Post-boot re-arm: the first fix reports once — REENTER if inside the zone,
 * EXIT if beyond the radius. */
static inline void geofence_boot_arm(struct geofence_state *g)
{
	geofence_reset(g);
	g->boot_fix = 1;
}

/* Feed the distance of one fresh fix; returns the event to report, if any. */
static inline enum geofence_event geofence_update(struct geofence_state *g,
						  double dist_m,
						  double radius_m)
{
	bool boot = g->boot_fix != 0;

	g->boot_fix = 0;
	if (g->outside) {
		if (dist_m < radius_m - GEO_REENTER_HYST_M) {
			g->outside = 0;
			g->beyond = 0;
			return GEO_EVENT_REENTER;
		}
		return GEO_EVENT_NONE;
	}
	if (dist_m > radius_m) {
		if (boot || g->beyond + 1 >= GEO_EXIT_CONFIRM_FIXES) {
			g->outside = 1;
			g->beyond = 0;
			return GEO_EVENT_EXIT;
		}
		g->beyond++;
		return GEO_EVENT_NONE;
	}
	g->beyond = 0;
	return boot ? GEO_EVENT_REENTER : GEO_EVENT_NONE;
}

/* "50м", "950м", "3км", "3.5км", "3.25км" — compact zone-message form, the
 * unit glued to the value; trailing zeros are trimmed. */
static inline void geofence_dist_str(char *buf, size_t len, double meters)
{
	if (meters < 1000.0) {
		snprintf(buf, len, "%uм", (unsigned)(meters + 0.5));
		return;
	}
	unsigned hundredths = (unsigned)(meters / 10.0 + 0.5); /* km x 100 */
	unsigned frac = hundredths % 100U;

	if (frac == 0) {
		snprintf(buf, len, "%uкм", hundredths / 100U);
	} else if (frac % 10U == 0) {
		snprintf(buf, len, "%u.%uкм", hundredths / 100U, frac / 10U);
	} else {
		snprintf(buf, len, "%u.%02uкм", hundredths / 100U, frac);
	}
}

/* "🚪 Вышел из геозоны\n📐 3км\n📏 3.5км\n📍 <link>", or the
 * "✅ Вернулся в геозону" twin. `len` is the real send budget
 * (MAX_TEXT_LEN minus the "name: " prefix the send path adds): when the
 * message does not fit, the radius line goes first so the maps link — always
 * the last line — is never cut mid-URL. */
static inline void geofence_format_msg(char *buf, size_t len, bool exited,
				       double radius_m, double dist_m,
				       double lat_deg, double lon_deg)
{
	char r[12], d[12], url[48];
	const char *head = exited ? "Вышел из геозоны" : "Вернулся в геозону";
	const char *emoji = exited ? GEO_EMOJI_EXIT : GEO_EMOJI_ENTER;
	int n;

	geofence_dist_str(r, sizeof(r), radius_m);
	geofence_dist_str(d, sizeof(d), dist_m);
	tracking_maps_url(url, sizeof(url), lat_deg, lon_deg);
	n = snprintf(buf, len, "%s %s\n📐 %s\n📏 %s\n📍 %s",
		     emoji, head, r, d, url);
	if (n > 0 && (size_t)n < len) {
		return;
	}
	snprintf(buf, len, "%s %s\n📏 %s\n📍 %s", emoji, head, d, url);
}

/* "43.24112,76.92730" -> two degrees in range (a space between them is also
 * accepted). Exactly two finite numbers, no trailing tokens. */
static inline bool geofence_parse_point(const char *arg, double *lat, double *lon)
{
	char *end;
	double a = strtod(arg, &end);

	if (end == arg) {
		return false;
	}
	while (*end == ' ' || *end == '\t' || *end == ',') {
		end++;
	}
	const char *second = end;
	double b = strtod(second, &end);

	if (end == second) {
		return false;
	}
	while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t') {
		end++;
	}
	if (*end != '\0' ||
	    a < -90.0 || a > 90.0 || b < -180.0 || b > 180.0) {
		return false;
	}
	*lat = a;
	*lon = b;
	return true;
}
