/* SPDX-License-Identifier: MIT */
#pragma once

/*
 * TrackingBacklog — deferred re-send of tracking points the mesh did not
 * carry. A report no neighbour repeated reached nobody; its point (time +
 * coords) waits here and goes out again, oldest first, in the
 * "🕓 HH:MM 📍 <link>" form once a send is heard again. A fresh point joins
 * the tail whenever the backlog is not empty, so a later report never
 * overtakes an earlier one.
 *
 * Timing (k_uptime_get() ms): a send's heard-verdict is asked
 * TRACKING_BACKLOG_VERDICT_MS after the send (ContentionTracker dupe count,
 * one-shot). While the tap is closed the head is probed at the caller's
 * probe deadline — TRACKING_BACKLOG_PROBE_LEAD_MS before the next tracking
 * report, long enough for the ring to drain before the report is due, so
 * the scheduled point goes out live as always. A heard send opens the tap: the backlog then flushes one message every
 * TRACKING_BACKLOG_DRAIN_INTERVAL_MS. A message carries up to two points
 * (tracking_format_deferred).
 */

#include <TrackingMessage.h>

#define TRACKING_BACKLOG_SLOTS 16
#define TRACKING_BACKLOG_VERDICT_MS 5000
#define TRACKING_BACKLOG_DRAIN_INTERVAL_MS 10000
/* 2.5 min before the report: the whole ring drains in less, so the report
 * is due only once the backlog is out and goes live as always. */
#define TRACKING_BACKLOG_PROBE_LEAD_MS 150000
/* With no report running (`tracking off`) the probe repeats on this period
 * so an ended session still flushes what accumulated. */
#define TRACKING_BACKLOG_PROBE_RETRY_MS 60000

enum tracking_backlog_action {
	TRACKING_BACKLOG_IDLE = 0,
	TRACKING_BACKLOG_SEND,
	TRACKING_BACKLOG_VERDICT,
};

struct tracking_backlog {
	struct tracking_deferred_point pts[TRACKING_BACKLOG_SLOTS];
	uint8_t count;
	bool draining;     /* the last backlog send was heard: keep flushing */
	bool inflight;   /* a message is awaiting its heard verdict */
	uint32_t inflight_hash;
	int64_t inflight_sent_ms;
	int64_t inflight_due_ms;
	uint8_t inflight_points;  /* head entries the message carried */
	bool inflight_live;    /* a live report: its point is not in the ring */
	struct tracking_deferred_point inflight_live_pt;
	int64_t next_send_ms;   /* the drain deadline (valid while draining) */
};

static inline void tracking_backlog_reset(struct tracking_backlog *b)
{
	memset(b, 0, sizeof(*b));
}

static inline bool tracking_backlog_empty(const struct tracking_backlog *b)
{
	return b->count == 0;
}

/* Appends a point; when full the oldest is dropped — the newest positions
 * are the ones a searcher needs. */
static inline void tracking_backlog_push(struct tracking_backlog *b, uint32_t time_s,
					 int64_t lat_ndeg, int64_t lon_ndeg)
{
	if (b->count == TRACKING_BACKLOG_SLOTS) {
		for (int i = 1; i < b->count; i++) {
			b->pts[i - 1] = b->pts[i];
		}
		b->count--;
	}
	b->pts[b->count].time_s = time_s;
	b->pts[b->count].lat_ndeg = lat_ndeg;
	b->pts[b->count].lon_ndeg = lon_ndeg;
	b->count++;
}

/* A live report left: remember its point (it joins the tail when the send
 * is not heard) and when to ask the heard question. */
static inline void tracking_backlog_begin_live(struct tracking_backlog *b, uint32_t hash,
					       int64_t now_ms,
					       const struct tracking_deferred_point *pt)
{
	b->inflight = true;
	b->inflight_live = true;
	b->inflight_hash = hash;
	b->inflight_sent_ms = now_ms;
	b->inflight_due_ms = now_ms + TRACKING_BACKLOG_VERDICT_MS;
	b->inflight_points = 1;
	b->inflight_live_pt = *pt;
}

/* A backlog message (probe or drain) left: it carries the first `points`
 * entries, which leave the ring only when the message is heard. */
static inline void tracking_backlog_begin(struct tracking_backlog *b, uint32_t hash,
					  int64_t now_ms, int points)
{
	b->inflight = true;
	b->inflight_live = false;
	b->inflight_hash = hash;
	b->inflight_sent_ms = now_ms;
	b->inflight_due_ms = now_ms + TRACKING_BACKLOG_VERDICT_MS;
	b->inflight_points = (uint8_t)points;
}

/* The inflight message's verdict: "heard" means at least one neighbour
 * repeated the flood. An unheard live report joins the tail (the point is
 * still worth delivering); an unheard backlog message leaves its points
 * where they are and closes the tap. */
static inline void tracking_backlog_verdict(struct tracking_backlog *b, bool heard)
{
	if (!b->inflight) {
		return;
	}
	if (b->inflight_live) {
		if (!heard) {
			/* The point is still worth delivering and the tap just
			 * proved closed. */
			tracking_backlog_push(b, b->inflight_live_pt.time_s,
					      b->inflight_live_pt.lat_ndeg,
					      b->inflight_live_pt.lon_ndeg);
			b->draining = false;
		}
		b->inflight = false;
		return;
	}
	if (heard) {
		int n = b->inflight_points < b->count ? b->inflight_points : b->count;

		for (int i = n; i < b->count; i++) {
			b->pts[i - n] = b->pts[i];
		}
		b->count = (uint8_t)(b->count - n);
		/* The tap is open: the next message goes out on the drain
		 * cadence even if only the fresh report is left. */
		b->draining = true;
		b->next_send_ms = b->inflight_sent_ms + TRACKING_BACKLOG_DRAIN_INTERVAL_MS;
	} else {
		b->draining = false;
	}
	b->inflight = false;
}

/* What to do at now_ms: VERDICT when the inflight send is due its heard
 * question, SEND when the head message may go out (the drain cadence while
 * the tap is open, otherwise the caller's probe deadline), else IDLE. */
static inline enum tracking_backlog_action
tracking_backlog_next(const struct tracking_backlog *b, int64_t now_ms, int64_t probe_ms)
{
	if (b->inflight) {
		return now_ms >= b->inflight_due_ms ? TRACKING_BACKLOG_VERDICT
						    : TRACKING_BACKLOG_IDLE;
	}
	if (b->count == 0) {
		return TRACKING_BACKLOG_IDLE;
	}
	if (b->draining) {
		return now_ms >= b->next_send_ms ? TRACKING_BACKLOG_SEND
						 : TRACKING_BACKLOG_IDLE;
	}
	return now_ms >= probe_ms ? TRACKING_BACKLOG_SEND : TRACKING_BACKLOG_IDLE;
}
