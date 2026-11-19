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
#include <mesh/MeshCore.h>

/* Same bound as BaseChatMesh's MAX_TEXT_LEN (10*CIPHER_BLOCK_SIZE): a feed
 * entry never truncates what the mesh accepted. */
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

/* A single deferred redraw waits this long after an entry landed while the
 * viewer was reading older messages, so a running conversation does not jump
 * the screen out from under them. */
#define UI_FEED_DEFER_MS 5000

#define UI_FEED_F_DM 0x01u /* direct message: rendered with ">>" */

/* Channel names are short in practice ("Public", "#zephcore"); the feed keeps
 * enough to tag the line. */
#define UI_FEED_CHANNEL_LEN 16

struct ui_feed_entry {
	uint32_t ts; /* sender timestamp (unix seconds), 0 = unknown */
	uint8_t flags;
	char name[UI_FEED_NAME_LEN];
	char channel[UI_FEED_CHANNEL_LEN]; /* empty for a direct message */
	char text[UI_FEED_TEXT_LEN + 1];
};

struct ui_feed {
	struct ui_feed_entry e[UI_FEED_SLOTS];
	uint8_t head;  /* index of the newest entry */
	uint8_t count; /* valid entries, 0..UI_FEED_SLOTS */
};

enum ui_feed_redraw {
	UI_FEED_REDRAW_NOW = 0,   /* the viewer is at the top: show it at once */
	UI_FEED_REDRAW_DEFER = 1, /* the viewer is reading older: hold the redraw */
};

/* ---- ring ---- */

static inline void ui_feed_reset(struct ui_feed *f)
{
	memset(f, 0, sizeof(*f));
}

/* Newest first: 0 is the newest, count-1 the oldest. NULL past the end. */
static inline const struct ui_feed_entry *ui_feed_get(const struct ui_feed *f,
						      uint8_t i)
{
	if (i >= f->count) {
		return NULL;
	}
	return &f->e[(uint8_t)((f->head + UI_FEED_SLOTS - i) % UI_FEED_SLOTS)];
}

/* Push a message. `view_pos` is the entry currently at the top of the screen
 * (0 = the newest); when the viewer is down the list it is moved along with
 * the entries so the message being read stays under the eye. Returns whether
 * the screen may be redrawn now. */
static inline enum ui_feed_redraw ui_feed_push(struct ui_feed *f,
					       const struct ui_feed_entry *e,
					       uint8_t *view_pos)
{
	bool at_top = (view_pos == NULL || *view_pos == 0);
	uint8_t slot = (uint8_t)((f->head + 1) % UI_FEED_SLOTS);

	/* Entry body first, then the indices: the renderer may run in another
	 * thread, and a half-written entry must never become reachable. */
	f->e[slot] = *e;
	f->head = slot;
	if (f->count < UI_FEED_SLOTS) {
		f->count++;
	}
	if (!at_top && view_pos != NULL) {
		/* Everything moved one slot towards the old end; keep the
		 * reading position on the same message. */
		if (*view_pos + 1 < f->count) {
			(*view_pos)++;
		}
	}
	return at_top ? UI_FEED_REDRAW_NOW : UI_FEED_REDRAW_DEFER;
}

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

/* ---- line ---- */

/* One feed line in full, before any wrapping:
 *   ">> 12:35 Mama: ты где?"              direct: name from the contact
 *   "18.11 12:34 Nick: привет, как дела?" public channel (untagged)
 *   "12:34 [#zephcore] Nick: тру ляля"    any other channel
 * A channel message carries its sender as the "<name>: " prefix the mesh
 * already put in the text (sendGroupMessage), so only a direct message adds
 * a name of its own. `channel_name` is ignored for a direct message. */
static inline void ui_feed_format_line(char *buf, size_t n,
				       const struct ui_feed_entry *e,
				       uint32_t now, int16_t tz_minutes)
{
	char time[16];

	ui_feed_format_time(time, sizeof(time), e->ts, now, tz_minutes);

	if (e->flags & UI_FEED_F_DM) {
		snprintf(buf, n, ">> %s %s: %s", time, e->name, e->text);
	} else if (e->channel[0] == '\0' ||
		   strcmp(e->channel, "Public") == 0) {
		snprintf(buf, n, "%s %s", time, e->text);
	} else {
		snprintf(buf, n, "%s [%.12s] %s", time, e->channel, e->text);
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
	{ "\xF0\x9F\x99\x82", ":)" },    /* 🙂 */
	{ "\xF0\x9F\x98\x84", ":D" },    /* 😄 */
	{ "\xF0\x9F\x98\xA2", ":(" },    /* 😢 */
	{ "\xE2\x9D\xA4", "<3" },        /* ❤ */
	{ "\xF0\x9F\x91\x8D", "+1" },    /* 👍 */
	{ "\xF0\x9F\x91\x8E", "-1" },    /* 👎 */
	{ "\xF0\x9F\x86\x98", "SOS!" },  /* 🆘 */
	{ "\xF0\x9F\x93\x8D", "[map]" }, /* 📍 */
	{ "\xF0\x9F\x9B\xB0", "[sat]" }, /* 🛰 */
	{ "\xF0\x9F\x94\x8B", "[bat]" }, /* 🔋 */
	{ "\xF0\x9F\x8C\xA1", "[t]" },   /* 🌡 */
	{ "\xF0\x9F\x9A\x80", "[go]" },  /* 🚀 */
	{ "\xF0\x9F\x9B\x91", "[stop]" }, /* 🛑 */
	{ "\xF0\x95\x95\x93", "[time]" }, /* 🕓 */
	{ "\xF0\x9F\x8F\x83", "[run]" }, /* 🏃 */
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
			/* U+1F000..U+3FFFF is the pictograph plane(s): drop an
			 * emoji we have no text for. */
			if (m == 4) {
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
		size_t adv = ui_feed_utf8_len(s);

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
