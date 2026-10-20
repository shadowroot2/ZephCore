// Read-only migration checks for the legacy SHPF companion tail.
#define CONFIG_ZEPHCORE_UI_TIMEZONE_OFFSET_MINUTES 0
#include <helpers/ShadowPrefs.h>
#include <assert.h>
#include <stdio.h>

int main()
{
    uint8_t record[ShadowPrefs::record_size] = {'S', 'H', 'P', 'F', 1};
    uint8_t *payload = record + 5;
    payload[2] = 1;
    payload[3] = 30;
    memcpy(payload + 5, "#migration-test", sizeof("#migration-test"));
    payload[37] = 5;
    payload[38] = 1;

    NodePrefs restored{};
    initNodePrefs(&restored);
    assert(ShadowPrefs::decode(restored, record, ShadowPrefs::record_size));
    assert(restored.auto_shutdown_emergency == 1);
    assert(restored.tracking_interval_minutes == 30);
    assert(strcmp(restored.tracking_group_name, "#migration-test") == 0);
    assert(restored.fall_sensitivity == 5 && restored.fall_enabled == 1);

    // A rejected record must not partially modify preferences.
    NodePrefs before = restored;
    assert(!ShadowPrefs::decode(restored, record, sizeof(record) - 1));
    assert(memcmp(&restored, &before, sizeof(restored)) == 0);
    record[4] = 2;
    assert(!ShadowPrefs::decode(restored, record, ShadowPrefs::record_size));
    record[4] = 1;
    record[0] = 'X';
    assert(!ShadowPrefs::decode(restored, record, ShadowPrefs::record_size));
    puts("shadow_prefs: PASS");
}
