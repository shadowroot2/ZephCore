/* SPDX-License-Identifier: MIT */
/*
 * Host tests for the tracking deferred backlog: the "🕓 HH:MM 📍 <link>"
 * message, the ring of points no repeater repeated, and the flush schedule
 * (probe 2.5 min before the report, verdict 5 s after a send, drain 10 s apart).
 */

#include <TrackingBacklog.h>
#include <TrackingMessage.h>
#include "test.h"

#include <cstring>
#include <string>

#define PT1234 45240U /* 12:34:00 UTC, day 0 */
#define PT1239 45540U /* 12:39:00 UTC, day 0 */

static const int64_t LAT = 55755800000LL; /* 55.75580 */
static const int64_t LON = 37617300000LL; /* 37.61730 */

TEST(tracking_deferred_format, "UNIT-TRACKBUF-001",
     "Deferred points carry their time and a whole map link") {
	struct tracking_deferred_point p = { PT1234, LAT, LON };
	char buf[160];

	tracking_format_deferred_point(buf, sizeof(buf), &p, 0);
	CHECK(std::string(buf) == "🕓 12:34 📍 https://maps.google.com/?q=55.75580,37.61730");

	/* Local wall clock via the display timezone. */
	tracking_format_deferred_point(buf, sizeof(buf), &p, 120);
	CHECK(std::string(buf) == "🕓 14:34 📍 https://maps.google.com/?q=55.75580,37.61730");

	/* One line is 60 bytes; a budget of 120 keeps the second point out
	 * rather than cutting its link, 121 takes both. */
	struct tracking_deferred_point pts[2] = {
		{ PT1234, LAT, LON },
		{ PT1239, LAT, LON },
	};

	CHECK(tracking_format_deferred(buf, 121, pts, 2, 0) == 2);
	CHECK(std::string(buf) ==
	      "🕓 12:34 📍 https://maps.google.com/?q=55.75580,37.61730\n"
	      "🕓 12:39 📍 https://maps.google.com/?q=55.75580,37.61730");
	CHECK(tracking_format_deferred(buf, 120, pts, 2, 0) == 1);
	CHECK(std::string(buf) == "🕓 12:34 📍 https://maps.google.com/?q=55.75580,37.61730");
	CHECK(tracking_format_deferred(buf, 60, pts, 2, 0) == 1);
	CHECK(tracking_format_deferred(buf, 59, pts, 2, 0) == 0);
	CHECK(std::string(buf) == "");
}

TEST(tracking_backlog_ring, "UNIT-TRACKBUF-002",
     "The backlog keeps point order and the verdict moves points in and out") {
	struct tracking_backlog b;
	int i;

	tracking_backlog_reset(&b);
	CHECK(tracking_backlog_empty(&b));

	tracking_backlog_push(&b, PT1234, LAT, LON);
	tracking_backlog_push(&b, PT1239, LAT + 1, LON + 1);
	CHECK(b.count == 2);
	CHECK(b.pts[0].time_s == PT1234 && b.pts[1].time_s == PT1239);

	/* A live report that was not heard joins the tail and closes the tap. */
	struct tracking_deferred_point live = { 46000U, LAT + 2, LON + 2 };

	tracking_backlog_begin_live(&b, 11, 1000, &live);
	b.draining = true;
	CHECK(b.inflight && b.inflight_live);
	tracking_backlog_verdict(&b, false);
	CHECK(b.count == 3 && b.pts[2].time_s == 46000U);
	CHECK(!b.draining);

	/* A heard backlog message drops the head entries it carried and opens
	 * the tap for the next send. */
	tracking_backlog_begin(&b, 22, 5000, 2);
	tracking_backlog_verdict(&b, true);
	CHECK(b.count == 1 && b.pts[0].time_s == 46000U);
	CHECK(b.draining);
	CHECK(b.next_send_ms == 5000 + TRACKING_BACKLOG_DRAIN_INTERVAL_MS);

	/* An unheard backlog message keeps its points and closes the tap. */
	tracking_backlog_begin(&b, 33, 9000, 1);
	tracking_backlog_verdict(&b, false);
	CHECK(b.count == 1 && b.pts[0].time_s == 46000U);
	CHECK(!b.draining);

	/* Full ring: the oldest is dropped, the newest positions stay. */
	tracking_backlog_reset(&b);
	for (i = 0; i < TRACKING_BACKLOG_SLOTS + 3; i++) {
		tracking_backlog_push(&b, 1000U + (uint32_t)i, LAT, LON);
	}
	CHECK(b.count == TRACKING_BACKLOG_SLOTS);
	CHECK(b.pts[0].time_s == 1003U);
	CHECK(b.pts[TRACKING_BACKLOG_SLOTS - 1].time_s ==
	      1000U + TRACKING_BACKLOG_SLOTS + 2U);
}

TEST(tracking_backlog_schedule, "UNIT-TRACKBUF-003",
     "The flush schedule: verdict after a send, probe and drain deadlines") {
	struct tracking_backlog b;

	tracking_backlog_reset(&b);
	CHECK(tracking_backlog_next(&b, 0, 100) == TRACKING_BACKLOG_IDLE);

	/* A send is due its heard question only after the verdict window. */
	tracking_backlog_push(&b, PT1234, LAT, LON);
	tracking_backlog_begin(&b, 1, 1000, 1);
	CHECK(tracking_backlog_next(&b, 1000 + TRACKING_BACKLOG_VERDICT_MS - 1, 0) ==
	      TRACKING_BACKLOG_IDLE);
	CHECK(tracking_backlog_next(&b, 1000 + TRACKING_BACKLOG_VERDICT_MS, 0) ==
	      TRACKING_BACKLOG_VERDICT);

	/* Tap closed: the head waits for the caller's probe deadline. */
	tracking_backlog_verdict(&b, false);
	CHECK(tracking_backlog_next(&b, 2000, 7000) == TRACKING_BACKLOG_IDLE);
	CHECK(tracking_backlog_next(&b, 7000, 7000) == TRACKING_BACKLOG_SEND);

	/* Tap open: one message per drain interval. */
	tracking_backlog_reset(&b);
	tracking_backlog_push(&b, PT1234, LAT, LON);
	tracking_backlog_push(&b, PT1239, LAT, LON);
	tracking_backlog_push(&b, 46000U, LAT, LON);
	tracking_backlog_begin(&b, 2, 8000, 2);
	tracking_backlog_verdict(&b, true);
	CHECK(b.draining);
	CHECK(tracking_backlog_next(&b, 8000 + TRACKING_BACKLOG_DRAIN_INTERVAL_MS - 1, 0) ==
	      TRACKING_BACKLOG_IDLE);
	CHECK(tracking_backlog_next(&b, 8000 + TRACKING_BACKLOG_DRAIN_INTERVAL_MS, 0) ==
	      TRACKING_BACKLOG_SEND);
}
