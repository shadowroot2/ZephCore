/* SPDX-License-Identifier: MIT */
#pragma once

/*
 * UiFeed — the PUBLIC message feed for the button UI: the newest message at
 * the top of the list, older ones below it, each line carrying local time,
 * the sender and the text. A direct message is marked with ">>" ahead of the
 * time; the public channel is not tagged, any other channel is.
 *
 * Pure logic: the UI owns the storage and the rendering, this header owns the
 * ring, the line format, the emoji substitution and the redraw decision.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <helpers/tz_offset.h>

/* Same bound as BaseChatMesh's MAX_TEXT_LEN (10*CIPHER_BLOCK_SIZE = 160):
 * a feed entry never truncates what the mesh accepted.  Spelled out rather
 * than pulled from mesh/MeshCore.h, which is C++-only and this header is
 * included from C. */
#define UI_FEED_TEXT_LEN 160
/* ContactInfo::name is 32 bytes; the feed keeps the sender whole. */
#define UI_FEED_NAME_LEN 32

#ifndef UI_FEED_SLOTS
#ifdef CONFIG_ZEPHCORE_UI_FEED_COUNT
#define UI_FEED_SLOTS CONFIG_ZEPHCORE_UI_FEED_COUNT
#else
#define UI_FEED_SLOTS 10
#endif
#endif

#define UI_FEED_F_DM 0x01u /* direct message: rendered with ">>" */

/* Channel names are short in practice ("Public", "#zephcore"); the feed keeps
 * enough to tag the line. */
#define UI_FEED_CHANNEL_LEN 16

/* ---- time ---- */

/* Local day number of a unix timestamp at the given offset.  Floor, not
 * C truncation: an epoch-second clock plus a negative offset lands below
 * zero, and truncation would file that day under day 0. */
static inline int32_t ui_feed_local_day(uint32_t ts, int16_t tz_minutes)
{
	int64_t local = (int64_t)ts + (int64_t)tz_minutes * 60;

	return (int32_t)((local - (local < 0 ? 86399 : 0)) / 86400);
}

/* "HH:MM" today, "DD.MM HH:MM" once the message is from an earlier local
 * day, "--:--" when the sender carried no timestamp. */
static inline void ui_feed_format_time(char *buf, size_t n, uint32_t ts,
				       uint32_t now, int16_t tz_minutes)
{
	int64_t local;
	uint32_t day_sec;

	if (ts == 0) {
		snprintf(buf, n, "--:--");
		return;
	}
	local = (int64_t)ts + (int64_t)tz_minutes * 60;
	/* 86400 is the day length; the offset cannot push a timestamp past the
	 * int64 range here. */
	day_sec = (uint32_t)(((local % 86400) + 86400) % 86400);

	if (ui_feed_local_day(ts, tz_minutes) == ui_feed_local_day(now, tz_minutes)) {
		snprintf(buf, n, "%02u:%02u", day_sec / 3600U,
			 (day_sec % 3600U) / 60U);
	} else {
		/* Day-of-month and month of the local date. */
		int32_t days = ui_feed_local_day(ts, tz_minutes);
		int32_t z = days + 719468; /* days from 0000-03-01 */
		int32_t era = (z >= 0 ? z : z - 146096) / 146097;
		uint32_t doe = (uint32_t)(z - era * 146097);
		uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
		uint32_t mp = (5 * doy + 2) / 153;
		uint32_t d = doy - (153 * mp + 2) / 5 + 1;
		uint32_t m = mp + (mp < 10 ? 3 : -9);

		snprintf(buf, n, "%02u.%02u %02u:%02u", d, m,
			 day_sec / 3600U, (day_sec % 3600U) / 60U);
	}
}


/* ---- emoji ---- */

struct ui_feed_emoji {
	const char *utf8;
	const char *ascii;
};

/* Our own automatic messages lead with these; anything else worth keeping
 * can join the table as it is missed. */
static const struct ui_feed_emoji ui_feed_emoji_table[] = {
	{ "\xF0\x9F\x99\x82", ":)" },	/* 🙂 */
	{ "\xF0\x9F\x99\x83", ":)" },	/* 🙃 */
	{ "\xF0\x9F\x98\x8A", ":)" },	/* 😊 */
	{ "\xE2\x98\xBA", ":)" },	/* ☺ */
	{ "\xF0\x9F\x98\x84", ":D" },	/* 😄 */
	{ "\xF0\x9F\x98\x80", ":D" },	/* 😀 */
	{ "\xF0\x9F\x98\x81", ":D" },	/* 😁 */
	{ "\xF0\x9F\x98\x83", ":D" },	/* 😃 */
	{ "\xF0\x9F\x98\x82", "xD" },	/* 😂 */
	{ "\xF0\x9F\xA4\xA3", "xD" },	/* 🤣 */
	{ "\xF0\x9F\x98\x85", ":S" },	/* 😅 */
	{ "\xF0\x9F\x98\xAC", ":S" },	/* 😬 */
	{ "\xF0\x9F\x98\x93", ":S" },	/* 😓 */
	{ "\xF0\x9F\x98\x89", ";)" },	/* 😉 */
	{ "\xF0\x9F\x98\x9B", ":P" },	/* 😛 */
	{ "\xF0\x9F\x98\x9C", ":P" },	/* 😜 */
	{ "\xF0\x9F\x98\x8B", ":P" },	/* 😋 */
	{ "\xF0\x9F\x98\x8F", ":P" },	/* 😏 */
	{ "\xF0\x9F\x98\x98", ":*" },	/* 😘 */
	{ "\xF0\x9F\x98\x97", ":*" },	/* 😗 */
	{ "\xF0\x9F\x98\x9A", ":*" },	/* 😚 */
	{ "\xF0\x9F\x98\x8D", "<3" },	/* 😍 */
	{ "\xF0\x9F\xA5\xB0", "<3" },	/* 🥰 */
	{ "\xE2\x9D\xA4", "<3" },	/* ❤ */
	{ "\xF0\x9F\xA9\xB7", "<3" },	/* 🩷 */
	{ "\xF0\x9F\x92\x95", "<3" },	/* 💕 */
	{ "\xF0\x9F\x92\x96", "<3" },	/* 💖 */
	{ "\xF0\x9F\x92\x97", "<3" },	/* 💗 */
	{ "\xF0\x9F\x92\x93", "<3" },	/* 💓 */
	{ "\xF0\x9F\x92\x9E", "<3" },	/* 💞 */
	{ "\xF0\x9F\x98\xA2", ":(" },	/* 😢 */
	{ "\xF0\x9F\x98\x9E", ":(" },	/* 😞 */
	{ "\xF0\x9F\x98\x94", ":(" },	/* 😔 */
	{ "\xF0\x9F\x99\x81", ":(" },	/* 🙁 */
	{ "\xE2\x98\xB9", ":(" },	/* ☹ */
	{ "\xF0\x9F\x98\xAD", ":'(" },	/* 😭 */
	{ "\xF0\x9F\x98\xA4", ">:(" },	/* 😤 */
	{ "\xF0\x9F\x98\xA0", ">:(" },	/* 😠 */
	{ "\xF0\x9F\x98\xA1", ">:(" },	/* 😡 */
	{ "\xF0\x9F\xA4\xAC", ">:(" },	/* 🤬 */
	{ "\xF0\x9F\x98\xB1", "D:" },	/* 😱 */
	{ "\xF0\x9F\x98\xA8", "D:" },	/* 😨 */
	{ "\xF0\x9F\x98\xB0", "D:" },	/* 😰 */
	{ "\xF0\x9F\x98\x8E", "B)" },	/* 😎 */
	{ "\xF0\x9F\x98\x90", ":|" },	/* 😐 */
	{ "\xF0\x9F\x98\x91", ":|" },	/* 😑 */
	{ "\xF0\x9F\x98\xB6", ":|" },	/* 😶 */
	{ "\xF0\x9F\x98\xB4", "zZz" },	/* 😴 */
	{ "\xF0\x9F\xA4\xA4", "zZz" },	/* 🤤 */
	{ "\xF0\x9F\x98\xAA", "zZz" },	/* 😪 */
	{ "\xF0\x9F\x98\xAE", ":O" },	/* 😮 */
	{ "\xF0\x9F\x98\xAF", ":O" },	/* 😯 */
	{ "\xF0\x9F\x98\xA7", ":O" },	/* 😧 */
	{ "\xF0\x9F\x92\x94", "</3" },	/* 💔 */
	{ "\xF0\x9F\x91\x8D", "+1" },	/* 👍 */
	{ "\xF0\x9F\x91\x8E", "-1" },	/* 👎 */
	{ "\xF0\x9F\x92\xAF", "100" },	/* 💯 */
	{ "\xE2\x9C\x85", "v" },	/* ✅ */
	{ "\xE2\x9D\x8C", "x" },	/* ❌ */
	{ "\xE2\x9A\xA0", "!" },	/* ⚠ */
	{ "\xE2\x9D\x97", "!!" },	/* ❗ */
	{ "\xE2\x9D\x93", "?" },	/* ❓ */
	{ "\xE2\xAD\x90", "*" },	/* ⭐ */
	{ "\xE2\x9C\xA8", "*" },	/* ✨ */
	{ "\xF0\x9F\x8C\x9F", "*" },	/* 🌟 */
	{ "\xF0\x9F\x92\xAB", "*" },	/* 💫 */
	{ "\xF0\x9F\x86\x98", "SOS!" },	/* 🆘 */
	{ "\xF0\x9F\xA5\xBA", "*pls*" },	/* 🥺 */
	{ "\xF0\x9F\xA4\xA2", "*sick*" },	/* 🤢 */
	{ "\xF0\x9F\xA4\xAE", "*sick*" },	/* 🤮 */
	{ "\xF0\x9F\x98\xB7", "*ill*" },	/* 😷 */
	{ "\xF0\x9F\xA4\x92", "*ill*" },	/* 🤒 */
	{ "\xF0\x9F\xA4\x95", "*ill*" },	/* 🤕 */
	{ "\xF0\x9F\xA5\xB3", "*party*" },	/* 🥳 */
	{ "\xF0\x9F\x8E\x89", "*party*" },	/* 🎉 */
	{ "\xF0\x9F\x8E\x8A", "*party*" },	/* 🎊 */
	{ "\xF0\x9F\xA4\xAF", "*boom*" },	/* 🤯 */
	{ "\xF0\x9F\xA4\x94", "*think*" },	/* 🤔 */
	{ "\xF0\x9F\xA7\x90", "*think*" },	/* 🧐 */
	{ "\xF0\x9F\x99\x8F", "*pray*" },	/* 🙏 */
	{ "\xF0\x9F\x92\xAA", "*strong*" },	/* 💪 */
	{ "\xF0\x9F\x91\x8B", "*wave*" },	/* 👋 */
	{ "\xF0\x9F\x91\x8C", "*ok*" },	/* 👌 */
	{ "\xE2\x9C\x8A", "*fist*" },	/* ✊ */
	{ "\xF0\x9F\xA4\x9B", "*fist*" },	/* 🤛 */
	{ "\xF0\x9F\xA4\x9C", "*fist*" },	/* 🤜 */
	{ "\xF0\x9F\xA4\x9D", "*deal*" },	/* 🤝 */
	{ "\xF0\x9F\x8E\xAF", "*aim*" },	/* 🎯 */
	{ "\xF0\x9F\x8F\x86", "*win*" },	/* 🏆 */
	{ "\xF0\x9F\xA5\x87", "*win*" },	/* 🥇 */
	{ "\xF0\x9F\x8E\x81", "*gift*" },	/* 🎁 */
	{ "\xF0\x9F\x94\xA5", "*fire*" },	/* 🔥 */
	{ "\xF0\x9F\x92\xA1", "*idea*" },	/* 💡 */
	{ "\xF0\x9F\x94\x94", "*bell*" },	/* 🔔 */
	{ "\xF0\x9F\x94\x8B", "*bat*" },	/* 🔋 */
	{ "\xF0\x9F\x93\x8D", "*map*" },	/* 📍 */
	{ "\xF0\x9F\x9B\xB0", "*sat*" },	/* 🛰 */
	{ "\xF0\x9F\x8C\xA1", "*t*" },	/* 🌡 */
	{ "\xF0\x9F\x9A\x80", "*go*" },	/* 🚀 */
	{ "\xF0\x9F\x9B\x91", "*stop*" },	/* 🛑 */
	{ "\xF0\x9F\x8F\x83", "*run*" },	/* 🏃 */
	{ "\xF0\x9F\x95\x93", "*time*" },	/* 🕓 */
	{ "\xE2\x8F\xB0", "*time*" },	/* ⏰ */
	{ "\xE2\x8F\xB1", "*time*" },	/* ⏱ */
	{ "\xE2\x8C\x9A", "*time*" },	/* ⌚ */
	{ "\xF0\x9F\x94\x91", "*key*" },	/* 🔑 */
	{ "\xF0\x9F\x94\x92", "*lock*" },	/* 🔒 */
	{ "\xF0\x9F\x94\x93", "*open*" },	/* 🔓 */
	{ "\xF0\x9F\xA9\xB9", "*med*" },	/* 🩹 */
	{ "\xF0\x9F\x92\x8A", "*med*" },	/* 💊 */
	{ "\xF0\x9F\x93\xB1", "*phone*" },	/* 📱 */
	{ "\xF0\x9F\x92\xBB", "*pc*" },	/* 💻 */
	{ NULL, NULL },
};

/* Variation selectors draw nothing on a bitmap display: U+FE0E (text) and
 * U+FE0F (emoji). Drop them so a substitution leaves no dangling bytes —
 * "❤️" is "❤" + U+FE0F and must not outlive its base. */
#define UI_FEED_VS_EMOJI "\xEF\xB8\x8F"
#define UI_FEED_VS_TEXT  "\xEF\xB8\x8E"

/* Length of the complete UTF-8 sequence starting at s (1..4), 0 when s is
 * empty. A truncated sequence (a lead byte whose continuations are missing
 * or cut short by the terminator) counts as one byte, so no caller walks
 * past the NUL chasing a length the string does not have. */
static inline size_t ui_feed_utf8_len(const char *s)
{
	unsigned char c = (unsigned char)s[0];
	size_t want;

	if (c == 0) {
		return 0;
	}
	if (c < 0x80) {
		return 1;
	}
	if ((c & 0xE0) == 0xC0) {
		want = 2;
	} else if ((c & 0xF0) == 0xE0) {
		want = 3;
	} else if ((c & 0xF8) == 0xF0) {
		want = 4;
	} else {
		return 1; /* a stray continuation byte: pass it through */
	}
	for (size_t k = 1; k < want; k++) {
		if (s[k] == '\0' || ((unsigned char)s[k] & 0xC0) != 0x80) {
			return 1; /* truncated or malformed: take the lead only */
		}
	}
	return want;
}

/* Copy text into dst, substituting emoji the display fonts cannot draw. An
 * unknown pictograph (a 4-byte sequence in the emoji plane) is dropped whole
 * rather than left as mojibake; every other multi-byte character is kept, so
 * Cyrillic and accented text survive. dst is always NUL-terminated. */
static inline void ui_feed_emoji_text(char *dst, size_t n, const char *src)
{
	size_t out = 0;

	if (n == 0) {
		return;
	}
	while (*src != '\0' && out + 1 < n) {
		size_t i;

		for (i = 0; ui_feed_emoji_table[i].utf8 != NULL; i++) {
			size_t m = strlen(ui_feed_emoji_table[i].utf8);

			if (strncmp(src, ui_feed_emoji_table[i].utf8, m) != 0) {
				continue;
			}
			{
				size_t r = strlen(ui_feed_emoji_table[i].ascii);

				if (out + r + 1 > n) {
					dst[out] = '\0';
					return;
				}
				memcpy(dst + out, ui_feed_emoji_table[i].ascii, r);
				out += r;
			}
			src += m;
			break;
		}
		if (ui_feed_emoji_table[i].utf8 != NULL) {
			continue; /* a substitution happened */
		}
		{
			size_t m = ui_feed_utf8_len(src);

			/* A variation selector has no glyph of its own. */
			if (m == 3 && (strncmp(src, UI_FEED_VS_EMOJI, 3) == 0 ||
				       strncmp(src, UI_FEED_VS_TEXT, 3) == 0)) {
				src += 3;
				continue;
			}
			/* U+1F000.. is the pictograph plane: an emoji with no
			 * text of its own leaves a mark rather than vanish. */
			if (m == 4) {
				if (out + 4 <= n) {
					memcpy(dst + out, "\xE2\x80\xA6", 3);
					out += 3;
				}
				src += m;
				continue;
			}
			if (out + m + 1 > n) {
				break;
			}
			memcpy(dst + out, src, m);
			out += m;
			src += m;
		}
	}
	dst[out] = '\0';
}

/* Copy the first display row of `*src` into dst (at most `cols` code
 * points wide), breaking at a space when there is one, and advance *src
 * past the row. A multi-byte UTF-8 sequence is never split: one sequence
 * is one display column, since the font maps code points. Returns the
 * row length in bytes. */
static inline int ui_feed_wrap_row(char *dst, size_t dst_n, const char **src,
				   int cols)
{
	const char *s = *src;
	int nbytes = 0;
	int ncols = 0;
	int last_space_bytes = -1;

	if (dst_n == 0) {
		*src = s + strlen(s);
		return 0;
	}
	while (*s != '\0' && ncols < cols && (size_t)nbytes + 1 < dst_n) {
		size_t adv;

		if (*s == '\n') {
			/* A hard break: two coordinates in one message stay
			 * on separate rows. */
			s++;
			break;
		}
		adv = ui_feed_utf8_len(s);
		if (adv == 0) {
			break;
		}
		if ((size_t)nbytes + adv >= dst_n) {
			break;
		}
		if (*s == ' ') {
			last_space_bytes = nbytes;
		}
		for (size_t k = 0; k < adv && s[k] != '\0'; k++) {
			dst[nbytes++] = s[k];
		}
		s += adv;
		ncols++;
	}
	if (*s != '\0' && last_space_bytes >= 0) {
		nbytes = last_space_bytes;
		s = *src + nbytes + 1;
		if (nbytes == 0) {
			/* A break at offset 0 means the row is only spaces:
			 * drop one so the cursor always advances. */
			s = *src + 1;
		}
	}
	dst[nbytes] = '\0';
	while (*s == ' ') {
		s++;
	}
	*src = s;
	return nbytes;
}


/* ---- conversations ---- */

/* One conversation — a channel or a direct chat — with its own small ring.
 * The list screen shows the names, entering one shows its messages. */
struct ui_chat_msg {
	uint32_t ts;			/* sender timestamp (unix seconds), 0 = unknown */
	char text[UI_FEED_TEXT_LEN + 1];
};

#ifndef UI_CHAT_MSGS
#ifdef CONFIG_ZEPHCORE_UI_CHAT_MSGS
#define UI_CHAT_MSGS CONFIG_ZEPHCORE_UI_CHAT_MSGS
#else
#define UI_CHAT_MSGS 4
#endif
#endif

#ifndef UI_CHAT_COUNT
#ifdef CONFIG_ZEPHCORE_UI_CHAT_COUNT
#define UI_CHAT_COUNT CONFIG_ZEPHCORE_UI_CHAT_COUNT
#else
#define UI_CHAT_COUNT 6
#endif
#endif

struct ui_chat {
	char name[UI_FEED_NAME_LEN];
	bool is_group;			/* a channel, not a direct chat */
	uint16_t unread;		/* messages since the last visit */
	uint8_t head;			/* index of the newest message */
	uint8_t count;			/* messages kept, 0..UI_CHAT_MSGS */
	struct ui_chat_msg msgs[UI_CHAT_MSGS];
};

struct ui_chat_list {
	struct ui_chat chats[UI_CHAT_COUNT];
	uint8_t count;
};

/* A position in one chat's wrapped rows: `entry` is the message (0 = newest)
 * and `row` is the wrapped line within it.  The chat reads as one continuous
 * text, so a message longer than the panel spills onto the next page. */
struct ui_feed_cursor {
	uint8_t entry;
	uint16_t row;
};

static inline bool ui_feed_cursor_before(struct ui_feed_cursor a,
					 struct ui_feed_cursor b)
{
	return a.entry < b.entry ||
	       (a.entry == b.entry && a.row < b.row);
}

/* Newest first: 0 is the newest, count-1 the oldest. NULL past the end. */
static inline const struct ui_chat_msg *ui_chat_get(const struct ui_chat *c,
						    uint8_t i)
{
	if (i >= c->count) {
		return NULL;
	}
	return &c->msgs[(uint8_t)((c->head + UI_CHAT_MSGS - i) % UI_CHAT_MSGS)];
}

static inline void ui_chat_reset(struct ui_chat *c)
{
	memset(c, 0, sizeof(*c));
}

static inline void ui_chat_list_reset(struct ui_chat_list *l)
{
	memset(l, 0, sizeof(*l));
}

/* The chat carrying `name`, or NULL. */
static inline struct ui_chat *ui_chat_find(struct ui_chat_list *l,
					   const char *name)
{
	for (uint8_t i = 0; i < l->count; i++) {
		if (strncmp(l->chats[i].name, name, sizeof(l->chats[i].name))
		    == 0) {
			return &l->chats[i];
		}
	}
	return NULL;
}

/* A conversation's place in the list: Public first, then the public "#"
 * channels, then the private channels, then the direct chats. */
static inline int ui_chat_tier(const struct ui_chat *c)
{
	if (!c->is_group) {
		return 3;
	}
	if (strcmp(c->name, "Public") == 0) {
		return 0;
	}
	if (c->name[0] == '#') {
		return 1;
	}
	return 2;
}

/* The n-th chat in display order.  NULL past the end. */
static inline struct ui_chat *ui_chat_nth(struct ui_chat_list *l, int n)
{
	int seen = 0;

	if (n < 0) {
		return NULL;
	}
	for (uint8_t tier = 0; tier < 4; tier++) {
		for (uint8_t i = 0; i < l->count; i++) {
			if ((uint8_t)ui_chat_tier(&l->chats[i]) != tier) {
				continue;
			}
			if (seen == n) {
				return &l->chats[i];
			}
			seen++;
		}
	}
	return NULL;
}

/* Number of chats in display order. */
static inline int ui_chat_list_len(const struct ui_chat_list *l)
{
	return (int)l->count;
}

/* Drop display-invisible pictographs from a name: a smiley in a nickname
 * is noise on a bitmap panel.  Everything else, Cyrillic included, stays. */
static inline void ui_feed_strip_pictographs(char *dst, size_t n, const char *src)
{
	size_t out = 0;

	if (n == 0) {
		return;
	}
	while (*src != '\0' && out + 1 < n) {
		size_t m = ui_feed_utf8_len(src);

		if (m == 0) {
			break;
		}
		if (m == 4) {
			src += m;
			continue;
		}
		if (m == 3 && (strncmp(src, UI_FEED_VS_EMOJI, 3) == 0 ||
			       strncmp(src, UI_FEED_VS_TEXT, 3) == 0)) {
			src += 3;
			continue;
		}
		if (out + m + 1 > n) {
			break;
		}
		memcpy(dst + out, src, m);
		out += m;
		src += m;
	}
	dst[out] = '\0';
	/* A nickname like "\xF0\x9F\x99\x82 ShadoW" must become "ShadoW", not
	 * " ShadoW": the emoji goes entirely, the gap it left with it. */
	{
		size_t start = 0;
		size_t end = out;

		while (dst[start] == ' ') {
			start++;
		}
		while (end > start && dst[end - 1] == ' ') {
			end--;
		}
		if (start > 0) {
			memmove(dst, dst + start, end - start);
		}
		dst[end - start] = '\0';
	}
}

/* A message that carries map links is only coordinates to a reader on a
 * panel: pull every "q=" pair out and put each on its own line.  Two links
 * in one message become two rows. */
static inline void ui_feed_extract_coords(char *dst, size_t n, const char *src)
{
	static const char host[] = "maps.google.com";
	size_t out = 0;
	int found = 0;

	if (n == 0) {
		return;
	}
	while (*src != '\0' && out + 1 < n) {
		const char *m = strstr(src, host);
		const char *q;

		if (m == NULL) {
			break;
		}
		q = strstr(m, "q=");
		if (q == NULL) {
			src = m + sizeof(host) - 1;
			continue;
		}
		q += 2;
		if (found > 0) {
			dst[out++] = '\n';
		}
		found++;
		while (*q != '\0' && *q != ' ' && *q != '\n' && out + 1 < n) {
			dst[out++] = *q++;
		}
		src = q;
	}
	dst[out] = '\0';
}

/* "https://maps.google.com/?q=55.75580,37.61730" -> "55.75580,37.61730":
 * the link is a courtesy in the app, on a panel only the numbers fit. */
static inline void ui_feed_shorten_map_url(char *dst, size_t n, const char *src)
{
	static const char prefix[] = "https://maps.google.com/";
	size_t out = 0;

	if (n == 0) {
		return;
	}
	while (*src != '\0' && out + 1 < n) {
		if (strncmp(src, prefix, sizeof(prefix) - 1) == 0) {
			const char *q = strstr(src, "q=");

			if (q != NULL) {
				src = q + 2;
				continue;
			}
		}
		dst[out++] = *src++;
	}
	dst[out] = '\0';
}

/* The "<name>: " prefix the mesh puts on a channel message: clean the name
 * of pictographs and shorten any map link in what follows. */
static inline void ui_chat_clean_text(char *dst, size_t n, const char *src)
{
	const char *sep;

	if (strstr(src, "maps.google.com") != NULL) {
		ui_feed_extract_coords(dst, n, src);
		return;
	}
	sep = strstr(src, ": ");
	if (sep == NULL) {
		ui_feed_shorten_map_url(dst, n, src);
		return;
	}
	{
		char name[UI_FEED_NAME_LEN + 1];
		char clean_name[UI_FEED_NAME_LEN + 1];
		char tail[UI_FEED_TEXT_LEN + 1];
		size_t name_len = (size_t)(sep - src);

		if (name_len >= sizeof(name)) {
			name_len = sizeof(name) - 1;
		}
		memcpy(name, src, name_len);
		name[name_len] = '\0';
		ui_feed_strip_pictographs(clean_name, sizeof(clean_name), name);
		ui_feed_shorten_map_url(tail, sizeof(tail), sep);
		snprintf(dst, n, "%s%s", clean_name, tail);
	}
}

/* The conversation carrying `name`, created if this is its first.  No
 * message is recorded: this is how the list is seeded from the channels
 * and contacts the node already has. */
static inline struct ui_chat *ui_chat_ensure(struct ui_chat_list *l,
					     const char *name, bool is_group)
{
	char clean[UI_FEED_NAME_LEN];
	struct ui_chat *c;

	/* Look up by the name as it will be stored: the list keeps names
	 * without their pictographs, and a raw search carrying one never
	 * matched - a second, empty chat opened next to the real one and
	 * the messages landed there. */
	ui_feed_strip_pictographs(clean, sizeof(clean), name);
	c = ui_chat_find(l, clean);
	if (c != NULL) {
		return c;
	}
	if (l->count >= UI_CHAT_COUNT) {
		return NULL;
	}
	c = &l->chats[l->count++];
	memset(c, 0, sizeof(*c));
	strncpy(c->name, clean, sizeof(c->name) - 1);
	c->is_group = is_group;
	return c;
}

/* Record one incoming message.  The chat is created if this is its first.
 * Returns the chat it landed in, or NULL when the list is full and the name
 * is unknown. */
static inline struct ui_chat *ui_chat_push(struct ui_chat_list *l,
					   const char *name, bool is_group,
					   uint32_t ts, const char *text)
{
	struct ui_chat *c = ui_chat_ensure(l, name, is_group);

	if (c == NULL) {
		return NULL;
	}
	{
		uint8_t slot = (uint8_t)((c->head + 1) % UI_CHAT_MSGS);

		/* Body first, then the index: the renderer may run in another
		 * thread, and a half-written message must never become
		 * reachable. */
		c->msgs[slot].ts = ts;
		{
			char cleaned[UI_FEED_TEXT_LEN + 1];

			ui_chat_clean_text(cleaned, sizeof(cleaned), text);
			ui_feed_emoji_text(c->msgs[slot].text,
					   sizeof(c->msgs[slot].text), cleaned);
		}
		c->head = slot;
		if (c->count < UI_CHAT_MSGS) {
			c->count++;
		}
	}
	if (c->unread < 0xFFFF) {
		c->unread++;
	}
	return c;
}

/* ---- lines ---- */

/* A message reads as three kinds of row: the time, the text below it, and a
 * blank line before the next message.  This is the first of them. */
static inline void ui_chat_format_line(char *buf, size_t n,
				       const struct ui_chat *c,
				       const struct ui_chat_msg *m,
				       uint32_t now, int16_t tz_minutes)
{
	(void)c;
	ui_feed_format_time(buf, n, m->ts, now, tz_minutes);
}

/* ---- paging ---- */

struct ui_chat_iter {
	const struct ui_chat *c;
	struct ui_feed_cursor cur;	/* row the next pull returns */
	uint32_t now;
	int16_t tz_minutes;
	int cols;
	char head[UI_FEED_NAME_LEN + 32];	/* "18.11 13:02 Nick:" */
	char line[UI_FEED_TEXT_LEN + 32];	/* what the rows carry */
	const char *line_p;
	int phase;			/* 0 = head, 1 = text, 2 = blank */
	bool row_is_head;		/* the last row pulled was the head */
	bool loaded;
};

/* A direct chat reads as "12:35 ты где?" - the time and the text on one
 * line, wrapped together.  A channel puts the sender on the time's line
 * and starts its text below:
 *     18.11 13:02 Nick:
 *     увидимся
 */
static inline void ui_chat_iter_begin(struct ui_chat_iter *it)
{
	const struct ui_chat_msg *m = ui_chat_get(it->c, it->cur.entry);
	char time[16];

	it->head[0] = '\0';
	it->line[0] = '\0';
	it->line_p = it->line;
	it->phase = 0;
	if (m == NULL) {
		it->cur.entry = it->c->count;
		return;
	}
	ui_feed_format_time(time, sizeof(time), m->ts, it->now, it->tz_minutes);

	it->row_is_head = false;
	if (!it->c->is_group) {
		snprintf(it->line, sizeof(it->line), "%s %s", time, m->text);
		it->phase = 1;	/* no head: the text carries the time */
		return;
	}
	{
		/* The mesh prefixes a channel message with "<name>: ". */
		const char *sep = strstr(m->text, ": ");

		if (sep == NULL) {
			snprintf(it->line, sizeof(it->line), "%s %s", time,
				 m->text);
			it->phase = 1;
			return;
		}
		{
			size_t name_len = (size_t)(sep - m->text);
			char name[UI_FEED_NAME_LEN + 1];

			if (name_len >= sizeof(name)) {
				name_len = sizeof(name) - 1;
			}
			memcpy(name, m->text, name_len);
			name[name_len] = '\0';
			/* One row only: cfb_print wraps at the panel edge
			 * and would spill the name over the text below. */
			{
				char full[UI_FEED_NAME_LEN + 32];
				const char *hp;
				int ncols = 0;
				size_t out = 0;

				snprintf(full, sizeof(full), "%s %s", time,
					 name);
				hp = full;
				it->head[0] = '\0';
				while (*hp != '\0' && ncols < it->cols) {
					size_t adv = ui_feed_utf8_len(hp);

					if (adv == 0 ||
					    out + adv >= sizeof(it->head)) {
						break;
					}
					memcpy(it->head + out, hp, adv);
					hp += adv;
					out += adv;
					ncols++;
				}
				it->head[out] = '\0';
			}
			strncpy(it->line, sep + 2, sizeof(it->line) - 1);
			it->line[sizeof(it->line) - 1] = '\0';
		}
	}
}

/* Step over one row without emitting it.  False at the message's end. */
static inline bool ui_chat_iter_skip_one(struct ui_chat_iter *it)
{
	if (it->cur.entry >= it->c->count) {
		return false;
	}
	if (it->phase == 0) {
		it->phase = 1;
	} else if (it->phase == 1) {
		if (*it->line_p == '\0') {
			it->phase = 2;
		} else {
			/* The wrap needs the full row width: a short buffer
			 * cuts the row early and leaves the cursor standing
			 * inside a line instead of past it. */
			char skip[UI_FEED_TEXT_LEN + 32];

			ui_feed_wrap_row(skip, sizeof(skip), &it->line_p,
					 it->cols);
		}
	} else {
		return false;
	}
	it->cur.row++;
	return true;
}

static inline void ui_chat_iter_load(struct ui_chat_iter *it)
{
	/* The skip count must be held outside the cursor: skip_one advances
	 * cur.row as it goes, and a loop over a moving bound never ends. */
	uint16_t skip = it->cur.row;

	it->loaded = true;
	it->cur.row = 0;
	if (it->cur.entry >= it->c->count) {
		return;
	}
	ui_chat_iter_begin(it);
	for (uint16_t i = 0; i < skip; i++) {
		if (!ui_chat_iter_skip_one(it)) {
			break;
		}
	}
}

static inline void ui_chat_iter_init(struct ui_chat_iter *it,
				     const struct ui_chat *c,
				     struct ui_feed_cursor cur, uint32_t now,
				     int16_t tz_minutes, int cols)
{
	it->c = c;
	it->cur = cur;
	it->now = now;
	it->tz_minutes = tz_minutes;
	it->cols = cols > 0 ? cols : 1;
	it->loaded = false;
}

/* Pull the next row: the head line, a line of text, or the blank between
 * messages.  False at the end of the chat. */
static inline bool ui_chat_iter_next(struct ui_chat_iter *it, char *out,
				     size_t out_n)
{
	if (!it->loaded) {
		ui_chat_iter_load(it);
	}
	while (it->cur.entry < it->c->count) {
		if (it->phase == 0) {
			if (out_n > 0) {
				strncpy(out, it->head, out_n - 1);
				out[out_n - 1] = '\0';
			}
			it->row_is_head = true;
			it->phase = 1;
			it->cur.row++;
			return true;
		}
		if (it->phase == 1) {
			if (*it->line_p == '\0') {
				it->phase = 2;
				continue;
			}
			{
				char row[UI_FEED_TEXT_LEN + 32];

				ui_feed_wrap_row(row, sizeof(row), &it->line_p,
						 it->cols);
				if (out_n > 0) {
					strncpy(out, row, out_n - 1);
					out[out_n - 1] = '\0';
				}
				it->row_is_head = false;
				it->cur.row++;
				return true;
			}
		}
		/* The blank line: the last row of the message. */
		if (out_n > 0) {
			out[0] = '\0';
		}
		it->row_is_head = false;
		it->cur.entry++;
		it->cur.row = 0;
		ui_chat_iter_begin(it);
		return true;
	}
	return false;
}

/* The cursor of the page that starts `page_rows` display rows before `cur`. */
static inline struct ui_feed_cursor
ui_chat_page_before(const struct ui_chat *c, struct ui_feed_cursor cur,
		    uint32_t now, int16_t tz_minutes, int cols, int page_rows)
{
	struct ui_chat_iter it;
	struct ui_feed_cursor zero = { 0, 0 };
	struct ui_feed_cursor best = { 0, 0 };
	struct ui_feed_cursor page_start = { 0, 0 };
	int in_page = 0;
	char row[UI_FEED_TEXT_LEN + 32];

	if (page_rows < 1) {
		page_rows = 1;
	}
	ui_chat_iter_init(&it, c, zero, now, tz_minutes, cols);
	while (it.cur.entry < c->count) {
		struct ui_feed_cursor row_start = it.cur;

		if (!ui_chat_iter_next(&it, row, sizeof(row))) {
			break;
		}
		if (in_page == 0) {
			page_start = row_start;
		}
		in_page++;
		if (in_page >= page_rows) {
			if (ui_feed_cursor_before(page_start, cur)) {
				best = page_start;
			} else {
				break;
			}
			in_page = 0;
		}
	}
	return best;
}
