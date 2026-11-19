/* SPDX-License-Identifier: MIT */
/*
 * Unicode -> 8-bit display charset, shared by the renderer and its host
 * tests.  The 6x8 font covers: ASCII 32-127, Cyrillic 128-193 (Russian,
 * yo after e), Latin-1 A circumflex .. y diaeresis 194-255.  Everything
 * else folds to a base letter or is dropped.
 */

#pragma once

#include <stdint.h>

/* Base-letter fold for all of Latin Extended-A (U+0100..U+017F), indexed by
 * cp - 0x100. Used for code points without a glyph slot above. */
static const char display_latin_ext_a_fold[] =
	"AaAaAa" "CcCcCcCc" "DdDd" "EeEeEeEeEe" "GgGgGgGg" "HhHh"
	"IiIiIiIiIi" "Ii" "Jj" "Kkk" "LlLlLlLlLl" "NnNnNnnNn"
	"OoOoOoOo" "RrRrRr" "SsSsSsSs" "TtTtTt" "UuUuUuUuUuUu"
	"Ww" "YyY" "ZzZzZz" "s";


static inline uint8_t display_charset_map(uint32_t cp)
{
	/* Cyrillic (Russian) lives in slots 128-193: 33 upper then 33 lower,
	 * in Unicode order U+0410..U+042F / U+0430..U+044F, with yo (U+0401 /
	 * U+0451) in the slot after e/E — which shifts the tail of each row. */
	if (cp >= 0x410 && cp <= 0x415) {	/* А..Е */
		return (uint8_t)(128 + (cp - 0x410));	/* 128..133 */
	}
	if (cp == 0x401) {			/* Ё */
		return 134;
	}
	if (cp >= 0x416 && cp <= 0x42F) {	/* Ж..Я */
		return (uint8_t)(128 + (cp - 0x410) + 1); /* 135..160 */
	}
	if (cp >= 0x430 && cp <= 0x435) {	/* а..е */
		return (uint8_t)(161 + (cp - 0x430));	/* 161..166 */
	}
	if (cp == 0x451) {			/* ё */
		return 167;
	}
	if (cp >= 0x436 && cp <= 0x44F) {	/* ж..я */
		return (uint8_t)(161 + (cp - 0x430) + 1); /* 168..193 */
	}
	/* Latin-1 accented keeps its glyphs in slots 194..255 (A circumflex
	 * .. y diaeresis).  The 0xA0-0xC1 range lost its slots to Cyrillic:
	 * A-grave/A-acute fold to A, the rest (currency signs, fractions)
	 * drop rather than borrow a Cyrillic letter's glyph. */
	if (cp >= 0xC2 && cp <= 0xFF) {
		return (uint8_t)cp;	/* native Latin-1 glyph */
	}
	if (cp == 0xC0 || cp == 0xC1) {
		return 'A';
	}
	if (cp >= 0xA0 && cp <= 0xBF) {
		return 0;
	}
	/* Latin Extended-A: the glyph slots are gone, fold to the base
	 * letter (Hungarian, Czech, Slovak, Polish, Croatian names read
	 * unaccented rather than as a Cyrillic letter). */
	if (cp >= 0x100 && cp <= 0x17F) {
		return (uint8_t)display_latin_ext_a_fold[cp - 0x100];
	}
	switch (cp) {	/* Romanian uses comma-below variants */
	case 0x218: return 'S';
	case 0x219: return 's';
	case 0x21A: return 'T';
	case 0x21B: return 't';
	default:    return 0;	/* drop: emoji, other scripts, ... */
	}
}

