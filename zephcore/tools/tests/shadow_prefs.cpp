// Host regression tests for the versioned custom preference tail.
#define CONFIG_ZEPHCORE_UI_TIMEZONE_OFFSET_MINUTES 0
#include <helpers/ShadowPrefs.h>
#include <assert.h>
#include <stdio.h>

int main()
{
    NodePrefs original{};
    initNodePrefs(&original);
    original.ui_timezone_offset_minutes = -345;
    original.auto_shutdown_emergency = 0;
    original.tracking_interval_minutes = 65535;
    strcpy(original.tracking_group_name, "#migration-test");
    original.fall_sensitivity = 5;
    original.fall_enabled = 0;

    uint8_t record[ShadowPrefs::record_size + 1]{};
    ShadowPrefs::encode(original, record);
    assert(record[5] == 0xa7 && record[6] == 0xfe);
    assert(record[8] == 0xff && record[9] == 0xff);

    NodePrefs restored{};
    initNodePrefs(&restored);
    assert(ShadowPrefs::decode(restored, record, ShadowPrefs::record_size));
    assert(restored.ui_timezone_offset_minutes == -345);
    assert(restored.auto_shutdown_emergency == 0);
    assert(restored.tracking_interval_minutes == 65535);
    assert(strcmp(restored.tracking_group_name, "#migration-test") == 0);
    assert(restored.fall_sensitivity == 5 && restored.fall_enabled == 0);

    // A rejected record must not partially modify preferences.
    for (size_t n = 0; n <= sizeof(record); ++n) {
        if (n == ShadowPrefs::record_size) continue;
        uint8_t before[ShadowPrefs::record_size];
        uint8_t after[ShadowPrefs::record_size];
        ShadowPrefs::encode(restored, before);
        assert(!ShadowPrefs::decode(restored, record, n));
        ShadowPrefs::encode(restored, after);
        assert(memcmp(before, after, sizeof(before)) == 0);
    }
    record[4] = 2;
    assert(!ShadowPrefs::decode(restored, record, ShadowPrefs::record_size));
    record[4] = 1;
    record[0] = 'X';
    assert(!ShadowPrefs::decode(restored, record, ShadowPrefs::record_size));
    record[0] = 'S';

    // The deployed 202-byte companion layout has the same payload at 163.
    uint8_t legacy[202]{};
    memcpy(legacy + 163, record + 5, ShadowPrefs::payload_size);
    initNodePrefs(&restored);
    ShadowPrefs::decodePayload(restored, legacy + 163);
    assert(restored.ui_timezone_offset_minutes == -345);
    assert(restored.tracking_interval_minutes == 65535);
    assert(restored.fall_sensitivity == 5 && restored.fall_enabled == 0);

    // Untrusted payload values are normalized by the shared sanitizer.
    memset(legacy + 163, 0xff, ShadowPrefs::payload_size);
    ShadowPrefs::decodePayload(restored, legacy + 163);
    sanitizeNodePrefs(&restored);
    assert(restored.tracking_group_name[31] == 0);
    assert(restored.fall_sensitivity == 3);
    assert(restored.fall_enabled <= 1 && restored.auto_shutdown_emergency <= 1);
    puts("shadow_prefs: PASS");
}
