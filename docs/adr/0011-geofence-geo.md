# ADR 0011 — Geofence (`geo`): exit/return alerts over LoRa only

Status: accepted (recorded 2026-11-05)

## Context

- Feature request ("поводок"): alert when the node itself leaves a radius around one point.
- Decided 2026-11-05: (1) only the own distance to a point set by CLI — no group-node leash; (2) the alert goes
  to the tracking group as a hidden message (`sendGroupMessage` without `queueLocalAutomatedChannelMessage`),
  never shown in local outgoing; (3) one global radius, set in meters.
- Follow-ups the same day: a `geo` help group placed after `nav`; remote coordinate edits only via
  `set geo.point` (help shows a coordinate example); a return-to-zone message; all event messages LoRa-only
  (no vContact/BLE path); a `geo on|off` toggle available in every CLI, first in the group, owning the GPS
  duty while on (save state → GPS on, duty 300 s → restore on off).
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
an example (`set geo.point 43.24112 76.92730`).

**Radius.** One global `set geo.radius <m>` — the help text states meters. Guard 50..50000, default 3000.
Radius is the zone size; the toggle below is the on/off switch.

**Toggle.** `geo on|off`, available in every CLI and the first line of the `geo` help group. On: save the
current GPS enabled state and duty, enable GPS, set duty to 300 s. Off: restore both (refused while an SOS is
waiting for a fix, same as `tracking off`). Mutual guard with tracking — one owner of the GPS duty at a time:
`geo on` refuses while tracking is on and `tracking on` refuses while geo is on. While geo is on, `gps off`
and `set gps duty` are refused (the same block tracking applies); a toggle is refused while navigating or
while a point save is running. Toggle state is runtime, like tracking and nav; point and radius persist.

**Point save.** `geo save` runs the exact `wp save` pipeline (`companion_home_start_save` /
`companion_home_save_process` with a geo target flag): force a fix, capture it, return the GPS to what it
was. One save at a time — `wp save` and `geo save` refuse each other.

**Events.** Evaluated per fresh fix (age <= 30 s) on the main-thread `MESH_EVENT_GPS_FIX` path next to
`companion_nav_fix_process`; `dist = tracking_distance_m(own, geo point)`. No fix, no evaluation.

- EXIT: 2 consecutive fixes with `dist > radius` -> one message.
- REENTER: `dist < radius - 50 m` -> one message, and the exit trigger re-arms.

No repeats while the state does not change.

**Messages (LoRa only).** Both go to the tracking group via `sendGroupMessage` through a new
`companion_send_hidden_group_message` — the same send as the visible helper minus
`queueLocalAutomatedChannelMessage`, so the alert never appears in the sender's local group feed. No
vContact, no BLE/USB text path. Local feedback is a buzzer/UI signal only, not a message.

```
🚪 Вышел из геозоны (> %s)
📏 Расстояние: %s
📍 https://maps.google.com/?q=%.5f,%.5f

✅ Вернулся в геозону (< %s)
📏 Расстояние: %s
📍 https://maps.google.com/?q=%.5f,%.5f
```

Both `%s` distances come from `nav_distance_ru_str` ("3.00 км"); the first is the radius, the second the
distance at event time. Emojis are single constants.

**Help.** The `geo` group goes right after `nav` in LocalCLIHelp.h; the companion help lists the remote-allowed
subset with the coordinate example.

**Remote allowlist.** `geo`, `geo on`, `geo off`, `get geo.point`, `set geo.point ` — matched in code, not in
the allowlist tables (those pointers cost DRAM). `geo save`, `get geo.radius` and `set geo.radius ` stay
local/app only.

**Prefs (prefs.json/zc only; the legacy PrefsCodec is frozen).** `geo_lat_e7`, `geo_lon_e7` (degrees x 1e7),
`geo_saved`, `geo_radius_m` — default point 43.241117 / 76.927301 with `geo_saved = 0`, default radius 3000 m,
range guards 50..50000. The fields sit in existing NodePrefs padding holes on purpose: NodePrefs rides in the
static mesh object and companion DRAM (Heltec V3) has single-digit bytes free, so the struct must not grow.

## Consequences

- Alert latency is bounded by the pinned duty: up to 2 x 300 s of confirmation plus fix acquisition. That
  fits the 3 km use case; radii at the 50 m end will alert late — the fix is `set geo.radius` larger or a
  future duty knob, not silent per-board tuning.
- Hidden send means the sender's group feed has no record of its own alerts; the local log line is the trace.
- Duty restore assumes geo and tracking never run together (mutual guard) and SOS restore runs first
  (existing guard on the restore path).
- The GPS duty pin makes geo on/off a visible power trade-off: GPS stays powered at 5 min cadence until
  `geo off`.
