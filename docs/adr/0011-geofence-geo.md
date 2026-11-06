# ADR 0011 — Geofence (`geo`): exit/return alerts over LoRa only

Status: accepted (recorded 2026-11-05)

## Context

- Feature request ("поводок"): alert when the node itself leaves a radius around one point.
- Decided 2026-11-05: (1) only the own distance to a point set by CLI — no group-node leash; (2) the alert goes
  to the tracking group as a hidden message (`sendGroupMessage` without `queueLocalAutomatedChannelMessage`),
  never shown in local outgoing; (3) one global radius, set in meters.
- Follow-ups the same day: a `geo` help group placed after `nav`; remote coordinate edits only via
  `set geo.point` (help shows a coordinate example); a return-to-zone message; all event messages LoRa-only
  (no vContact/BLE path); a `geo on|off` toggle available in every CLI, first in the group, pinning the GPS
  duty while on (GPS on, duty 500 s). 2026-11-06: the GPS duty ownership model was rewritten — tracking,
  nav, geo and sos are users of one GPS, the configured duty lives in `prefs.gps_interval` and only the
  last user out powers the GPS down; tracking, nav and sos outrank geo.
- Reused cores: `tracking_distance_m` and `nav_distance_ru_str` (TrackingMessage.h); the wait-for-fix save
  process of `wp save` (`companion_home_start_save`, main_companion.cpp); the GPS save/restore pattern of
  `companion_tracking_set_enabled` (main_companion.cpp:1501); the remote CLI allowlist
  `companion_remote_cli_command_allowed` (CompanionCLIHelp.h:223).

## Decision

**Scope.** Own position vs a single geofence point. No per-contact radii and no group leash — those would be
an ADR of their own.

**Point.** `geo save` is local-CLI-only (like `wp save`: it starts the same wait-for-fix capture; the default
coordinates are the home-slot defaults 43.241117 / 76.927301). `get/set geo.point <lat> <lon>` reads and
writes the point manually and is the only coordinate-edit path available over remote CLI; the help text shows
an example (`set geo.point 43.241117 76.927301`).

**Radius.** One global `set geo.radius <m>` — the help text states meters. Guard 50..50000, default 3000.
Radius is the zone size; the toggle below is the on/off switch.

**GPS duty ownership (2026-11-06).** The GPS has four users — tracking, nav, geo and sos. Each on
ensures the GPS is powered and sets its own duty (tracking and nav 0, geo 500 s); the configured duty
lives in `prefs.gps_interval` and no feature ever writes it, so it survives reboots by construction.
When a user leaves and one of the higher-rank three is still active, nothing is touched; when only geo
remains, its 500 s pin is re-asserted; with nobody left the GPS powers down and the live duty returns
to `prefs.gps_interval` (`companion_gps_user_left`). Tracking, nav and sos outrank geo:
`geo on`/`geo off` under them leaves the GPS and duty alone. After a reboot
tracking and nav are gone and the configured duty returns; geo stays on and re-pins 500 s.

**Toggle.** `geo on|off`, available in every CLI and the first line of the `geo` help group. On: GPS on
and duty 500 s when geo is the only user (see ownership above); a point save is refused meanwhile. While
geo owns the duty, `gps off` and `set gps duty` are refused (the same block tracking applies). The toggle
persists across reboots (`prefs.geo_on`): at boot a stored `on` re-pins the duty like `geo on`. The first
fix after boot reports the zone state exactly once — `✅ Вернулся в геозону` when inside,
`🚪 Вышел из геозоны` when beyond the radius.

**Point save.** `geo save` runs the exact `wp save` pipeline (`companion_home_start_save` /
`companion_home_save_process` with a geo target flag): force a fix, capture it, return the GPS to what it
was. One save at a time — `wp save` and `geo save` refuse each other.

**Events.** Evaluated per fresh fix (age <= 30 s) on the main-thread `MESH_EVENT_GPS_FIX` path next to
`companion_nav_fix_process`; `dist = tracking_distance_m(own, geo point)`. No fix, no evaluation.

- EXIT: the first fix with `dist > radius` -> one message.
- REENTER: `dist < radius - 50 m` -> one message, and the exit trigger re-arms.

No repeats while the state does not change.

**Messages (LoRa only).** Both go to the tracking group via `sendGroupMessage` through a new
`companion_send_hidden_group_message` — the same send as the visible helper minus
`queueLocalAutomatedChannelMessage`, so the alert never appears in the sender's local group feed. No
vContact, no BLE/USB text path. Nothing is signalled on the device itself.

```
🚪 Вышел из геозоны
📐 %s
📏 %s
📍 https://maps.google.com/?q=%.5f,%.5f

✅ Вернулся в геозону
📐 %s
📏 %s
📍 https://maps.google.com/?q=%.5f,%.5f
```

Both distance lines come from `geofence_dist_str` — the compact form the zone
messages use ("50м", "3км", "3.5км", unit glued to the value); the first is the
radius, the other is the distance at event time. Emojis are single constants. The
message is formatted into the real send budget (`MAX_TEXT_LEN` minus the
"name: " prefix `sendGroupMessage` adds) and drops the radius line first when it
does not fit, so the maps link is never cut.

**Help.** The `geo` group goes right after `nav` in LocalCLIHelp.h; the companion help lists the remote-allowed
subset with the coordinate example.

**Remote allowlist.** `geo`, `geo on`, `geo off`, `get geo.point`, `set geo.point ` — matched in code, not in
the allowlist tables (those pointers cost DRAM). `geo save`, `get geo.radius` and `set geo.radius ` stay
local/app only.

**Prefs (prefs.json/zc only; the legacy PrefsCodec is frozen).** `geo_lat_e7`, `geo_lon_e7` (degrees x 1e7),
`geo_saved`, `geo_on`, `geo_radius_m` — default point 43.241117 / 76.927301 with `geo_saved = 0`, toggle off,
default radius 3000 m,
range guards 50..50000. The fields sit in existing NodePrefs padding holes on purpose: NodePrefs rides in the
static mesh object and companion DRAM (Heltec V3) has single-digit bytes free, so the struct must not grow.

## Consequences

- Alert latency is bounded by the pinned duty: one duty wake (500 s) plus fix acquisition. Exit fires on
  the first fix beyond the radius (2026-11-06, was two — latency preferred over boundary debounce; the
  50 m re-arm hysteresis remains the chatter guard). That
  fits the 3 km use case; radii at the 50 m end will alert late — the fix is `set geo.radius` larger or a
  future duty knob, not silent per-board tuning.
- Hidden send means the sender's group feed has no record of its own alerts; the local log line is the trace.
- The configured duty is never written by features, so nested on/off orders and reboots cannot lose it;
  only `set gps duty` changes it. While tracking or nav holds duty 0 over geo, the leash rides on their
  fixes; when they leave and only geo remains, its 500 s pin is re-asserted.
- The GPS duty pin makes geo on/off a visible power trade-off: GPS stays powered at the 500 s cadence
  until `geo off` or a higher-rank user takes over.
