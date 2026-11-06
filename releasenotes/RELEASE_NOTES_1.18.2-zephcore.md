# ZephCore 1.18.2-zephcore

Geofence alerts: the node tells the tracking group when it leaves a zone around a saved point and when
it comes back.

> [!NOTE]
> A normal upgrade keeps your identity, settings, contacts and phone pairing.

---

## Geofence (`geo`)

`geo save` stores the current GPS fix as the geofence point (the same wait-for-fix capture as
`wp save`), `set geo.point 43.241117 76.927301` sets it by hand — that is also the only coordinate edit
available from remote CLI. `get/set geo.radius <m>` sets one radius in meters for the whole zone
(default 3000, range 50–50000).

`geo on` watches the distance to the point on every fresh GPS fix. While it is on, the GPS stays
powered at duty 500 s. The GPS now has four users — tracking, nav, geo and sos: whoever leaves last
powers it down and returns `gps duty` to your configured value (which no feature overwrites, so it
survives reboots and any on/off order); with only geo left, its 500 s duty comes back. Tracking, nav
and sos outrank geo — `geo on|off` under them leaves the GPS alone. The toggle survives a
reboot: a stored `on` re-pins the duty at boot, and the first fix after boot reports the zone state
once — back inside below the radius sends the return message, still beyond sends the exit message.

When you leave the zone (the first fix beyond the radius) and when you return (below radius − 50
m), the node sends to the tracking group (`set tracking.group`, default `#tracks`):

```
🚪 Вышел из геозоны
📐 3км
📏 3.5км
📍 https://maps.google.com/?q=43.24112,76.92730
```

```
✅ Вернулся в геозону
📐 3км
📏 2.9км
📍 https://maps.google.com/?q=43.24112,76.92730
```

Both go out over LoRa only and hidden: they never appear in your own outgoing group feed and are not
sent to the v-contact, so the group is the one place that sees them. Nothing is signalled on the
device itself.

## Other changes

- **Save confirmations got a 💾**: `💾 Точка успешно сохранена!` for `wp save` and
  `💾 Центр геозоны сохранен!` for `geo save`, both still followed by the maps link.
- **Offline message queue 256 → 128 entries.** The queue holds messages waiting for the phone app;
  each entry costs 173 bytes of DRAM, so this frees 22 KB — the biggest single saving on DRAM-bound
  companions like the Heltec V3. 128 undelivered messages is still generous: the app drains them one
  by one after reconnect. Boards with their own value (T-Beam and ThinkNode M5 at 100) keep it.
- **`help geo` group after `nav`** in the app, remote and USB help, with a coordinate example; remote
  CLI accepts `geo`, `geo on|off`, `get/set geo.point` but not `geo save` or the radius.
