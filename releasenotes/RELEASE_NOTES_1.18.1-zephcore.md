# ZephCore 1.18.1-zephcore

Half-hour and 45-minute timezones on the on-screen clock, the clock zone on repeater screens, and a
Zephyr update.

> [!NOTE]
> A normal upgrade keeps your identity, settings, contacts and phone pairing.

---

## Half-hour timezones on the clock

`set tz.offset` now takes half and quarter hours, as upstream MeshCore now does (after its 1.18.0):
`set tz.offset 5.5` for India (+5:30), `5.75` for Nepal (+5:45), `-3.5` for Newfoundland. The
display shows the zone as `UTC+5:30`. Values that are not a whole quarter hour are refused, and
`get tz.offset` now always prints at least one decimal (`2.0`, `5.75`). As before, only the
on-screen clock moves; the node's own clock and every timestamp it sends stay UTC.

Repeaters and room servers with a screen now show their clock in the zone you set. Before, they
accepted `set tz.offset` but kept showing UTC.

If you go back to an older ZephCore, a half-hour setting reads as the whole hour below it (5.5
becomes 5).

## Other changes

- **A waypoint save lands on the point it was asked for, and a set name survives it.** The save
  captured its target when the GPS fix arrived, so re-selecting points while `Waiting GPS FIX...`
  was up could write the coordinates into another slot and name that slot `SAVED`. The target is
  now pinned when the save starts. A name you set (say `2-HOME`) is never overwritten — only an
  unnamed `FREE` slot becomes `SAVED`.
- **`send.heard <ts>` on a companion answers how many neighbours repeated a channel send.**
  Send a channel message from a companion app, then ask `send.heard <ts>` with the message
  timestamp the app sent: the reply `> <n>` is how many nodes retransmitted that flood in the
  first seconds after the send. `0` means nobody repeated it yet, `-1` means the timestamp is
  unknown or the measurement already expired. One-shot — the stored send is consumed — and best
  queried a few seconds after sending.
- **A point save no longer fights the other GPS users.** The save force-leases the GPS for its fix,
  but the last feature to stop could switch the receiver off under it, and the save's restore of the
  pre-save state could kill the GPS of a `tracking`/`nav`/`sos` that started while the fix was
  pending. The save now counts as a GPS user and hands the receiver back to whoever needs it now.
  `nav start` is refused while a save is running, the screen's save button does nothing while
  navigating (as `wp save` already refused), and `gps off`/`set gps duty` are refused — naming the
  user — while any of tracking, navigation, sos, a save or geo needs the receiver.
- **The geofence works below the equator, on small radii and with garbage input.** The saved point
  was stored unsigned, so a southern or western coordinate wrapped (a Sydney fence came out as
  `395.62793`); the fields are signed now and old wrapped values read back correctly. At the 50 m
  minimum radius the fixed 50 m return hysteresis left no return band at all — `✅ Вернулся в
  геозону` could never fire — so the hysteresis now clamps to half the radius. `set geo.point
  nan,nan` used to be accepted and corrupt the point; non-finite values are refused.
- **A fall alarm can be stopped and its emergency message is never dropped.** `fall off` could not
  stop a delivered fall's alarm (the guard looked at the sos state, which the final send already
  cleared), so `MELODY_SOS` rang every 30 s forever; it now stops the alarm unconditionally and
  cancels a live fall with `❌ Падение отменено` and a sound, like the button does. And when the
  radio queue refused the final emergency message (pool empty, `#sos` missing) the state was wiped
  and the coordinates lost — the send now retries every 30 s until it lands.
- **The power-on notice no longer arrives after the power-off notice.** A shutdown in the first
  seconds after boot left the boot notice armed, so the family saw the node switching off and then
  switching on; the boot notice is cancelled when a shutdown starts.
- **`nav stop` keeps its distance line and maps link.** The stop notice formed after the GPS was
  already released, so its distance line and link were dropped even with a fresh fix while the start
  notice had them.
- **The navigation target is pinned at start.** Re-selecting a point (`wp <n>`, double-tap cycle)
  during a route silently re-targeted it — wrong-way, reports and the arrival all followed the new
  point. The route keeps the point chosen at `nav start`, like a save keeps its slot.
- **An app cannot switch the GPS behind a feature's back.** The binary companion command that sets
  `gps` and `gps_interval` had no lease check — a connected app could power the receiver off in the
  middle of navigation, tracking or a point save, or overwrite the running duty. It now answers with
  the same error as the CLI and the on-screen toggle while any of them holds the receiver.
- **A legacy wrapped geofence point migrates on device.** Before the point fields were signed, a
  southern or western coordinate was stored wrapped (e.g. `4102467296` for −19.25°). The reader
  saturated such a value to +180° on 32-bit boards — the fence landed in the wrong hemisphere after
  the upgrade; it now reads back the wrapped value as the signed point it was.
- **`nav start` now announces itself over LoRa too.** With `nav telemetry on`, the start notice
  (`🚀 Начало навигации к точке` with the point line and, on a fresh fix, the distance line and a
  maps link) goes to the tracking group like the stop notice already did. Before, the first LoRa
  news of a trip was the periodic report.
- **A tracking point that got through is no longer sent again.** The deferred re-send asked "did
  any neighbour repeat this flood?" 5 s after the send, but a repeat can land up to 10 s later:
  the answer was then "no" even though the receiver had the point, and the re-send arrived as a
  second message with the same coordinates. The verdict now waits the full 10 s repeat window.
- **WiFi companions no longer freeze when the app stops reading.** If an app connected over WiFi
  stopped reading (for example in the middle of a contact sync), the node could stop answering on
  USB, Bluetooth and WiFi, and stay stuck as long as that connection stayed open. The node now
  drops such a connection after 2 seconds, and the app can reconnect.
- **`set leds.hb off` and `hb` now also stop the message blink.** Before, a node with the heartbeat
  LED off still flashed on every DM and channel message. `set leds on` with `set leds.hb off` now keeps
  a T1000-E dark in use while it still blinks three times when you switch it off with the buzzer muted;
  `set leds off` keeps it dark at power-off too.
- **Zephyr** updated to `b70dbaf045e`. A GPS receiver that fails to start on the first try is now
  really tried again (the two retries did nothing before), and on boards with external flash a
  failed start of that flash is retried at the next mount instead of staying failed until reboot.
