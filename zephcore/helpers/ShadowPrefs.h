#pragma once

#include <helpers/NodePrefs.h>
#include <stdint.h>
#include <string.h>

/* Read-only migration of the ShadoW tail written by 1.17.4 firmware. */
namespace ShadowPrefs {
constexpr size_t payload_size = 39;
constexpr size_t record_size = 5 + payload_size;

inline void decodePayload(NodePrefs &p, const uint8_t *b)
{
    /* b[0..1] held a superseded UI-only timezone; tz_offset is authoritative. */
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

}
