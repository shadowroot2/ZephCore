/* SPDX-License-Identifier: MIT */
/*
 * Host tests for the PUBLIC message feed: the newest-first ring, the
 * "HH:MM"/"DD.MM HH:MM" line time at the display offset, the ">>"/Public/
 * "[#channel]" line shapes, the emoji substitution table and the redraw
 * decision of a push while the viewer is down the list.
 */

#include <UiFeed.h>

#include "test.h"

#include <cstring>
#include <string>

/* 2026-11-19 12:34 UTC */
#define NOW 1795091640U
/* 2026-11-18 12:34 UTC */
#define YESTERDAY 1795005240U
/* 2026-11-19 10:00 UTC */
#define EARLIER_TODAY 1795082400U
/* 2026-11-19 23:50 UTC = 2026-11-20 02:50 at UTC+3 */
#define LATE 1795132200U

static struct ui_feed_entry entry(uint32_t ts, uint8_t flags, const char *name,
				  const char *text)
{
	struct ui_feed_entry e;

	memset(&e, 0, sizeof(e));
	e.ts = ts;
	e.flags = flags;
	strncpy(e.name, name, sizeof(e.name) - 1);
	strncpy(e.text, text, sizeof(e.text) - 1);
	return e;
}

static struct ui_feed_entry ch_entry(uint32_t ts, const char *channel,
				     const char *text)
{
	struct ui_feed_entry e = entry(ts, 0, "", text);

	strncpy(e.channel, channel, sizeof(e.channel) - 1);
	return e;
}

TEST(feed_ring_newest_first, "UNIT-FEED-001",
     "The ring keeps the newest on top and evicts the oldest on overflow")
{
	struct ui_feed f;
	uint8_t pos = 0;

	ui_feed_reset(&f);
	CHECK(f.count == 0);
	CHECK(ui_feed_get(&f, 0) == NULL);

	for (int i = 0; i < UI_FEED_SLOTS; i++) {
		char name[8];

		snprintf(name, sizeof(name), "n%d", i);
		struct ui_feed_entry e = entry(NOW + (uint32_t)i, 0, name, "x");
		CHECK(ui_feed_push(&f, &e, &pos) == UI_FEED_REDRAW_NOW);
		CHECK(f.count == (uint8_t)(i + 1));
	}
	/* Newest first: n9 on top, n0 at the bottom. */
	CHECK(std::string(ui_feed_get(&f, 0)->name) == "n9");
	CHECK(std::string(ui_feed_get(&f, UI_FEED_SLOTS - 1)->name) == "n0");
	CHECK(ui_feed_get(&f, UI_FEED_SLOTS) == NULL);

	/* Overflow evicts the oldest (n0), never drops the newest. */
	struct ui_feed_entry e = entry(NOW + 99, 0, "new", "x");
	CHECK(ui_feed_push(&f, &e, &pos) == UI_FEED_REDRAW_NOW);
	CHECK(f.count == UI_FEED_SLOTS);
	CHECK(std::string(ui_feed_get(&f, 0)->name) == "new");
	CHECK(std::string(ui_feed_get(&f, 1)->name) == "n9");
	CHECK(std::string(ui_feed_get(&f, UI_FEED_SLOTS - 1)->name) == "n1");
	for (uint8_t i = 0; i < f.count; i++) {
		CHECK(std::string(ui_feed_get(&f, i)->name) != "n0");
	}
}

TEST(feed_time_today_and_before, "UNIT-FEED-002",
     "Line time is HH:MM today and DD.MM HH:MM on an earlier local day")
{
	char buf[32];

	/* UTC: same day, and the day before. */
	ui_feed_format_time(buf, sizeof(buf), EARLIER_TODAY, NOW, 0);
	CHECK(std::string(buf) == "10:00");
	ui_feed_format_time(buf, sizeof(buf), NOW, NOW, 0);
	CHECK(std::string(buf) == "12:34");
	ui_feed_format_time(buf, sizeof(buf), YESTERDAY, NOW, 0);
	CHECK(std::string(buf) == "18.11 12:34");

	/* The local day, not the UTC day, decides: at UTC+3 both of these
	 * land on 2026-11-20, so they are "today" together. */
	ui_feed_format_time(buf, sizeof(buf), LATE, LATE, 180);
	CHECK(std::string(buf) == "02:50");
	/* Against a now that is still 2026-11-19 locally, it is yesterday. */
	ui_feed_format_time(buf, sizeof(buf), LATE, NOW, 180);
	CHECK(std::string(buf) == "20.11 02:50");

	/* A negative offset moves the day the other way: 23:50 UTC at UTC-3
	 * is 20:50 the same local day. */
	ui_feed_format_time(buf, sizeof(buf), LATE, NOW, -180);
	CHECK(std::string(buf) == "20:50");

	/* No sender timestamp at all. */
	ui_feed_format_time(buf, sizeof(buf), 0, NOW, 0);
	CHECK(std::string(buf) == "--:--");

	/* Month and year rollovers still format as DD.MM. */
	ui_feed_format_time(buf, sizeof(buf), 1772355900U, NOW, 0); /* 2026-03-01 */
	CHECK(std::string(buf) == "01.03 09:05");
	ui_feed_format_time(buf, sizeof(buf), 1767254400U, NOW, 0); /* 2026-01-01 */
	CHECK(std::string(buf) == "01.01 08:00");
}

TEST(feed_line_shapes, "UNIT-FEED-003",
     "Direct, public and channel lines carry their own markers")
{
	char buf[192];
	/* A channel message carries its sender in the text already. */
	struct ui_feed_entry dm = entry(NOW, UI_FEED_F_DM, "Mama", "ты где?");
	struct ui_feed_entry pub = ch_entry(YESTERDAY, "Public", "Nick: привет, как дела?");
	struct ui_feed_entry bare = ch_entry(YESTERDAY, "", "Nick: привет, как дела?");
	struct ui_feed_entry grp = ch_entry(NOW, "#zephcore", "Nick: тру ляля");

	ui_feed_format_line(buf, sizeof(buf), &dm, NOW, 0);
	CHECK(std::string(buf) == ">> 12:34 Mama: ты где?");

	/* The public channel is not tagged. */
	ui_feed_format_line(buf, sizeof(buf), &pub, NOW, 0);
	CHECK(std::string(buf) == "18.11 12:34 Nick: привет, как дела?");
	ui_feed_format_line(buf, sizeof(buf), &bare, NOW, 0);
	CHECK(std::string(buf) == "18.11 12:34 Nick: привет, как дела?");

	/* Any other channel is. */
	ui_feed_format_line(buf, sizeof(buf), &grp, NOW, 0);
	CHECK(std::string(buf) == "12:34 [#zephcore] Nick: тру ляля");

	/* A direct message keeps its own name, whatever channel it claims. */
	strncpy(dm.channel, "#zephcore", sizeof(dm.channel) - 1);
	ui_feed_format_line(buf, sizeof(buf), &dm, NOW, 0);
	CHECK(std::string(buf) == ">> 12:34 Mama: ты где?");
}

TEST(feed_emoji_substitution, "UNIT-FEED-004",
     "Emoji become text, unknown pictographs go, other multi-byte text stays")
{
	char buf[96];

	ui_feed_emoji_text(buf, sizeof(buf), "\xF0\x9F\x99\x82 hi");
	CHECK(std::string(buf) == ":) hi");

	ui_feed_emoji_text(buf, sizeof(buf), "SOS \xF0\x9F\x86\x98!");
	CHECK(std::string(buf) == "SOS SOS!!");

	/* The variation selector is not left dangling behind a substitution. */
	ui_feed_emoji_text(buf, sizeof(buf), "\xE2\x9D\xA4\xEF\xB8\x8F you");
	CHECK(std::string(buf) == "<3 you");

	/* Cyrillic is multi-byte and must survive untouched. */
	ui_feed_emoji_text(buf, sizeof(buf), "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
	CHECK(std::string(buf) == "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");

	/* An unknown 4-byte pictograph is dropped whole, never sliced. */
	ui_feed_emoji_text(buf, sizeof(buf), "a\xF0\x9F\xA6\x84z"); /* 🦄 */
	CHECK(std::string(buf) == "az");

	/* The buffer is bounded and always NUL-terminated. */
	ui_feed_emoji_text(buf, 4, "abcdef");
	CHECK(std::string(buf) == "abc");
	ui_feed_emoji_text(buf, 1, "abc");
	CHECK(std::string(buf) == "");
}

TEST(feed_push_redraw_decision, "UNIT-FEED-005",
     "A push redraws at the top and holds the screen while reading older")
{
	struct ui_feed f;
	uint8_t pos = 0;

	ui_feed_reset(&f);

	/* At the top: redraw now, nothing moves. */
	struct ui_feed_entry a = entry(NOW, 0, "a", "1");
	CHECK(ui_feed_push(&f, &a, &pos) == UI_FEED_REDRAW_NOW);
	CHECK(pos == 0);

	/* Down the list: the push is deferred and the reading position stays
	 * on the same message, which is now one slot further from the top. */
	struct ui_feed_entry b = entry(NOW + 1, 0, "b", "2");
	CHECK(ui_feed_push(&f, &b, &pos) == UI_FEED_REDRAW_NOW);
	pos = 1; /* reading "a" */
	struct ui_feed_entry c = entry(NOW + 2, 0, "c", "3");
	CHECK(ui_feed_push(&f, &c, &pos) == UI_FEED_REDRAW_DEFER);
	CHECK(pos == 2);
	CHECK(std::string(ui_feed_get(&f, pos)->name) == "a");
	CHECK(std::string(ui_feed_get(&f, 0)->name) == "c");

	/* A NULL view means "nobody is reading": always redraw now. */
	struct ui_feed_entry d = entry(NOW + 3, 0, "d", "4");
	CHECK(ui_feed_push(&f, &d, NULL) == UI_FEED_REDRAW_NOW);
	CHECK(f.count == 4);
}

TEST(feed_wrap_rows, "UNIT-FEED-006",
     "Rows break at a space and never split a UTF-8 sequence")
{
	char row[64];
	const char *p;
	int n;

	/* ASCII wraps at the last space that fits. */
	p = "hello wide world";
	n = ui_feed_wrap_row(row, sizeof(row), &p, 11);
	CHECK(std::string(row) == "hello wide");
	CHECK(n == 10);
	n = ui_feed_wrap_row(row, sizeof(row), &p, 11);
	CHECK(std::string(row) == "world");
	CHECK(*p == '\0');

	/* One Cyrillic sequence is one column, not two bytes. */
	p = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80";	/* привет мир */
	n = ui_feed_wrap_row(row, sizeof(row), &p, 6);
	CHECK(std::string(row) == "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
	CHECK(n == 12);	/* 6 code points, 2 bytes each */
	n = ui_feed_wrap_row(row, sizeof(row), &p, 6);
	CHECK(std::string(row) == "\xD0\xBC\xD0\xB8\xD1\x80");

	/* A long word with no space is cut at the column limit and the
	 * cut is never inside a sequence. */
	p = "abcd\xD0\xB0\xD0\xB1\xD0\xB2\xD0\xB3\xD0\xB4\xD0\xB5";
	n = ui_feed_wrap_row(row, sizeof(row), &p, 5);
	CHECK(n == 4 + 2);	/* "abcd" + one Cyrillic letter */
	CHECK((unsigned char)row[4] == 0xD0);
}

TEST(feed_time_epoch_edge, "UNIT-FEED-007",
     "An epoch-second clock with a negative offset stays on its own day")
{
	char buf[32];

	/* ts=1000 at UTC-12 is 1969-12-31 12:16 local: day -1, not day 0.
	 * A truncating division would call it "today" next to a day-0 message. */
	CHECK(ui_feed_local_day(1000, -720) == -1);
	CHECK(ui_feed_local_day(50000, -720) == 0);
	CHECK(ui_feed_local_day(1000, -720) != ui_feed_local_day(50000, -720));

	/* And the date it prints is 31.12, not a rolled-over 01.01. */
	ui_feed_format_time(buf, sizeof(buf), 1000, 50000, -720);
	CHECK(std::string(buf) == "31.12 12:16");
}

TEST(feed_wrap_leading_spaces, "UNIT-FEED-008",
     "A row of only spaces still makes progress")
{
	char row[64];
	const char *p = "     tail";
	int n;

	n = ui_feed_wrap_row(row, sizeof(row), &p, 3);
	CHECK(n <= 3);
	CHECK(*p != '\0');	/* the cursor moved */
	/* The rest is reachable in one more call. */
	n = ui_feed_wrap_row(row, sizeof(row), &p, 16);
	CHECK(std::string(row) == "tail");
	CHECK(*p == '\0');
}

TEST(feed_utf8_truncated, "UNIT-FEED-009",
     "A lead byte cut off by the terminator never walks past it")
{
	char buf[32];

	/* A lone lead byte is one byte, not two. */
	CHECK(ui_feed_utf8_len("\xD0") == 1);
	CHECK(ui_feed_utf8_len("") == 0);
	CHECK(ui_feed_utf8_len("\xD0\x9F") == 2);	/* complete */
	CHECK(ui_feed_utf8_len("\xD0X") == 1);		/* bad continuation */

	/* Substitution copies what is there and stops. */
	ui_feed_emoji_text(buf, sizeof(buf), "a\xD0");
	CHECK(std::string(buf) == "a\xD0");

	/* Wrapping does not step over the terminator either. */
	{
		char row[8];
		const char *p = "ab\xD0";

		(void)ui_feed_wrap_row(row, sizeof(row), &p, 8);
		CHECK(*p == '\0');
		CHECK(std::string(row) == "ab\xD0");
	}
}

