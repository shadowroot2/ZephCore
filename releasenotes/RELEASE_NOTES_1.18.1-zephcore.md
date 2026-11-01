# ZephCore 1.18.1-zephcore

Firmware updates over Bluetooth for ESP32 companions, half-hour and 45-minute timezones on the
on-screen clock, the clock zone on repeater screens, and a Zephyr update.

> [!NOTE]
> A normal upgrade keeps your identity, settings, contacts and phone pairing.

---

## Bluetooth updates for ESP32 companions

ESP32 companions can now be updated from a phone over Bluetooth, the way nRF companions already
can. Send `start ota` (or pick DFU / BLE OTA on the device's screen menu): the node opens an
update window for 10 minutes and keeps running. Then, in **nRF Connect Device Manager** (iOS and
Android), connect to the node, pair with its PIN if asked, and upload the `<board>-update.bin`
from the release (choose "Confirm only"). The node restarts on the new firmware by itself and
keeps your identity, settings, contacts and pairing. An update takes about a minute from a phone.

- Use the `-update.bin`, not the `-merged.bin`: the merged file is for flashing over USB.
- The Nordic DFU app used for nRF boards does not work here; use nRF Connect Device Manager.
- The first update to this version still goes over USB or the web flasher. From then on, updates
  can go over Bluetooth.
- `stop ota` closes the window early. Only a phone paired with the node's PIN can upload.
- Available on the Heltec V4, V4.3, V4 R8 and Wireless Tracker V2, LilyGo T3S3, Meshnology W12,
  Station G2, and the XIAO ESP32-S3 and ESP32-C3. The Heltec V3, Heltec Wireless Tracker, LilyGo
  T-Lora C6 and XIAO ESP32-C6 do not have the memory for it next to their WiFi companion, and the
  classic ESP32 boards (T-Beam) cannot boot an update this way; they update over USB as before.

On boards without an over-the-air update, `start ota` used to answer as if it was rebooting into
an update mode and then did nothing; it now says that the build has no OTA. Selecting DFU on the
screen menu of an ESP32 board no longer blanks the screen and leaves the node in an undefined
state.

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

- **Repeaters and room servers now protect their cell from over-discharge.** The low-battery
  auto-shutdown a companion had (`get/set autoshutdown <mV>`) runs on the server roles too: below
  the threshold (default 3200 mV on nRF52, 0 elsewhere) the node powers off on the third low
  reading in a row, 30 s apart, and never while externally powered. A companion with an app
  connected announces the power-off and waits for the notice to be fetched; a server has nobody to
  tell and stores the reason instead, so `get pwrmgt.bootreason` reports `Low Voltage` on the next
  boot. The command is answered over USB only: a remote contact could otherwise switch protection
  off (`0`) or power a field node down (`5000`).
- **A ThinkNode M6 no longer reports 0% while 250 mV of battery are left.** Its discharge curve
  reaches 0% at 3200 mV — the auto-shutdown threshold — instead of 3450 mV, so an empty gauge means
  the node is about to power off rather than holding reserve the user cannot see. Nothing changes
  above 3500 mV; between 3450 and 3200 mV the reading is now 4% down to 0% instead of a flat 0%.
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
  and the coordinates lost — the retry now covers the immediate send paths too (no GPS hardware, or
  a fix already in hand) and stands down after 10 attempts instead of holding the receiver forever.
- **The power-on notice no longer arrives after the power-off notice.** A shutdown in the first
  seconds after boot left the boot notice armed, so the family saw the node switching off and then
  switching on; the boot notice is cancelled when a shutdown starts.
- **`nav stop` keeps its distance line and maps link.** The stop notice formed after the GPS was
  already released, so its distance line and link were dropped even with a fresh fix while the start
  notice had them.
- **The navigation target is pinned at start.** Re-selecting a point (`wp <n>`, double-tap cycle)
  during a route silently re-targeted it — wrong-way, reports and the arrival all followed the new
  point. The route keeps the point chosen at `nav start`, like a save keeps its slot; `wp clear` is
  now refused while navigating, since it would empty the slots the route targets. `nav start` on an
  unsaved point is refused too (`ERROR: point not saved`) instead of silently walking to the default
  Almaty intersection.
- **An app cannot switch the GPS behind a feature's back.** The binary companion command that sets
  `gps` and `gps_interval` had no lease check — a connected app could power the receiver off in the
  middle of navigation, tracking or a point save, or overwrite the running duty. It now answers with
  the same error as the CLI and the on-screen toggle while any of them holds the receiver.
- **A legacy wrapped geofence point migrates on device.** Before the point fields were signed, a
  southern or western coordinate was stored wrapped (e.g. `4102467296` for −19.25°). The reader
  saturated such a value to +180° on 32-bit boards — the fence landed in the wrong hemisphere after
  the upgrade; it now reads back the wrapped value as the signed point it was.
- **The geofence does not double-report around a zone change or a reboot.** Changing the radius or
  the point (and geo off/on) reset the tracker: staying outside re-fired `🚪 Вышел из геозоны`, an
  out-to-in move swallowed the `✅` return and lost the excursion seconds, and a boot report either
  dropped them or re-stamped the exit. The tracker now survives the change and resolves the next
  fix honestly — no repeat exit, no swallowed return — books the excursion it finds open at boot,
  and refuses `set geo.point` while a save is pending.
- **`nav start` now announces itself over LoRa too.** With `nav telemetry on`, the start notice
  (`🚀 Начало навигации к точке` with the point line and, on a fresh fix, the distance line and a
  maps link) goes to the tracking group like the stop notice already did. Before, the first LoRa
  news of a trip was the periodic report.
- **A tracking point that got through is no longer sent again.** The deferred re-send asked "did
  any neighbour repeat this flood?" 5 s after the send, but a repeat can land up to 10 s later:
  the answer was then "no" even though the receiver had the point, and the re-send arrived as a
  second message with the same coordinates. The verdict now waits the full 10 s repeat window, and
  the repeat count survives the internal fold, so a verdict never calls a delivered point unheard
  because the measurement got lost.
- **Small tracking fixes.** A full backlog ring no longer double-charges a dropped point when the
  verdict settles, the `tracking off` summary keeps its maps link whole under a long node name,
  `set tracking.interval` re-arms the backlog probe on the new timeline, and stored intervals clamp
  to the documented minimum (5 min) instead of jumping to the default.
- **Shutdown and emergency corners.** `poweroff`, `shutdown y` and `reboot` now send the family the
  power-off notice and drop the boot notice before silence (the button path already did), and an
  emergency still owing its final message gets one last send on every shutdown path instead of dying
  on the waiting notice. A remote silent `sos` no longer mutes a ringing fall alarm, a fresh fix is
  honoured even with the receiver off, a fall re-trigger keeps the running sequence (no waiting-notice
  spam, no postponed coordinates), and a fall acknowledgement keeps the 60 s re-detect cooldown.
- **Small fixes.** Arrival needs two consecutive in-radius fixes — one glitch no longer auto-stops a
  route. The stop notice respects the same gates as the start notice, so a family that never saw the
  start no longer sees the stop. A manual GPS session survives a feature taking and releasing the
  receiver. An unheard backlog probe retries on the 60 s cadence instead of starving to the next
  report window. The movement filter demands twice the threshold at standstill (a multipath jump no
  longer fakes a shifted point). `geo log`'s running tail counts only an open episode of a live
  geofence, and the deferred point clock wraps midnight instead of clamping to 00:00.
- **WiFi companions no longer freeze when the app stops reading.** If an app connected over WiFi
  stopped reading (for example in the middle of a contact sync), the node could stop answering on
  USB, Bluetooth and WiFi, and stay stuck as long as that connection stayed open. The node now
  drops such a connection after 2 seconds, and the app can reconnect.
- **`set leds.hb off` and `hb` now also stop the message blink.** Before, a node with the heartbeat
  LED off still flashed on every DM and channel message. `set leds on` with `set leds.hb off` now keeps
  a T1000-E dark in use while it still blinks three times when you switch it off with the buzzer muted;
  `set leds off` keeps it dark at power-off too.
- **Packets the radio never checked are no longer repeated.** A LoRa header can say a packet
  carries no CRC, and the radio then passes it on without checking it. MeshCore always sends a CRC,
  so such a packet is a misread header or noise; it is now dropped like a packet with a bad CRC, on
  every radio type. It is rare (one in about 48 000 receptions on a busy repeater) and it does not
  explain the damaged packet copies seen on map.meshcore.hu.
- **Zephyr** updated to `b70dbaf045e`. A GPS receiver that fails to start on the first try is now
  really tried again (the two retries did nothing before), and on boards with external flash a
  failed start of that flash is retried at the next mount instead of staying failed until reboot.
