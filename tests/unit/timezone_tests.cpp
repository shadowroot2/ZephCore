#include "test.h"
#include <helpers/ui/ui_timezone.h>
#include <cstring>

TEST(ui_timezone_display, "UNIT-UI-002", "Display timezone labels fit E-Paper rows and local epochs stay bounded") {
    struct Zone { int16_t minutes; const char *label; };
    const Zone zones[] = {
        {0, "UTC"}, {300, "GMT+5"}, {330, "GMT+5:30"}, {345, "GMT+5:45"},
        {-210, "GMT-3:30"}, {630, "GMT+1030"}, {765, "GMT+1245"},
        {825, "GMT+1345"}, {-720, "GMT-12"}, {840, "GMT+14"},
    };
    for (const auto &zone : zones) {
        ui_set_timezone_offset_minutes(zone.minutes);
        char label[12];
        ui_timezone_format_label(label, sizeof(label));
        CHECK(std::strcmp(label, zone.label) == 0);
        CHECK(std::strlen(label) <= 8);
        const uint32_t utc = 1767225600U;
        CHECK(ui_local_epoch(utc) == utc + (int32_t)zone.minutes * 60);
        CHECK(ui_local_day_seconds(utc) == ui_local_epoch(utc) % 86400U);
    }
    ui_set_timezone_offset_minutes(-210);
    CHECK(ui_local_epoch(0) == 0);
    char tiny[1] = {'x'};
    ui_timezone_format_label(tiny, sizeof(tiny));
    CHECK(tiny[0] == 0);
    char untouched = 'x';
    ui_timezone_format_label(&untouched, 0);
    CHECK(untouched == 'x');
    ui_set_timezone_offset_minutes(300);
}
