/* SPDX-License-Identifier: MIT */
/*
 * Unicode -> 8-bit display charset, shared by the renderer and its host
 * tests.  The 6x8 font covers: ASCII 32-127, Cyrillic 128-193 (Russian,
 * yo after e), Latin-1 A circumflex .. y diaeresis 194-255.  Everything
 * else folds to a base letter (NBSP, guillemets, dashes, ellipsis,
 * numero, ruble) or is dropped.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Base-letter fold for all of Latin Extended-A (U+0100..U+017F), indexed by
 * cp - 0x100. Used for code points without a glyph slot above. */
static const char display_latin_ext_a_fold[] =
	"AaAaAa" "CcCcCcCc" "DdDd" "EeEeEeEeEe" "GgGgGgGg" "HhHh"
	"IiIiIiIiIi" "Ii" "Jj" "Kkk" "LlLlLlLlLl" "NnNnNnnNn"
	"OoOoOoOo" "RrRrRr" "SsSsSsSs" "TtTtTt" "UuUuUuUuUuUu"
	"Ww" "YyY" "ZzZzZz" "s";

/* U+0100..U+017F is exactly 128 code points (string literal adds NUL);
 * a short table would make display_charset_map() read past the end. */
#ifdef __cplusplus
static_assert(sizeof(display_latin_ext_a_fold) == 128 + 1,
	      "fold table must cover U+0100..U+017F");
#else
_Static_assert(sizeof(display_latin_ext_a_fold) == 128 + 1,
	       "fold table must cover U+0100..U+017F");
#endif


static inline uint8_t display_charset_map(uint32_t cp)
{
	/* Russian letters that are drawn exactly like a Latin one borrow that
	 * letter's glyph: the panel font already draws those well, and a hand
	 * copied double never matches.  Everything else falls to the Cyrillic
	 * slots below.  т and к stay Cyrillic: Latin t and k have ascenders,
	 * the Russian letters are x-height. */
	switch (cp) {
	case 0x410: return 'A';	/* А */
	case 0x412: return 'B';	/* В */
	case 0x415: return 'E';	/* Е */
	case 0x41D: return 'H';	/* Н */
	case 0x41E: return 'O';	/* О */
	case 0x420: return 'P';	/* Р */
	case 0x421: return 'C';	/* С */
	case 0x422: return 'T';	/* Т */
	case 0x430: return 'a';	/* а */
	case 0x435: return 'e';	/* е */
	case 0x43E: return 'o';	/* о */
	case 0x440: return 'p';	/* р */
	case 0x441: return 'c';	/* с */
	case 0x443: return 'y';	/* у */
	case 0x445: return 'x';	/* х */
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
	if (cp == 0xA0) {
		return ' ';	/* NBSP must still break words */
	}
	if (cp == 0xAB) {
		return '<';	/* « */
	}
	if (cp == 0xBB) {
		return '>';	/* » */
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
	switch (cp) {	/* Romanian comma-below + common Russian punctuation */
	case 0x218: return 'S';
	case 0x219: return 's';
	case 0x21A: return 'T';
	case 0x21B: return 't';
	case 0x2018:			/* ' */
	case 0x2019: return '\'';	/* ' */
	case 0x201C:			/* " */
	case 0x201D: return '"';	/* " */
	case 0x2013:			/* – */
	case 0x2014: return '-';	/* — */
	case 0x2026: return '.';	/* … */
	case 0x2116: return 'N';	/* № */
	case 0x20BD: return 'P';	/* ₽ */
	default:    return 0;	/* drop: emoji, other scripts, ... */
	}
}

/* Sanitize UTF-8 into display slots.  ASCII passes through, valid UTF-8
 * goes through display_charset_map(), and anything else keeps its bytes:
 * display-encoded text (slots 128-255) must survive a second pass.
 * Sequences are decoded strictly (RFC 3629) so slot bytes that merely
 * look like lead bytes are not eaten — ю and я live at 0xC0/0xC1 and
 * would otherwise swallow the glyph behind them on the second pass. */
static inline void utf8_to_display(char *dst, const char *src, size_t dst_size)
{
	size_t di = 0;
	size_t si = 0;

	if (dst_size == 0) {
		return;
	}
	while (src[si] && di < dst_size - 1) {
		uint8_t c = (uint8_t)src[si];
		uint32_t cp;
		int len;

		if (c < 0x80) {
			dst[di++] = (char)c;
			si++;
			continue;
		} else if (c >= 0xC2 && c <= 0xDF) {
			cp = c & 0x1F;
			len = 2;
		} else if (c >= 0xE0 && c <= 0xEF) {
			cp = c & 0x0F;
			len = 3;
		} else if (c >= 0xF0 && c <= 0xF4) {
			cp = c & 0x07;
			len = 4;
		} else {
			/* Not a valid UTF-8 lead byte: already display-encoded
			 * text (or junk) — pass through so a second sanitizing
			 * pass is harmless. */
			dst[di++] = (char)c;
			si++;
			continue;
		}

		bool valid = true;

		for (int k = 1; k < len; k++) {
			if (((uint8_t)src[si + k] & 0xC0) != 0x80) {
				valid = false;
				break;
			}
			cp = (cp << 6) | ((uint8_t)src[si + k] & 0x3F);
		}
		/* RFC 3629: no overlong forms, no surrogates, nothing past
		 * U+10FFFF.  A rejected sequence is display-encoded text in
		 * a lead byte's clothing: keep the lead as a glyph and
		 * rescan the rest. */
		if (valid && ((len == 3 && cp < 0x800) ||
		    (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
		    (cp >= 0xD800 && cp <= 0xDFFF))) {
			valid = false;
		}
		if (!valid) {
			dst[di++] = (char)c;	/* lone lead byte: pass through */
			si++;
			continue;
		}
		si += len;

		uint8_t out = display_charset_map(cp);

		if (out) {
			dst[di++] = (char)out;
		}
	}
	dst[di] = '\0';
}

