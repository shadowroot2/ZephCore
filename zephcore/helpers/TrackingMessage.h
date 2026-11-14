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

/* Literal-per-case instead of a static pointer table: the table was emitted
 * per including TU and its pointers kept data relocations. */
static inline const char *tracking_report_arrow(uint32_t bearing_mdeg)
{
	switch (tracking_report_sector_index(bearing_mdeg)) {
	case 0: return "⬆️";
	case 1: return "↗️";
	case 2: return "➡️";
	case 3: return "↘️";
	case 4: return "⬇️";
	case 5: return "↙️";
	case 6: return "⬅️";
	default: return "↖️";
	}
}

static inline const char *tracking_report_sector(uint32_t bearing_mdeg)
{
	switch (tracking_report_sector_index(bearing_mdeg)) {
	case 0: return "С";
	case 1: return "СВ";
	case 2: return "В";
	case 3: return "ЮВ";
	case 4: return "Ю";
	case 5: return "ЮЗ";
	case 6: return "З";
	default: return "СЗ";
	}
}

/* "https://maps.google.com/?q=%.5f,%.5f" — one literal for every message
 * that links to a position. */
static inline void tracking_maps_url(char *buf, size_t len,
				     double lat_deg, double lon_deg)
{
	snprintf(buf, len, "https://maps.google.com/?q=%.5f,%.5f",
		 lat_deg, lon_deg);
}

/* The arrival notice: "<head>\n📍 <point>\n⏱️: <dur>\n📏 <dist>\n<link>".
 * `len` is the send budget after the headline (MAX_TEXT_LEN minus the
 * "name: " prefix on LoRa); when it does not fit, the ⏱ and 📏 lines drop
 * in that order so the maps link — always last — survives whole. */
static inline void nav_format_arrival(char *buf, size_t len, const char *head,
				      int point_idx, const char *point_name,
				      const char *dur, const char *dist,
				      const char *url)
{
	int n = snprintf(buf, len, "%s\n📍 %d-%s\n⏱️: %s\n📏 %s\n%s",
			 head, point_idx, point_name, dur, dist, url);
	if (n > 0 && (size_t)n < len) {
		return;
	}
	n = snprintf(buf, len, "%s\n📍 %d-%s\n📏 %s\n%s",
		     head, point_idx, point_name, dist, url);
	if (n > 0 && (size_t)n < len) {
		return;
	}
	snprintf(buf, len, "%s\n📍 %d-%s\n%s", head, point_idx, point_name, url);
}

/* One tracking report: "🏃 12 км/ч ↗️ СВ · <maps link>" while moving,
 * "🅿️ <maps link>" at standstill. speed_mms/bearing_mdeg are GNSS nav data. */
static inline void tracking_format_report(char *buf, size_t len,
					  double lat_deg, double lon_deg,
					  uint32_t speed_mms,
					  uint32_t bearing_mdeg)
{
	char url[48];
	unsigned kph = (unsigned)((speed_mms * 36U + 5000U) / 10000U);

	tracking_maps_url(url, sizeof(url), lat_deg, lon_deg);
	if (kph <= TRACKING_REPORT_STANDSTILL_KMH) {
		snprintf(buf, len, "🅿️\n📍 %s", url);
		return;
	}
	snprintf(buf, len, "🏃 %u км/ч %s %s\n📍 %s",
		 kph, tracking_report_arrow(bearing_mdeg),
		 tracking_report_sector(bearing_mdeg), url);
}

/* ===== Deferred points: a report no neighbour repeated, re-sent later =====
 * The buffer keeps when and where; the message says both so a point that
 * arrives late still has its moment: "🕓 12:34 📍 <maps link>". */

struct tracking_deferred_point {
	uint32_t time_s; /* wall clock at the fix (RTC, unix seconds) */
	int64_t lat_ndeg;
	int64_t lon_ndeg;
};

/* One deferred point, local wall clock via tz_minutes from UTC. */
static inline void tracking_format_deferred_point(char *buf, size_t len,
	const struct tracking_deferred_point *p, int16_t tz_minutes)
{
	char url[48];
	int64_t local = (int64_t)p->time_s + (int64_t)tz_minutes * 60;
	uint32_t day_sec = (uint32_t)(local < 0 ? 0 : local % 86400);

	tracking_maps_url(url, sizeof(url), p->lat_ndeg / 1e9, p->lon_ndeg / 1e9);
	snprintf(buf, len, "🕓 %02u:%02u 📍 %s",
		 day_sec / 3600U, (day_sec % 3600U) / 60U, url);
}

/* Up to two deferred points per message: "🕓 12:34 📍 <link>\n🕓 12:39
 * 📍 <link>". `len` is the send budget after the "name: " prefix; a point
 * is dropped whole rather than cutting a maps link. Returns how many points
 * were packed. */
static inline int tracking_format_deferred(char *buf, size_t len,
	const struct tracking_deferred_point *pts, int n, int16_t tz_minutes)
{
	int packed = 0;
	size_t off = 0;
	int i;

	buf[0] = '\0';
	for (i = 0; i < n; i++) {
		char line[72];
		size_t line_len, need;

		tracking_format_deferred_point(line, sizeof(line), &pts[i], tz_minutes);
		line_len = strlen(line);
		need = line_len + (off ? 1U : 0U);
		if (off + need > len) {
			break;
		}
		if (off) {
			buf[off++] = '\n';
		}
		memcpy(buf + off, line, line_len);
		off += line_len;
		packed++;
	}
	buf[off] = '\0';
	return packed;
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
	unsigned idx = (unsigned)((bearing_deg + 22.5) / 45.0) % 8U;

	switch (idx) {
	case 0: return "N";
	case 1: return "NE";
	case 2: return "E";
	case 3: return "SE";
	case 4: return "S";
	case 5: return "SW";
	case 6: return "W";
	default: return "NW";
	}
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

/* "8м", "9ч59м", "1д23ч15м" — minutes only, h+min, or d+h+min. Clamped at
 * 99д23ч59м so the worst case still fits the mesh text frame. */
static inline void tracking_duration_str(char *buf, size_t len, uint64_t ms)
{
	uint64_t min = (ms + 30000U) / 60000U;

	if (min > 99U * 24U * 60U + 23U * 60U + 59U) {
		min = 99U * 24U * 60U + 23U * 60U + 59U;
	}
	if (min < 60U) {
		snprintf(buf, len, "%uм", (unsigned)min);
	} else if (min < 24U * 60U) {
		snprintf(buf, len, "%uч%02uм", (unsigned)(min / 60U),
			 (unsigned)(min % 60U));
	} else {
		snprintf(buf, len, "%uд%uч%02uм",
			 (unsigned)(min / (24U * 60U)),
			 (unsigned)((min / 60U) % 24U), (unsigned)(min % 60U));
	}
}

/* Three lines: "🏁 12.4 км · 2ч15м · 🚶 1ч48м", the stop/speed line, and
 * the finish link. Sized so the worst case fits the mesh text frame. */
static inline void tracking_format_summary(char *buf, size_t len,
					   const struct tracking_odometer *o,
					   int64_t end_lat_ndeg, int64_t end_lon_ndeg,
					   bool has_fix)
{
	char total[16], moving[16];

	tracking_duration_str(total, sizeof(total), o->moving_ms + o->stopped_ms);
	tracking_duration_str(moving, sizeof(moving), o->moving_ms);
	unsigned km10 = (unsigned)((o->distance_mm + 50000U) / 100000U);
	unsigned avg10 = o->moving_ms
		? (unsigned)(((uint64_t)o->distance_mm * 36U + o->moving_ms / 2) / o->moving_ms)
		: 0;
	unsigned maxk = (unsigned)((o->max_speed_mms * 36U + 5000U) / 10000U);
	unsigned stopmin = (unsigned)((o->stopped_ms + 30000U) / 60000U);
	char stopbuf[16];

	if (stopmin > 59U) {
		tracking_duration_str(stopbuf, sizeof(stopbuf), o->stopped_ms);
	} else {
		snprintf(stopbuf, sizeof(stopbuf), "%u мин", stopmin);
	}
	int n = snprintf(buf, len,
			 "🏁 %u.%uкм (%u.%u / %u км/ч)\n⏱️ %s\n🚶 %s\n"
			 "🅿️ %s (%u)",
			 km10 / 10, km10 % 10, avg10 / 10, avg10 % 10, maxk,
			 total, moving, stopbuf, o->stops);

	if (has_fix && n > 0 && (size_t)n < len) {
		char url[48];

		tracking_maps_url(url, sizeof(url),
				  end_lat_ndeg / 1e9, end_lon_ndeg / 1e9);
		snprintf(buf + n, len - n, "\n📍 %s", url);
	}
}
