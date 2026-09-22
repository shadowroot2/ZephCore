#pragma once

#include <helpers/NodePrefs.h>
#include <stdint.h>
#include <string.h>

/* Versioned tail after the unchanged author's prefs layout. */
namespace ShadowPrefs {
constexpr size_t payload_size = 39;
constexpr size_t record_size = 5 + payload_size;

inline void decodePayload(NodePrefs &p, const uint8_t *b)
{
    p.ui_timezone_offset_minutes = (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
    p.auto_shutdown_emergency = b[2];
    p.tracking_interval_minutes = (uint16_t)b[3] | ((uint16_t)b[4] << 8);
    memcpy(p.tracking_group_name, b + 5, 32);
    p.tracking_group_name[31] = 0;
    p.fall_sensitivity = b[37];
    p.fall_enabled = b[38];
}

inline bool decode(NodePrefs &p, const uint8_t *b, size_t n)
{
    if (n != record_size || memcmp(b, "SHPF", 4) != 0 || b[4] != 1)
        return false;
    decodePayload(p, b + 5);
    return true;
}

inline void encode(const NodePrefs &p, uint8_t *b)
{
    memcpy(b, "SHPF", 4);
    b[4] = 1;
    b += 5;
    b[0] = (uint16_t)p.ui_timezone_offset_minutes;
    b[1] = (uint16_t)p.ui_timezone_offset_minutes >> 8;
    b[2] = p.auto_shutdown_emergency;
    b[3] = p.tracking_interval_minutes;
    b[4] = p.tracking_interval_minutes >> 8;
    memcpy(b + 5, p.tracking_group_name, 32);
    b[36] = 0;
    b[37] = p.fall_sensitivity;
    b[38] = p.fall_enabled;
}
}
