/*
 * ZephCore - Joystick UI Definitions
 * Copyright (c) 2026 ZephCore
 * SPDX-License-Identifier: MIT
 *
 * Key codes and layout constants for the joystick UI.
 */

#pragma once

/* ===== Key codes ===== */
#define KEY_ENTER       0x0D   /* center/OK button click */
#define KEY_LEFT        0x01   /* joystick left */
#define KEY_RIGHT       0x02   /* joystick right */
#define KEY_UP          0x03   /* joystick up */
#define KEY_DOWN        0x04   /* joystick down */
#define KEY_CANCEL      0x1B   /* back/ESC button */
#define KEY_NEXT        0x05   /* single button next page */
#define KEY_PREV        0x06   /* single button prev page */
#define KEY_HOME        0x07   /* go to home screen */
#define KEY_SELECT      0x08   /* triple click / special */
#define KEY_ENTER_LONG  0xF1   /* long press enter */
#define KEY_TO_TOP      0xF2   /* long press up   → page up   */
#define KEY_TO_BOTTOM   0xF3   /* long press down → page down */
#define KEY_LOCK        0xF4   /* user button + joystick center held together → screen lock */

/* Global action keys emitted by multi tap filter, handled in loop() */
#define KEY_FLOOD_ADVERT  0x42   /* INPUT_KEY_B: 2 taps → flood advert */
#define KEY_BUZZ_TOGGLE   0x44   /* INPUT_KEY_D: 3 taps → buzzer mute toggle */
#define KEY_GPS_TOGGLE    0x43   /* INPUT_KEY_C: 4 taps → GPS on/off */
#define KEY_LED_TOGGLE    0x45   /* INPUT_KEY_E: 5 taps → LED heartbeat toggle */

/* ===== Layout constants (calibrated for 128x64 OLED, 6x8 font) ===== */
/* All hard-coded offsets from old Arduino code are preserved here.
 * Resolution aware screens should use mc_display_width/height() directly. */
#define kHeaderSepY     11    /* y position of header separator line */
#define kContentY       14    /* y position where page content starts */
#define kMenuLineH      11    /* vertical spacing between menu items */
#define kBodyY          14    /* synonym for kContentY */
#define kLineH          9     /* compact line height */

/* Battery range for percentage calculation */
#define kBattMinMv      3000
#define kBattMaxMv      4200

/* Auto off timeout */
#define AUTO_OFF_MILLIS  30000UL   /* 30 seconds */

/* List view constants */
#define UI_RECENT_LIST_SIZE   4    /* max items visible in a scrollable list */

/* UI-only path_len status markers (no wire-format meaning beyond OUT_PATH_SENT,
 * which is exposed to the BLE app). These two encode the outcome of a local
 * channel send for the joystick UI's _ch_previews ring buffer:
 *   OUT_PATH_SENT_HEARD   — at least one neighbor repeated the flood
 *   OUT_PATH_SENT_UNHEARD — feedback window elapsed without hearing a repeat */
#define OUT_PATH_SENT_HEARD    0xFD
#define OUT_PATH_SENT_UNHEARD  0xFC
