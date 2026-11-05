/* SPDX-License-Identifier: MIT */
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* RMC course over ground is noise at walking speeds: at or below this many
 * km/h the report carries no speed and no course, just the maps link. */
#define TRACKING_REPORT_STANDSTILL_KMH 2U

/* Eight compass sectors centered on the cardinal directions. */
static inline unsigned tracking_report_sector_index(uint32_t bearing_mdeg)
{
	return (bearing_mdeg + 22500U) / 45000U % 8U;
}

static inline const char *tracking_report_arrow(uint32_t bearing_mdeg)
{
	static const char *const arrows[8] = {
		"⬆️", "↗️", "➡️", "↘️", "⬇️", "↙️", "⬅️", "↖️",
	};
	return arrows[tracking_report_sector_index(bearing_mdeg)];
}

static inline const char *tracking_report_sector(uint32_t bearing_mdeg)
{
	static const char *const sectors[8] = {
		"С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ",
	};
	return sectors[tracking_report_sector_index(bearing_mdeg)];
}

/* One tracking report: "🏃 12 км/ч ↗️ СВ · <maps link>" while moving,
 * "🅿️ <maps link>" at standstill. speed_mms/bearing_mdeg are GNSS nav data. */
static inline void tracking_format_report(char *buf, size_t len,
					  double lat_deg, double lon_deg,
					  uint32_t speed_mms,
					  uint32_t bearing_mdeg)
{
	unsigned kph = (unsigned)((speed_mms * 36U + 5000U) / 10000U);
	if (kph <= TRACKING_REPORT_STANDSTILL_KMH) {
		snprintf(buf, len, "🅿️\n📍 https://maps.google.com/?q=%.5f,%.5f",
			 lat_deg, lon_deg);
		return;
	}
	snprintf(buf, len, "🏃 %u км/ч %s %s\n📍 https://maps.google.com/?q=%.5f,%.5f",
		 kph, tracking_report_arrow(bearing_mdeg),
		 tracking_report_sector(bearing_mdeg), lat_deg, lon_deg);
}

/* ===== Navigation to a saved point ===== */

static inline double nav_bearing_deg(double lat0, double lon0, double lat1, double lon1)
{
	const double deg_to_rad = 0.017453292519943295;
	double dlon = (lon1 - lon0) * deg_to_rad;
	double y = sin(dlon) * cos(lat1 * deg_to_rad);
	double x = cos(lat0 * deg_to_rad) * sin(lat1 * deg_to_rad) -
		   sin(lat0 * deg_to_rad) * cos(lat1 * deg_to_rad) * cos(dlon);
	double deg = atan2(y, x) / deg_to_rad;

	if (deg < 0) {
		deg += 360.0;
	}
	return deg;
}

/* "2.34 km" from 1 km up, whole meters below. */
static inline void nav_distance_str(char *buf, size_t len, double meters)
{
	if (meters >= 1000.0) {
		unsigned hundredths = (unsigned)(meters * 100.0 + 0.5);

		snprintf(buf, len, "%u.%02u km", hundredths / 100000U,
			 (hundredths / 1000U) % 100U);
	} else {
		snprintf(buf, len, "%u m", (unsigned)(meters + 0.5));
	}
}

static inline const char *nav_cardinal_en(double bearing_deg)
{
	static const char *const names[8] = {
		"N", "NE", "E", "SE", "S", "SW", "W", "NW",
	};
	unsigned idx = (unsigned)((bearing_deg + 22.5) / 45.0) % 8U;

	return names[idx];
}

/* "2.34 км" from 1 km up, whole meters below (Russian units). */
static inline void nav_distance_ru_str(char *buf, size_t len, double meters)
{
	if (meters >= 1000.0) {
		unsigned hundredths = (unsigned)(meters * 100.0 + 0.5);

		snprintf(buf, len, "%u.%02u км", hundredths / 100000U,
			 (hundredths / 1000U) % 100U);
	} else {
		snprintf(buf, len, "%u м", (unsigned)(meters + 0.5));
	}
}

/* "2.34 км  СВ  ↗️" — distance, target sector and the travel arrow. */
static inline void nav_report_str(char *buf, size_t len, double dist_m,
				  double bearing_deg)
{
	char d[16];

	nav_distance_ru_str(d, sizeof(d), dist_m);
	snprintf(buf, len, "%s  %s  %s", d,
		 tracking_report_sector((uint32_t)(bearing_deg * 1000.0)),
		 tracking_report_arrow((uint32_t)(bearing_deg * 1000.0)));
}

/* Wrong-way ladder: the distance grew by 30 m stepwise since the last sample
 * where it shrank — 30, 60, 90 ... away from the current position. Any
 * approach resets the ladder, so standing still on GPS jitter cannot
 * retrigger. Floats: the struct sits in RAM on every board. */
struct nav_wrongway {
	float prev_dist_m;
	float away_m;
	float mark_m;
};

static inline void nav_wrongway_reset(struct nav_wrongway *w)
{
	w->prev_dist_m = -1.0f;
	w->away_m = 0.0f;
	w->mark_m = 30.0f;
}

/* Feed the current distance; returns true at each new 30 m step away. */
static inline bool nav_wrongway_update(struct nav_wrongway *w, double dist_m)
{
	if (w->prev_dist_m < 0.0f) {
		w->prev_dist_m = (float)dist_m;
		return false;
	}
	double delta = dist_m - w->prev_dist_m;
	w->prev_dist_m = (float)dist_m;
	if (delta > 0.0) {
		w->away_m += (float)delta;
		if (w->away_m >= w->mark_m) {
			w->mark_m += 30.0f;
			return true;
		}
	} else {
		w->away_m = 0.0f;
		w->mark_m = 30.0f;
	}
	return false;
}

/* ===== Hike odometer: one tracking session, from tracking on to tracking
 * off. Pure logic — the caller feeds validated fixes and closes the session.
 * A fix-to-fix segment counts as moving when the fix reports more than
 * TRACKING_REPORT_STANDSTILL_KMH; only moving segments add distance and the
 * max speed, and a stop is one moving-to-stopped transition. ===== */

struct tracking_odometer {
	uint32_t distance_mm;
	uint32_t moving_ms;
	uint32_t stopped_ms;
	uint16_t stops;
	uint32_t max_speed_mms;
	int64_t last_ms;
	int64_t last_lat_ndeg;
	int64_t last_lon_ndeg;
	uint32_t last_speed_mms;
	bool has_last;
	bool was_moving;
};

static inline void tracking_odometer_reset(struct tracking_odometer *o, int64_t now_ms)
{
	memset(o, 0, sizeof(*o));
	o->last_ms = now_ms;
}

static inline double tracking_distance_m(int64_t lat0_ndeg, int64_t lon0_ndeg,
					 int64_t lat1_ndeg, int64_t lon1_ndeg)
{
	const double deg_to_rad = 0.017453292519943295;
	double dlat = (lat1_ndeg - lat0_ndeg) / 1e9 * deg_to_rad;
	double dlon = (lon1_ndeg - lon0_ndeg) / 1e9 * deg_to_rad;
	double lat_mid = (lat0_ndeg + lat1_ndeg) / 2e9 * deg_to_rad;
	double x = dlon * cos(lat_mid);

	return 6371000.0 * sqrt(x * x + dlat * dlat);
}

static inline void tracking_odometer_segment(struct tracking_odometer *o,
					     int64_t to_ms, uint32_t speed_mms,
					     bool have_pos, int64_t lat_ndeg,
					     int64_t lon_ndeg)
{
	int64_t dt = to_ms - o->last_ms;

	if (dt < 0) {
		dt = 0;
	}
	unsigned kph = (unsigned)((speed_mms * 36U + 5000U) / 10000U);
	bool moving = kph > TRACKING_REPORT_STANDSTILL_KMH;

	if (moving) {
		if (o->has_last && have_pos) {
			o->distance_mm += (uint32_t)(tracking_distance_m(
				o->last_lat_ndeg, o->last_lon_ndeg, lat_ndeg, lon_ndeg) * 1000.0);
		}
		o->moving_ms += (uint32_t)dt;
		if (speed_mms > o->max_speed_mms) {
			o->max_speed_mms = speed_mms;
		}
	} else {
		o->stopped_ms += (uint32_t)dt;
		if (o->was_moving) {
			o->stops++;
		}
	}
	o->was_moving = moving;
	o->last_ms = to_ms;
	if (have_pos) {
		o->last_lat_ndeg = lat_ndeg;
		o->last_lon_ndeg = lon_ndeg;
		o->has_last = true;
	}
	o->last_speed_mms = speed_mms;
}

static inline void tracking_odometer_on_fix(struct tracking_odometer *o, int64_t now_ms,
					    int64_t lat_ndeg, int64_t lon_ndeg,
					    uint32_t speed_mms)
{
	tracking_odometer_segment(o, now_ms, speed_mms, true, lat_ndeg, lon_ndeg);
}

/* Closes the trailing segment, classified by the last fix's speed. */
static inline void tracking_odometer_finish(struct tracking_odometer *o, int64_t now_ms)
{
	tracking_odometer_segment(o, now_ms, o->last_speed_mms, false, 0, 0);
}

static inline void tracking_duration_str(char *buf, size_t len, uint32_t ms)
{
	uint32_t min = (ms + 30000U) / 60000U;

	snprintf(buf, len, "%uч%02uм", min / 60, min % 60);
}

/* Three lines: "🏁 12.4 км · 2ч15м · 🚶 1ч48м", the stop/speed line, and
 * the finish link. Sized so the worst case fits the mesh text frame. */
static inline void tracking_format_summary(char *buf, size_t len,
					   const struct tracking_odometer *o,
					   int64_t end_lat_ndeg, int64_t end_lon_ndeg,
					   bool has_fix)
{
	char total[12], moving[12];

	tracking_duration_str(total, sizeof(total), o->moving_ms + o->stopped_ms);
	tracking_duration_str(moving, sizeof(moving), o->moving_ms);
	unsigned km10 = (unsigned)((o->distance_mm + 50000U) / 100000U);
	unsigned avg10 = o->moving_ms
		? (unsigned)(((uint64_t)o->distance_mm * 36U + o->moving_ms / 2) / o->moving_ms)
		: 0;
	unsigned maxk = (unsigned)((o->max_speed_mms * 36U + 5000U) / 10000U);
	unsigned stopmin = (unsigned)((o->stopped_ms + 30000U) / 60000U);
	int n = snprintf(buf, len,
			 "🏁 %u.%uкм (%u.%u / %u км/ч)\n⏱️ %s\n🚶 %s\n"
			 "🅿️ %u мин (%u)",
			 km10 / 10, km10 % 10, avg10 / 10, avg10 % 10, maxk,
			 total, moving, stopmin, o->stops);

	if (has_fix && n > 0 && (size_t)n < len) {
		snprintf(buf + n, len - n, "\n📍 https://maps.google.com/?q=%.5f,%.5f",
			 end_lat_ndeg / 1e9, end_lon_ndeg / 1e9);
	}
}
