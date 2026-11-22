/* SPDX-License-Identifier: MIT */
/*
 * Host tests for the Unicode -> 8-bit display charset: the Cyrillic slot
 * layout (yo shifts the tail of the alphabet), the Latin-1 survivors and
 * the base-letter folds.
 */

#include <display_charset.h>

#include <cstring>

#include "test.h"

TEST(charset_cyrillic_slots, "UNIT-CHARSET-001",
     "Cyrillic lands in 128-193 with yo in the slot after e")
{
	/* Letters with a Latin twin borrow that glyph (UNIT-CHARSET-003);
	 * the rest take a slot of their own: Б..Ж are 129..135, Ё 134,
	 * Я 160; б..ж are 162..168, ё 167, я 193. */
	CHECK(display_charset_map(0x411) == 129);	/* Б */
	CHECK(display_charset_map(0x401) == 134);	/* Ё */
	CHECK(display_charset_map(0x416) == 135);	/* Ж */
	CHECK(display_charset_map(0x42F) == 160);	/* Я */

	CHECK(display_charset_map(0x431) == 162);	/* б */
	CHECK(display_charset_map(0x451) == 167);	/* ё */
	CHECK(display_charset_map(0x436) == 168);	/* ж */
	CHECK(display_charset_map(0x44F) == 193);	/* я */

	/* Every Russian letter lands somewhere unique, in 128..193 or on
	 * the Latin glyph it borrows, and nothing spills past 193. */
	bool used[256] = { false };

	for (uint32_t cp = 0x410; cp <= 0x44F; cp++) {
		uint8_t s = display_charset_map(cp);

		if (s < 128) {
			continue;	/* borrowed a Latin glyph */
		}
		CHECK(s >= 128 && s <= 193);
		CHECK(!used[s]);
		used[s] = true;
	}
	{
		uint8_t yo1 = display_charset_map(0x401);
		uint8_t yo2 = display_charset_map(0x451);

		CHECK(yo1 >= 128 && yo1 <= 193);
		CHECK(yo2 >= 128 && yo2 <= 193);
		CHECK(!used[yo1]);
		used[yo1] = true;
		CHECK(!used[yo2]);
		used[yo2] = true;
	}
	int n = 0;

	for (int s = 128; s <= 193; s++) {
		if (used[s]) {
			n++;
		}
	}
	/* U+0410..U+044F is 64 letters, 15 of which borrowed a Latin glyph,
	 * plus the two yo's sitting outside that range. */
	CHECK(n == 51);
}

TEST(charset_latin_survivors, "UNIT-CHARSET-002",
     "Latin-1 keeps 194-255, the lost range folds or drops")
{
	/* ASCII never reaches this function: utf8_to_display() passes it
	 * through.  Everything from here on is non-ASCII. */
	CHECK(display_charset_map(0xC2) == 0xC2);	/* Â: native glyph */
	CHECK(display_charset_map(0xFF) == 0xFF);	/* ÿ: native glyph */

	/* A-grave/A-acute lost their slots to Cyrillic and fold to A. */
	CHECK(display_charset_map(0xC0) == 'A');
	CHECK(display_charset_map(0xC1) == 'A');

	/* Currency signs and fractions have no slot and must not borrow a
	 * Cyrillic letter's glyph. */
	CHECK(display_charset_map(0xA3) == 0);	/* pound */
	CHECK(display_charset_map(0xBD) == 0);	/* 1/2 */

	/* Spacing and quotes that must survive as themselves. */
	CHECK(display_charset_map(0xA0) == ' ');	/* NBSP: still a break */
	CHECK(display_charset_map(0xAB) == '<');	/* « */
	CHECK(display_charset_map(0xBB) == '>');	/* » */

	/* Common Russian punctuation folds to ASCII. */
	CHECK(display_charset_map(0x2014) == '-');	/* — */
	CHECK(display_charset_map(0x2013) == '-');	/* – */
	CHECK(display_charset_map(0x2026) == '.');	/* … */
	CHECK(display_charset_map(0x2116) == 'N');	/* № */
	CHECK(display_charset_map(0x20BD) == 'P');	/* ₽ */
	CHECK(display_charset_map(0x2019) == '\'');	/* ' */

	/* Latin Extended-A folds to its base letter. */
	CHECK(display_charset_map(0x0104) == 'A');	/* A ogonek */
	CHECK(display_charset_map(0x017E) == 'z');	/* z caron */

	/* Romanian comma-below forms. */
	CHECK(display_charset_map(0x219) == 's');

	/* Pictographs go; nothing returns a Cyrillic slot by accident. */
	CHECK(display_charset_map(0x1F642) == 0);	/* 🙂 */
	CHECK(display_charset_map(0x2764) == 0);	/* heart */
}

TEST(charset_lookalike_borrowing, "UNIT-CHARSET-003",
     "A Russian letter drawn like a Latin one borrows its glyph")
{
	/* Upper: A B E H O P C T */
	CHECK(display_charset_map(0x410) == 'A');
	CHECK(display_charset_map(0x412) == 'B');
	CHECK(display_charset_map(0x415) == 'E');
	CHECK(display_charset_map(0x41A) != 'K');	/* К is not K: own shape */
	CHECK(display_charset_map(0x41C) != 'M');	/* М is not M: own shape */
	CHECK(display_charset_map(0x41D) == 'H');
	CHECK(display_charset_map(0x41E) == 'O');
	CHECK(display_charset_map(0x420) == 'P');
	CHECK(display_charset_map(0x421) == 'C');
	CHECK(display_charset_map(0x422) == 'T');
	CHECK(display_charset_map(0x423) != 'Y');	/* У is not Y */
	CHECK(display_charset_map(0x425) != 'X');	/* Х is not X */

	/* Lower: a e o p c y x */
	CHECK(display_charset_map(0x430) == 'a');
	CHECK(display_charset_map(0x435) == 'e');
	CHECK(display_charset_map(0x43E) == 'o');
	CHECK(display_charset_map(0x440) == 'p');
	CHECK(display_charset_map(0x441) == 'c');
	CHECK(display_charset_map(0x442) != 't');	/* т is not t: a small T */
	CHECK(display_charset_map(0x443) == 'y');
	CHECK(display_charset_map(0x445) == 'x');
	CHECK(display_charset_map(0x43A) != 'k');	/* к is not k: a small K */

	/* The ones with no Latin twin still go to their own slot. */
	CHECK(display_charset_map(0x411) == 129);	/* Б */
	CHECK(display_charset_map(0x416) == 135);	/* Ж */
	CHECK(display_charset_map(0x44F) == 193);	/* я */
	CHECK(display_charset_map(0x401) == 134);	/* Ё */
	CHECK(display_charset_map(0x451) == 167);	/* ё */
}

TEST(charset_second_pass_idempotent, "UNIT-CHARSET-004",
     "Already display-encoded text survives a second sanitizing pass")
{
	/* utf8_to_display() runs twice over names: once to store them,
	 * once at draw time.  The second pass must be a no-op, but slots
	 * 192/193 (ю/я) sit on UTF-8 lead bytes and used to swallow the
	 * glyph behind them. */
	char once[48];
	char twice[48];
	const char *names[] = {
		"яблоко",	/* я is slot 193 = 0xC1, a 2-byte lead */
		"Юля",		/* ю is slot 192 = 0xC0 */
		"объявление",
		"Hello ёж №5 — юля",
	};

	for (const char *name : names) {
		utf8_to_display(once, name, sizeof(once));
		utf8_to_display(twice, once, sizeof(twice));
		CHECK(strcmp(once, twice) == 0);
	}

	/* Explicit pair: lead-like slot must not eat the next slot. */
	const char encoded[] = { (char)193, (char)162, 0 };	/* яб */
	char out[8];

	utf8_to_display(out, encoded, sizeof(out));
	CHECK((uint8_t)out[0] == 193);
	CHECK((uint8_t)out[1] == 162);

	/* Valid UTF-8 still decodes: U+044F is я, U+0410 is А. */
	utf8_to_display(out, "\xD1\x8F", sizeof(out));
	CHECK((uint8_t)out[0] == 193);
	utf8_to_display(out, "\xD0\x90", sizeof(out));
	CHECK(out[0] == 'A');

	/* Truncated and overlong sequences pass through as bytes. */
	utf8_to_display(out, "\xD0", sizeof(out));
	CHECK((uint8_t)out[0] == 0xD0);
	utf8_to_display(out, "\xC0\xAF", sizeof(out));
	CHECK((uint8_t)out[0] == 0xC0);
	CHECK((uint8_t)out[1] == 0xAF);
}
