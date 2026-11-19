/* SPDX-License-Identifier: MIT */
/*
 * Host tests for the conversation feed: the per-chat ring and its newest
 * first order, the "HH:MM"/"DD.MM HH:MM" line time at the display offset,
 * the ">>"/Public/"[#channel]" line shapes, the emoji substitution table,
 * the word wrap and the continuous paging that spills a long message onto
 * the next page.
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

static struct ui_chat_list fresh_list(void)
{
	struct ui_chat_list l;

	ui_chat_list_reset(&l);
	return l;
}

TEST(chat_ring_newest_first, "UNIT-FEED-001",
     "A chat keeps the newest message on top and evicts the oldest")
{
	struct ui_chat_list l = fresh_list();
	char name[8];

	for (int i = 0; i < UI_CHAT_MSGS; i++) {
		char text[8];

		snprintf(name, sizeof(name), "c");
		snprintf(text, sizeof(text), "m%d", i);
		CHECK(ui_chat_push(&l, name, true, NOW + (uint32_t)i, text)
		      != NULL);
	}
	{
		struct ui_chat *c = ui_chat_find(&l, name);

		CHECK(c != NULL);
		CHECK(c->count == UI_CHAT_MSGS);
		CHECK(c->unread == UI_CHAT_MSGS);
		/* Newest first: m3 on top, m0 at the bottom. */
		CHECK(std::string(ui_chat_get(c, 0)->text) == "m3");
		CHECK(std::string(ui_chat_get(c, UI_CHAT_MSGS - 1)->text) == "m0");
		CHECK(ui_chat_get(c, UI_CHAT_MSGS) == NULL);
	}
	/* Overflow evicts the oldest (m0), never drops the newest. */
	CHECK(ui_chat_push(&l, name, true, NOW + 99, "new") != NULL);
	{
		struct ui_chat *c = ui_chat_find(&l, name);

		CHECK(c->count == UI_CHAT_MSGS);
		CHECK(std::string(ui_chat_get(c, 0)->text) == "new");
		CHECK(std::string(ui_chat_get(c, 1)->text) == "m3");
		CHECK(std::string(ui_chat_get(c, UI_CHAT_MSGS - 1)->text) == "m1");
		for (uint8_t i = 0; i < c->count; i++) {
			CHECK(std::string(ui_chat_get(c, i)->text) != "m0");
		}
	}
}

TEST(chat_list_order, "UNIT-FEED-002",
     "The list shows channels first, then direct chats")
{
	struct ui_chat_list l = fresh_list();

	CHECK(ui_chat_push(&l, "Mama", false, NOW, "hi") != NULL);
	CHECK(ui_chat_push(&l, "Public", true, NOW, "hi") != NULL);
	CHECK(ui_chat_push(&l, "Papa", false, NOW, "hi") != NULL);
	CHECK(ui_chat_push(&l, "#zephcore", true, NOW, "hi") != NULL);

	CHECK(ui_chat_list_len(&l) == 4);
	/* Groups first in slot order, then DMs in slot order. */
	CHECK(std::string(ui_chat_nth(&l, 0)->name) == "Public");
	CHECK(std::string(ui_chat_nth(&l, 1)->name) == "#zephcore");
	CHECK(std::string(ui_chat_nth(&l, 2)->name) == "Mama");
	CHECK(std::string(ui_chat_nth(&l, 3)->name) == "Papa");
	CHECK(ui_chat_nth(&l, 4) == NULL);
	CHECK(ui_chat_nth(&l, -1) == NULL);
	/* Both groups are flagged, both chats are not. */
	CHECK(ui_chat_nth(&l, 0)->is_group);
	CHECK(!ui_chat_nth(&l, 2)->is_group);
}

TEST(chat_unread, "UNIT-FEED-003",
     "Unread counts an arrival and clears on the visit")
{
	struct ui_chat_list l = fresh_list();
	struct ui_chat *c;

	c = ui_chat_push(&l, "Public", true, NOW, "one");
	CHECK(c != NULL);
	CHECK(c->unread == 1);
	(void)ui_chat_push(&l, "Public", true, NOW, "two");
	(void)ui_chat_push(&l, "Public", true, NOW, "three");
	CHECK(c->unread == 3);

	/* Entering the conversation is the reset. */
	c->unread = 0;
	CHECK(c->unread == 0);
	(void)ui_chat_push(&l, "Public", true, NOW, "four");
	CHECK(c->unread == 1);

	/* The counter saturates rather than wrapping. */
	c->unread = 0xFFFF;
	(void)ui_chat_push(&l, "Public", true, NOW, "five");
	CHECK(c->unread == 0xFFFF);
}

TEST(feed_time_today_and_before, "UNIT-FEED-004",
     "Line time is HH:MM today and DD.MM HH:MM on an earlier local day")
{
	char buf[32];

	ui_feed_format_time(buf, sizeof(buf), EARLIER_TODAY, NOW, 0);
	CHECK(std::string(buf) == "10:00");
	ui_feed_format_time(buf, sizeof(buf), NOW, NOW, 0);
	CHECK(std::string(buf) == "12:34");
	ui_feed_format_time(buf, sizeof(buf), YESTERDAY, NOW, 0);
	CHECK(std::string(buf) == "18.11 12:34");

	ui_feed_format_time(buf, sizeof(buf), LATE, LATE, 180);
	CHECK(std::string(buf) == "02:50");
	ui_feed_format_time(buf, sizeof(buf), LATE, NOW, 180);
	CHECK(std::string(buf) == "20.11 02:50");
	ui_feed_format_time(buf, sizeof(buf), LATE, NOW, -180);
	CHECK(std::string(buf) == "20:50");

	ui_feed_format_time(buf, sizeof(buf), 0, NOW, 0);
	CHECK(std::string(buf) == "--:--");

	ui_feed_format_time(buf, sizeof(buf), 1772355900U, NOW, 0);
	CHECK(std::string(buf) == "01.03 09:05");
	ui_feed_format_time(buf, sizeof(buf), 1767254400U, NOW, 0);
	CHECK(std::string(buf) == "01.01 08:00");
}

TEST(feed_time_epoch_edge, "UNIT-FEED-005",
     "An epoch-second clock with a negative offset stays on its own day")
{
	char buf[32];

	CHECK(ui_feed_local_day(1000, -720) == -1);
	CHECK(ui_feed_local_day(50000, -720) == 0);
	CHECK(ui_feed_local_day(1000, -720) != ui_feed_local_day(50000, -720));
	ui_feed_format_time(buf, sizeof(buf), 1000, 50000, -720);
	CHECK(std::string(buf) == "31.12 12:16");
}

TEST(feed_line_shapes, "UNIT-FEED-006",
     "Direct, public and channel lines carry their own markers")
{
	char buf[192];
	struct ui_chat_list l = fresh_list();
	struct ui_chat *dm;
	struct ui_chat *pub;
	struct ui_chat *grp;
	struct ui_chat_msg m;

	(void)ui_chat_push(&l, "Mama", false, NOW, "ты где?");
	(void)ui_chat_push(&l, "Public", true, YESTERDAY, "Nick: привет, как дела?");
	(void)ui_chat_push(&l, "#zephcore", true, NOW, "Nick: тру ляля");
	dm = ui_chat_find(&l, "Mama");
	pub = ui_chat_find(&l, "Public");
	grp = ui_chat_find(&l, "#zephcore");
	memset(&m, 0, sizeof(m));
	m.ts = NOW;
	strcpy(m.text, "ты где?");
	ui_chat_format_line(buf, sizeof(buf), dm, &m, NOW, 0);
	CHECK(std::string(buf) == ">> 12:34 Mama: ты где?");

	m.ts = YESTERDAY;
	strcpy(m.text, "Nick: привет, как дела?");
	ui_chat_format_line(buf, sizeof(buf), pub, &m, NOW, 0);
	CHECK(std::string(buf) == "18.11 12:34 Nick: привет, как дела?");

	m.ts = NOW;
	strcpy(m.text, "Nick: тру ляля");
	ui_chat_format_line(buf, sizeof(buf), grp, &m, NOW, 0);
	CHECK(std::string(buf) == "12:34 [#zephcore] Nick: тру ляля");
}

TEST(feed_emoji_substitution, "UNIT-FEED-007",
     "Emoji become text, unknown pictographs go, other multi-byte text stays")
{
	char buf[96];

	ui_feed_emoji_text(buf, sizeof(buf), "\xF0\x9F\x99\x82 hi");
	CHECK(std::string(buf) == ":) hi");
	ui_feed_emoji_text(buf, sizeof(buf), "SOS \xF0\x9F\x86\x98!");
	CHECK(std::string(buf) == "SOS SOS!!");
	ui_feed_emoji_text(buf, sizeof(buf), "\xE2\x9D\xA4\xEF\xB8\x8F you");
	CHECK(std::string(buf) == "<3 you");
	ui_feed_emoji_text(buf, sizeof(buf),
			    "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
	CHECK(std::string(buf) ==
	      "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
	ui_feed_emoji_text(buf, sizeof(buf), "a\xF0\x9F\xA6\x84z");
	CHECK(std::string(buf) == "az");
	ui_feed_emoji_text(buf, 4, "abcdef");
	CHECK(std::string(buf) == "abc");
	ui_feed_emoji_text(buf, 1, "abc");
	CHECK(std::string(buf) == "");
}

TEST(feed_wrap_rows, "UNIT-FEED-008",
     "Rows break at a space and never split a UTF-8 sequence")
{
	char row[64];
	const char *p;
	int n;

	p = "hello wide world";
	n = ui_feed_wrap_row(row, sizeof(row), &p, 11);
	CHECK(std::string(row) == "hello wide");
	CHECK(n == 10);
	n = ui_feed_wrap_row(row, sizeof(row), &p, 11);
	CHECK(std::string(row) == "world");
	CHECK(*p == '\0');

	p = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80";
	n = ui_feed_wrap_row(row, sizeof(row), &p, 6);
	CHECK(std::string(row) == "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
	CHECK(n == 12);
	n = ui_feed_wrap_row(row, sizeof(row), &p, 6);
	CHECK(std::string(row) == "\xD0\xBC\xD0\xB8\xD1\x80");

	p = "abcd\xD0\xB0\xD0\xB1\xD0\xB2\xD0\xB3\xD0\xB4\xD0\xB5";
	n = ui_feed_wrap_row(row, sizeof(row), &p, 5);
	CHECK(n == 4 + 2);
	CHECK((unsigned char)row[4] == 0xD0);
}

TEST(feed_wrap_leading_spaces, "UNIT-FEED-009",
     "A row of only spaces still makes progress")
{
	char row[64];
	const char *p = "     tail";
	int n;

	n = ui_feed_wrap_row(row, sizeof(row), &p, 3);
	CHECK(n <= 3);
	CHECK(*p != '\0');
	n = ui_feed_wrap_row(row, sizeof(row), &p, 16);
	CHECK(std::string(row) == "tail");
	CHECK(*p == '\0');
}

TEST(feed_utf8_truncated, "UNIT-FEED-010",
     "A lead byte cut off by the terminator never walks past it")
{
	char buf[32];

	CHECK(ui_feed_utf8_len("\xD0") == 1);
	CHECK(ui_feed_utf8_len("") == 0);
	CHECK(ui_feed_utf8_len("\xD0\x9F") == 2);
	CHECK(ui_feed_utf8_len("\xD0X") == 1);

	ui_feed_emoji_text(buf, sizeof(buf), "a\xD0");
	CHECK(std::string(buf) == "a\xD0");

	{
		char row[8];
		const char *p = "ab\xD0";

		(void)ui_feed_wrap_row(row, sizeof(row), &p, 8);
		CHECK(*p == '\0');
		CHECK(std::string(row) == "ab\xD0");
	}
}

TEST(feed_continuous_paging, "UNIT-FEED-011",
     "A page spills the overflow of a long message onto the next one")
{
	struct ui_chat_list l = fresh_list();
	struct ui_chat *c;
	char row[64];
	struct ui_chat_iter it;
	struct ui_feed_cursor start = { 0, 0 };
	int n = 0;

	(void)ui_chat_push(&l, "Public", true, NOW, "one");
	(void)ui_chat_push(&l, "Public", true, NOW + 1, "two");
	(void)ui_chat_push(&l, "Public", true, NOW + 2, "three");
	(void)ui_chat_push(&l, "Public", true, NOW + 3, "aaa bbb ccc");
	c = ui_chat_find(&l, "Public");
	CHECK(c != NULL);

	ui_chat_iter_init(&it, c, start, NOW, 0, 11);
	while (ui_chat_iter_next(&it, row, sizeof(row))) {
		n++;
	}
	/* "00:00 aaa bbb ccc" wraps to two rows at 11 columns; the other
	 * three lines take one each. */
	CHECK(n == 5);

	start = (struct ui_feed_cursor){ 0, 0 };
	ui_chat_iter_init(&it, c, start, NOW, 0, 11);
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(it.cur.entry == 1);
	CHECK(it.cur.row == 0);

	{
		struct ui_feed_cursor cur = { 1, 0 };
		struct ui_feed_cursor prev =
			ui_chat_page_before(c, cur, NOW, 0, 11, 2);

		CHECK(prev.entry == 0 && prev.row == 0);
	}
}
