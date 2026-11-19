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
	/* Russian letters that are drawn exactly like a Latin one borrow that
	 * letter's glyph: the panel font already draws those well, and a hand
	 * copied double never matches.  Everything else falls to the Cyrillic
	 * slots below. */
	switch (cp) {
	case 0x410: return 'A';	/* А */
	case 0x412: return 'B';	/* В */
	case 0x415: return 'E';	/* Е */
	case 0x41A: return 'K';	/* К */
	case 0x41C: return 'M';	/* М */
	case 0x41D: return 'H';	/* Н */
	case 0x41E: return 'O';	/* О */
	case 0x420: return 'P';	/* Р */
	case 0x421: return 'C';	/* С */
	case 0x422: return 'T';	/* Т */
	case 0x423: return 'Y';	/* У */
	case 0x425: return 'X';	/* Х */
	case 0x430: return 'a';	/* а */
	case 0x435: return 'e';	/* е */
	case 0x43E: return 'o';	/* о */
	case 0x440: return 'p';	/* р */
	case 0x441: return 'c';	/* с */
	case 0x442: return 't';	/* т */
	case 0x443: return 'y';	/* у */
	case 0x445: return 'x';	/* х */
	case 0x43A: return 'k';	/* к */
	default: break;
	}

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

