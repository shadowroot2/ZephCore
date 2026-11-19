/* SPDX-License-Identifier: MIT */
/*
 * Host tests for the Unicode -> 8-bit display charset: the Cyrillic slot
 * layout (yo shifts the tail of the alphabet), the Latin-1 survivors and
 * the base-letter folds.
 */

#include <display_charset.h>

#include "test.h"

TEST(charset_cyrillic_slots, "UNIT-CHARSET-001",
     "Cyrillic lands in 128-193 with yo in the slot after e")
{
	/* Upper: А..Е are 128..133, Ё 134, Ж..Я 135..160. */
	CHECK(display_charset_map(0x410) == 128);	/* А */
	CHECK(display_charset_map(0x415) == 133);	/* Е */
	CHECK(display_charset_map(0x401) == 134);	/* Ё */
	CHECK(display_charset_map(0x416) == 135);	/* Ж */
	CHECK(display_charset_map(0x42F) == 160);	/* Я */

	/* Lower: а..е are 161..166, ё 167, ж..я 168..193. */
	CHECK(display_charset_map(0x430) == 161);	/* а */
	CHECK(display_charset_map(0x435) == 166);	/* е */
	CHECK(display_charset_map(0x451) == 167);	/* ё */
	CHECK(display_charset_map(0x436) == 168);	/* ж */
	CHECK(display_charset_map(0x44F) == 193);	/* я */

	/* The whole of each half is accounted for, no two letters share a
	 * slot and nothing spills past 193. */
	bool used[256] = { false };

	for (uint32_t cp = 0x410; cp <= 0x42F; cp++) {
		uint8_t s = display_charset_map(cp);

		CHECK(s >= 128 && s <= 160);
		CHECK(!used[s]);
		used[s] = true;
	}
	CHECK(!used[134]);	/* Ё is not one of А..Я */
	used[134] = true;
	for (uint32_t cp = 0x430; cp <= 0x44F; cp++) {
		uint8_t s = display_charset_map(cp);

		CHECK(s >= 161 && s <= 193);
		CHECK(!used[s]);
		used[s] = true;
	}
	CHECK(!used[167]);	/* ё is not one of а..я */
	used[167] = true;
	int n = 0;

	for (int s = 128; s <= 193; s++) {
		if (used[s]) {
			n++;
		}
	}
	CHECK(n == 66);
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

	/* Latin Extended-A folds to its base letter. */
	CHECK(display_charset_map(0x0104) == 'A');	/* A ogonek */
	CHECK(display_charset_map(0x017E) == 'z');	/* z caron */

	/* Romanian comma-below forms. */
	CHECK(display_charset_map(0x219) == 's');

	/* Pictographs go; nothing returns a Cyrillic slot by accident. */
	CHECK(display_charset_map(0x1F642) == 0);	/* 🙂 */
	CHECK(display_charset_map(0x2764) == 0);	/* heart */
}
