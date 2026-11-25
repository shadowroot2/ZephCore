/* SPDX-License-Identifier: MIT */
/*
 * Host tests for the conversation feed: the per-chat ring and its newest
 * first order, the "HH:MM"/"+1d HH:MM"/"DD.MM HH:MM" line time at the
 * display offset, the ">>"/Public/"[#channel]" line shapes, the emoji
 * substitution table, the word wrap and the continuous paging that spills
 * a long message onto the next page.
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
		char exp[8];

		CHECK(c != NULL);
		CHECK(c->count == UI_CHAT_MSGS);
		CHECK(c->unread == UI_CHAT_MSGS);
		/* Newest first: the last pushed on top, the first at the
		 * bottom. */
		snprintf(exp, sizeof(exp), "m%d", UI_CHAT_MSGS - 1);
		CHECK(std::string(ui_chat_get(c, 0)->text) == exp);
		CHECK(std::string(ui_chat_get(c, UI_CHAT_MSGS - 1)->text) == "m0");
		CHECK(ui_chat_get(c, UI_CHAT_MSGS) == NULL);
	}
	/* Overflow evicts the oldest (m0), never drops the newest. */
	CHECK(ui_chat_push(&l, name, true, NOW + 99, "new") != NULL);
	{
		struct ui_chat *c = ui_chat_find(&l, name);
		char exp[8];

		CHECK(c->count == UI_CHAT_MSGS);
		CHECK(std::string(ui_chat_get(c, 0)->text) == "new");
		snprintf(exp, sizeof(exp), "m%d", UI_CHAT_MSGS - 1);
		CHECK(std::string(ui_chat_get(c, 1)->text) == exp);
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
     "Line time is HH:MM today, +1d HH:MM yesterday, DD.MM HH:MM earlier")
{
	char buf[32];

	ui_feed_format_time(buf, sizeof(buf), EARLIER_TODAY, NOW, 0);
	CHECK(std::string(buf) == "10:00");
	ui_feed_format_time(buf, sizeof(buf), NOW, NOW, 0);
	CHECK(std::string(buf) == "12:34");
	ui_feed_format_time(buf, sizeof(buf), YESTERDAY, NOW, 0);
	CHECK(std::string(buf) == "+1d 12:34");

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
	/* One local day apart is "+1d"; further back keeps its date, even
	 * below the epoch. */
	ui_feed_format_time(buf, sizeof(buf), 1000, 50000, -720);
	CHECK(std::string(buf) == "+1d 12:16");
	ui_feed_format_time(buf, sizeof(buf), 1000, 50000 + 2 * 86400, -720);
	CHECK(std::string(buf) == "31.12 12:16");
}

TEST(feed_line_shapes, "UNIT-FEED-006",
     "A message reads as time, then text")
{
	char buf[192];
	struct ui_chat_list l = fresh_list();
	struct ui_chat *dm;
	struct ui_chat_msg m;

	(void)ui_chat_push(&l, "Mama", false, NOW, "ты где?");
	dm = ui_chat_find(&l, "Mama");
	memset(&m, 0, sizeof(m));

	/* The row is just the time; the text is its own rows below it. */
	m.ts = NOW;
	strcpy(m.text, "ты где?");
	ui_chat_format_line(buf, sizeof(buf), dm, &m, NOW, 0);
	CHECK(std::string(buf) == "12:34");

	m.ts = YESTERDAY;
	ui_chat_format_line(buf, sizeof(buf), dm, &m, NOW, 0);
	CHECK(std::string(buf) == "+1d 12:34");
}

TEST(feed_text_cleanup, "UNIT-FEED-012",
     "A nickname keeps no smiley")
{
	char buf[192];

	/* The "<name>: " prefix the mesh puts on a channel message loses its
	 * pictographs; the map link loses its host. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "\xF0\x9F\x99\x82Mama: привет");
	CHECK(std::string(buf) == "Mama: привет");

	/* A plain message is left alone. */
	ui_chat_clean_text(buf, sizeof(buf), "просто текст");
	CHECK(std::string(buf) == "просто текст");

	ui_chat_clean_text(buf, sizeof(buf), "Nick: два слова");
	CHECK(std::string(buf) == "Nick: два слова");
}

TEST(feed_emoji_substitution, "UNIT-FEED-007",
     "Emoji become text, unknown pictographs leave a mark, other text stays")
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
	CHECK(std::string(buf) == "a\xE2\x80\xA6z");	/* a…z */
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

	/* Channel messages carry their sender as the "<name>: " prefix. */
	(void)ui_chat_push(&l, "Public", true, NOW, "Nick: one");
	(void)ui_chat_push(&l, "Public", true, NOW + 1, "Nick: two");
	(void)ui_chat_push(&l, "Public", true, NOW + 2, "Nick: three");
	(void)ui_chat_push(&l, "Public", true, NOW + 3,
			   "Nick: aaa bbb ccc ddd");
	c = ui_chat_find(&l, "Public");
	CHECK(c != NULL);

	ui_chat_iter_init(&it, c, start, NOW, 0, 11);
	while (ui_chat_iter_next(&it, row, sizeof(row))) {
		n++;
	}
	/* A channel message is its head line and the body.  The newest body
	 * wraps to two rows at 11 columns, so it is three rows and the other
	 * three are two each: 9 in all. */
	CHECK(n == 9);

	/* A page of two rows starting at the top: the head, then the first
	 * line of the body. */
	start = (struct ui_feed_cursor){ 0, 0 };
	ui_chat_iter_init(&it, c, start, NOW, 0, 11);
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "12:34 Nick");
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "aaa bbb");
	CHECK(it.cur.entry == 0);
	CHECK(it.cur.row == 2);
}

TEST(feed_private_one_line, "UNIT-FEED-013",
     "A direct chat carries its time on the text's own line")
{
	struct ui_chat_list l = fresh_list();
	struct ui_chat *c;
	char row[64];
	struct ui_chat_iter it;
	struct ui_feed_cursor start = { 0, 0 };

	(void)ui_chat_push(&l, "Mama", false, NOW, "ты где?");
	c = ui_chat_find(&l, "Mama");
	CHECK(c != NULL);

	ui_chat_iter_init(&it, c, start, NOW, 0, 21);
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "12:34 ты где?");
	CHECK(!ui_chat_iter_next(&it, row, sizeof(row)));
}

TEST(feed_seek_inside_message, "UNIT-FEED-014",
     "Opening a chat at a row past the first never spins")
{
	struct ui_chat_list l = fresh_list();
	struct ui_chat *c;
	char row[64];
	struct ui_chat_iter it;
	struct ui_feed_cursor cur = { 0, 2 };

	(void)ui_chat_push(&l, "Public", true, NOW, "Nick: aaa bbb ccc ddd");
	c = ui_chat_find(&l, "Public");
	CHECK(c != NULL);

	/* Row 2 of the newest message: the head is row 0 and "aaa bbb" is
	 * row 1, so this is the "ccc ddd" line.  Before the fix the skip
	 * loop advanced the very bound it was counting to and never ended. */
	ui_chat_iter_init(&it, c, cur, NOW, 0, 11);
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "ccc ddd");
}

TEST(feed_coords_only, "UNIT-FEED-015",
     "A message with map links keeps only the coordinates, one per row")
{
	char buf[192];

	/* One link: the message becomes just the pair. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "тут https://maps.google.com/?q=55.75580,37.61730 конец");
	CHECK(std::string(buf) == "55.75580,37.61730");

	/* Two links: two rows, split at a newline. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "https://maps.google.com/?q=55.7,37.6 и "
			   "https://maps.google.com/?q=55.8,37.7");
	CHECK(std::string(buf) == "55.7,37.6\n55.8,37.7");

	/* The deferred tracking form carries its own link. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "\xF0\x95\x95\x93 12:34 \xF0\x9F\x93\x8D "
			   "https://maps.google.com/?q=1.25000,2.50000");
	CHECK(std::string(buf) == "1.25000,2.50000");

	/* A message without a link is untouched. */
	ui_chat_clean_text(buf, sizeof(buf), "просто текст");
	CHECK(std::string(buf) == "просто текст");
}

TEST(feed_row_breaks_at_newline, "UNIT-FEED-016",
     "Two coordinates in one message stay on separate rows")
{
	char row[64];
	const char *p = "55.7,37.6\n55.8,37.7";

	CHECK(ui_feed_wrap_row(row, sizeof(row), &p, 21) > 0);
	CHECK(std::string(row) == "55.7,37.6");
	CHECK(ui_feed_wrap_row(row, sizeof(row), &p, 21) > 0);
	CHECK(std::string(row) == "55.8,37.7");
	CHECK(*p == '\0');
}

TEST(feed_head_fits_one_row, "UNIT-FEED-017",
     "A long sender is cut to the row, never wrapped onto the text")
{
	struct ui_chat_list l = fresh_list();
	struct ui_chat *c;
	char row[64];
	struct ui_chat_iter it;
	struct ui_feed_cursor start = { 0, 0 };

	(void)ui_chat_push(&l, "Public", true, NOW,
			   "\xF0\x9F\x99\x82VeryLongNicknameHere: текст");
	c = ui_chat_find(&l, "Public");
	CHECK(c != NULL);

	ui_chat_iter_init(&it, c, start, NOW, 0, 12);
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(it.row_is_head);
	/* The head is one row of 12 columns and no smiley survives it. */
	CHECK(std::string(row) == "12:34 VeryLo");
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "\xD1\x82\xD0\xB5\xD0\xBA\xD1\x81\xD1\x82");
}

TEST(feed_page_wraps_not_past_end, "UNIT-FEED-018",
     "Paging past the last row wraps to the newest, never past the chat")
{
	struct ui_chat_list l = fresh_list();
	struct ui_chat *c;
	char row[64];
	struct ui_chat_iter it;
	struct ui_feed_cursor cur = { 0, 0 };
	int pages = 0;

	(void)ui_chat_push(&l, "Public", true, NOW, "Nick: msg2");
	(void)ui_chat_push(&l, "Public", true, NOW + 1, "Nick: msg1");
	(void)ui_chat_push(&l, "Public", true, NOW + 2, "Nick: msg0");
	c = ui_chat_find(&l, "Public");
	CHECK(c != NULL);

	/* Walk the chat in screens of five rows and record where each page
	 * lands.  Before the fix the last page's pull of the closing row
	 * left the cursor past the end and the next render drew nothing. */
	while (pages < 8) {
		ui_chat_iter_init(&it, c, cur, NOW, 0, 11);
		for (int i = 0; i < 5; i++) {
			if (!ui_chat_iter_next(&it, row, sizeof(row))) {
				break;
			}
		}
		if (it.cur.entry >= c->count) {
			/* Past the end: the pager must wrap. */
			cur = (struct ui_feed_cursor){ 0, 0 };
			break;
		}
		cur = it.cur;
		pages++;
	}
	CHECK(cur.entry == 0 && cur.row == 0);
	CHECK(pages < 8);
}

TEST(feed_name_matching, "UNIT-FEED-019",
     "A message finds the conversation its nickname already opened")
{
	struct ui_chat_list l = fresh_list();
	struct ui_chat *seeded;
	struct ui_chat *landed;

	/* The list is seeded from the contact book under the cleaned name. */
	seeded = ui_chat_ensure(&l, "\xF0\x9F\x99\x82 ShadoW T1000-E", false);
	CHECK(seeded != NULL);
	CHECK(std::string(seeded->name) == "ShadoW T1000-E");
	CHECK(ui_chat_list_len(&l) == 1);

	/* A message from the same node carries the same nickname with its
	 * smiley: it must land in that conversation, not open a second one. */
	landed = ui_chat_push(&l, "\xF0\x9F\x99\x82 ShadoW T1000-E", false,
			      NOW, "привет");
	CHECK(landed == seeded);
	CHECK(ui_chat_list_len(&l) == 1);
	CHECK(landed->count == 1);
	CHECK(landed->unread == 1);
}

TEST(feed_name_gap_trimmed, "UNIT-FEED-020",
     "A nickname loses the smiley and the gap it left behind")
{
	char buf[64];

	/* "🙂 ShadoW" -> "ShadoW", not " ShadoW": the head line would
	 * otherwise read "17:22  ShadoW" with two spaces. */
	ui_feed_strip_pictographs(buf, sizeof(buf),
				  "\xF0\x9F\x99\x82 ShadoW T1000-E");
	CHECK(std::string(buf) == "ShadoW T1000-E");

	ui_feed_strip_pictographs(buf, sizeof(buf),
				  "\xF0\x9F\x99\x82ShadoW T1000-E");
	CHECK(std::string(buf) == "ShadoW T1000-E");

	ui_feed_strip_pictographs(buf, sizeof(buf), "  Mama  ");
	CHECK(std::string(buf) == "Mama");
}

TEST(feed_coords_keep_sender, "UNIT-FEED-021",
     "A channel message with coordinates keeps its sender in the header")
{
	char buf[192];
	struct ui_chat_list l = fresh_list();
	struct ui_chat *c;
	char row[64];
	struct ui_chat_iter it;
	struct ui_feed_cursor start = { 0, 0 };

	/* The "<name>: " prefix survives the coordinate squeeze, or the
	 * head row has no sender to name. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "Nick: https://maps.google.com/?q=55.75580,37.61730");
	CHECK(std::string(buf) == "Nick: 55.75580,37.61730");

	/* Two links: two rows of coordinates, name kept once. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "Nick: https://maps.google.com/?q=55.7,37.6 и "
			   "https://maps.google.com/?q=55.8,37.7");
	CHECK(std::string(buf) == "Nick: 55.7,37.6\n55.8,37.7");

	/* A message without a name prefix stays coordinates only. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "тут https://maps.google.com/?q=55.75580,37.61730");
	CHECK(std::string(buf) == "55.75580,37.61730");

	/* The head row names the sender like on any other message. */
	(void)ui_chat_push(&l, "#tracks", true, NOW,
			   "Nick: https://maps.google.com/?q=55.75580,37.61730");
	c = ui_chat_find(&l, "#tracks");
	CHECK(c != NULL);
	ui_chat_iter_init(&it, c, start, NOW, 0, 21);
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(it.row_is_head);
	CHECK(std::string(row) == "12:34 Nick");
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(!it.row_is_head);
	CHECK(std::string(row) == "55.75580,37.61730");
}

TEST(feed_coords_motion, "UNIT-FEED-022",
     "A tracking report keeps its speed and sector beside the coordinates")
{
	char buf[192];

	/* The wire line "🏃 12 км/ч ↗️ СВ\n📍 <link>" is motion and point:
	 * the pictographs go, the speed and the sector stay. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "🏃 12 км/ч ↗️ СВ\n"
			   "📍 https://maps.google.com/?q=55.75580,37.61730");
	CHECK(std::string(buf) == "12км/ч СВ 55.75580,37.61730");

	/* The sender keeps its place ahead of the motion. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "Nick: 🏃 6 км/ч ⬆️ Ю\n"
			   "📍 https://maps.google.com/?q=55.75580,37.61730");
	CHECK(std::string(buf) == "Nick: 6км/ч Ю 55.75580,37.61730");

	/* A standstill carries no speed and no sector to keep. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "🅿️\n"
			   "📍 https://maps.google.com/?q=55.75580,37.61730");
	CHECK(std::string(buf) == "55.75580,37.61730");

	/* "15 км/ч)" is a trip summary's parenthetical, not a motion. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "🏁 12.4км (10.2 / 15 км/ч)\n⏱️ 2ч15м\n🚶 1ч48м\n"
			   "🅿️ 30 мин (3)\n"
			   "📍 https://maps.google.com/?q=55.75580,37.61730");
	CHECK(std::string(buf) == "55.75580,37.61730");

	/* A whole word is not the СВ sector. */
	ui_chat_clean_text(buf, sizeof(buf),
			   "12 км/ч СВЕТ иду "
			   "https://maps.google.com/?q=55.75580,37.61730");
	CHECK(std::string(buf) == "55.75580,37.61730");
}

TEST(feed_own_mark, "UNIT-FEED-023",
     "An own message wears the Я head with its outcome right-aligned")
{
	struct ui_chat_list l = fresh_list();
	struct ui_chat *c;
	char row[64];
	struct ui_chat_iter it;
	struct ui_feed_cursor start = { 0, 0 };

	(void)ui_chat_push(&l, "Public", true, NOW, "Nick: чужое");
	(void)ui_chat_push(&l, "Public", true, NOW + 1, "Я: привет");
	(void)ui_chat_push(&l, "Public", true, NOW + 2, "Я: пока");
	c = ui_chat_find(&l, "Public");
	CHECK(c != NULL);

	CHECK(ui_chat_set_own(c, NOW + 1));
	CHECK(ui_chat_set_own(c, NOW + 2));
	CHECK(ui_chat_set_mark(c, NOW + 1, UI_MSG_MARK_HEARD, 3));
	CHECK(ui_chat_set_mark(c, NOW + 2, UI_MSG_MARK_DELIVERED, 0));
	/* An unknown timestamp (already evicted) changes nothing. */
	CHECK(!ui_chat_set_mark(c, NOW + 99, UI_MSG_MARK_FAILED, 0));

	/* Rows at 21 columns: "12:34 Я" on the left and the outcome at the
	 * row's end. */
	ui_chat_iter_init(&it, c, start, NOW, 0, 21);
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "12:34 Я            +1");
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "\xD0\xBF\xD0\xBE\xD0\xBA\xD0\xB0");
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "12:34 Я            +3");
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "12:34 Nick");
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "\xD1\x87\xD1\x83\xD0\xB6\xD0\xBE\xD0\xB5");
	CHECK(!ui_chat_iter_next(&it, row, sizeof(row)));

	/* An unheard direct message reads "x" at the row's end, and its
	 * text sits below the "Я" head like a channel one. */
	(void)ui_chat_push(&l, "Mama", false, NOW, "Я: где ты?");
	c = ui_chat_find(&l, "Mama");
	CHECK(c != NULL);
	CHECK(ui_chat_set_own(c, NOW));
	CHECK(ui_chat_set_mark(c, NOW, UI_MSG_MARK_FAILED, 0));
	start = (struct ui_feed_cursor){ 0, 0 };
	ui_chat_iter_init(&it, c, start, NOW, 0, 21);
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "12:34 Я             x");
	CHECK(ui_chat_iter_next(&it, row, sizeof(row)));
	CHECK(std::string(row) == "\xD0\xB3\xD0\xB4\xD0\xB5 \xD1\x82\xD1\x8B?");
	CHECK(!ui_chat_iter_next(&it, row, sizeof(row)));
}
