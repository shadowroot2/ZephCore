# ZephCore 1.16.8-zephcore

> [!CAUTION]
> **This is a pre-release.** ESP32 light sleep is enabled by default on Heltec repeaters and has not
> been validated on hardware yet — see the light-sleep section for what to check and how to build it
> out. It is deliberately **not published to the apps.meshamerica.com configurator**, which keeps
> offering v1.16.7; flash this one manually from the assets below if you want to help test it.
> The nRF based boards should be fine.

> [!IMPORTANT]
> ## Before you upgrade
>
> **From v1.16.5 / v1.16.6 / v1.16.7** — clean flash, no re-bond, bonds and data survive.
>
> **From v1.16.2 / v1.16.3 / v1.16.4** — clean flash, bonds and data survive. If you are on an
> **ESP32-S3 / ESP32-C board** and have *not* yet taken the v1.16.5 update, you still owe the one-time
> serial reflash: the app moved to flash offset `0x10000` in v1.16.5, and a board on the old layout
> cannot cross that update over WiFi-OTA or the browser flasher — **flash the `-merged.bin` once over
> USB/serial.** Affected boards: *Heltec V3 / V4 / V4.3, Station G2, Wireless Tracker / V2, XIAO
> ESP32-S3 / C3 / C6, LilyGo T-Lora C6, ThinkNode M9.* Identity, contacts, channels, prefs, and BLE
> bonds are preserved. nRF52, classic ESP32 (T-Beam / PICO-D4 / TTGO LoRa32), STM32WL, and native
> Linux are unaffected.
>
> **From v1.16.1 or older** — flash it; on first boot it clears BLE bonds automatically (identity,
> contacts, channels, prefs preserved). Re-bond your phone/desktop once.
>
> **Coming from Arduino MeshCore** — flash it; auto-formats on first boot (new identity, clean storage).
>
> **Heltec ESP32 repeaters now light-sleep**, and their USB CLI stops answering 10 minutes after boot
> as a result. Reconnecting your terminal normally resets the node and gives you another 10 minutes;
> if it doesn't, power-cycle. Details in the light-sleep section below.
>
> **If your node's GPS went quiet after its first fix, this release fixes it.** A wrong bit in the
> sleep command put u-blox modules into permanent backup mode on boards without hardware GPS power
> control. Recovery needed a physical power cycle — a reboot was not enough. See the GPS section.
>
> **One CLI rename:** `set/get cad.probe.interval` is now `set/get probe.interval` — the same setting,
> renamed because one measurement now feeds both the noise floor and the CAD probe. Your stored value
> carries over; only the command name changed. `set/get agc.reset.interval` is gone (see below).
>
> Take this with a grain of salt — try the formatters if anything anomalous happens with your node.

---

A **power and radio-measurement release**. Repeaters no longer wake on a fixed 5 s tick — they sleep
until their next real deadline. Every ESP32 build drops to 80 MHz, and Heltec repeaters now light-sleep
between packets. The noise-floor sampler and CAD probe were unified into one measurement, the
SX126x RSSI/AGC calibration is now band-correct, and the observer role got a batch of fixes for settings
that silently never reached the hardware.

## Highlights

### Repeaters are now deadline-driven — the periodic housekeeping tick is gone

Every repeater woke five times a second-and-a-bit forever: a fixed 5 s "housekeeping" timer that ran the
noise-floor sample, the CAD probe, advert timers and the tempradio revert. None of that actually needs a
poll — each item is either a deadline or a sampler with its own interval.

The new `mesh/Maintenance.h` contract lets the event loop ask *"when does maintenance next have work?"*
and arm **one one-shot wake for exactly that moment**. An idle repeater now sleeps until its soonest real
deadline instead of on a fixed cadence. A hard ceiling (`CONFIG_ZEPHCORE_MAINTENANCE_BACKSTOP_MS`,
default **60 s**) clamps the wake as a safety net against a missed deadline — it should never bind in
normal operation.

Companions and room servers keep their periodic tick deliberately: the companion block contains genuine
pollers (contact-dump progress, BLE advertising watchdog) with no deadline to arm, and the room server is
paced by its own 500 ms push timer regardless.

### ESP32 power: 80 MHz everywhere, and light sleep on Heltec repeaters

Espressif parts have no equivalent of the nRF52's "idle at microamps in WFI" — `WAITI` gates the core
clock but leaves PLL, peripherals and RAM powered, so an idle ESP32 sits in the **tens of mA**. CPU
frequency is therefore a first-order term in a battery node's draw, and light sleep (sub-mA) is what
removes the rest.

- **Every ESP32 build is now pulled to 80 MHz**, matching what Arduino MeshCore ships (new
  hal-espressif patch `0002` — upstream omits 80 from its DT map). **Observers are the exception** and
  keep the SoC maximum (240 MHz on S3/classic, 160 MHz on C3/C6): they run a full WiFi + TLS + MQTT
  stack and are mains- or big-panel powered, so throughput matters more than current.
- **Light sleep is now enabled automatically** for **repeater** builds on **Heltec V3, V4, V4.3,
  Wireless Tracker and Wireless Tracker V2**. Patch `0012` marks the LoRa DIO1 line as a wakeup source,
  so a received packet wakes the SoC — the node sleeps between packets and deadlines and wakes for real
  work. Combined with the deadline-driven change above, an idle repeater now spends most of its life
  asleep instead of waking five times a second at 240 MHz.

> [!IMPORTANT]
> **The USB CLI stops answering 10 minutes after boot on these boards.** Nothing arms a UART wake
> source on ESP32, so characters typed at a sleeping node are dropped. To make configuration reliable,
> light sleep is **blocked for the first 10 minutes after every boot**
> (`CONFIG_ZEPHCORE_PM_BOOT_AWAKE_MS`) — and because these boards' USB bridge drives EN from DTR,
> **connecting a terminal normally resets the node and re-arms that window.** If your terminal doesn't
> toggle DTR, power-cycle the node to get a console. Remote admin over LoRa is unaffected.

Console output itself is safe: a new `pm_notifier` flushes the console UART to idle before every sleep,
which Zephyr's PM path does not do — without it, lines would be cut mid-character. Packet-logging and
debug builds are therefore fine to run with sleep enabled.

Anything that can't tolerate sleeping holds a `pm_policy` lock instead of being excluded by hand: a
**WiFi OTA session** holds one for its duration, an **uplink/observer WiFi station** holds one
permanently (the association would drop), and **GPS holds one while the module is powered**, since the
GNSS UART is not a wake source and inbound NMEA would be lost. Under a GPS duty cycle the lock is only
held during the awake phase.

> [!WARNING]
> **Not yet validated on hardware**, and now on by default for the boards above — so this is the first
> thing to suspect if an allowlisted board misbehaves. The failure mode to rule out is a **deaf
> repeater**: check the boot log for `Pin N is not wakeup capable`, confirm the node still receives
> packets, and only then measure current. Rebuild with `-DCONFIG_PM=n` to take it back out.
>
> Boards **deliberately excluded** because DIO1 can't wake the SoC: XIAO ESP32-S3 (GPIO39), Station G2
> (GPIO48), ThinkNode M9 (GPIO42), LilyGo T-Lora C6 (GPIO23, outside the C6's LP range), and
> TTGO LoRa32 (SX127x — patch 0012 only arms the sx126x driver). **Companion builds never get it** on
> any board: the Espressif HCI driver takes no `pm_policy` locks, so the SoC could sleep mid-advertising
> or mid-connection. Passing `pm_esp32.conf` by hand still works for testing another board.

### One measurement now serves both the noise floor and CAD — `set probe.interval`

The noise-floor sampler and the CAD calibration probe used to be two separate things riding the same
5 s tick. They are now a **single periodic radio measurement**: one median-of-8 RSSI reading that the
CAD probe consumes rather than measuring again.

- `set cad.probe.interval <sec>` → **`set probe.interval <sec>`** (default still **15**, `0` still
  disables CAD probing). It is now also the noise-floor sampling rate, and it sets how often an idle
  repeater wakes.
- New `CONFIG_ZEPHCORE_NOISE_FLOOR_INTERVAL_MS` is the build-time fallback used when probing is off —
  15 s on repeaters, 5 s on companions/room servers (which wake on their tick anyway).
- A sample blocked by TX, mid-RX or a duty-cycle sleep window retries on a 5 s deadline (max 2 retries)
  instead of losing a full interval — deliberately reproducing the old retry grid, which matters under
  RX duty cycle where blocked attempts are the norm.

**RSSI reads are now correctly spaced.** A median only rejects outliers if the reads are independent,
and the chip refreshes RSSI once per averaging window (~16 µs at BW 62.5, ~134 µs at BW 7.8) — reads
issued faster return the *same* underlying sample and the median collapses to a single read. The spacing
and the RX-entry settle delay are now derived from the SX1261/2 datasheet (rev 2.2, Table 13-82).

`get cad` reports the result so the assumption stays falsifiable on real hardware: the old `iv:` field is
replaced by **`sp:<mean-spread>/<zero-spread %>(<count>)`**. A **non-zero mean proves the reads are
independent**, however high the zero-spread share climbs — only mean `0.0` with a high share indicts the
sampler. Measured on air at BW 62.5: `0.6/90%` quiet, `0.9/84%` with the floor at −103 dBm.

### GPS: module configuration was never actually running — multi-constellation now works

The whole boot-time GNSS configuration path (constellations, AssistNow/EASY, elevation mask, fix rate)
was **dead code**. A feature macro was used ~500 lines before it was defined, and an undefined
identifier in `#if` is silently zero, so every board on the generic-NMEA driver ran on whatever its
module happened to default to. Fixing the ordering exposed several further problems, all of which had
been masked by the code never executing:

- **u-blox `UBX-CFG-GNSS` was malformed.** Config blocks were written 7 bytes long instead of 8 (the
  `reserved0` field was missing from every block), so the receiver rejected the whole message. Rebuilt
  correctly, and now limited to three concurrent major constellations, which is the M8 hardware limit.
- **`UBX-CFG-NAV5` had a one-byte checksum error**, so the elevation mask was silently discarded.
- **Galileo could not be reported at all** until the NMEA version is raised to 4.10 — the `$GAGSV`
  talker does not exist below it. Now sent before the constellation config.
- **CASIC modules (Quectel L76K/L76KB, Air530Z) received no configuration whatsoever.** The generic
  path spoke only PMTK and UBX, and CASIC parts understand neither. It now speaks PCAS as well, so a
  L76K in a WisBlock slot is finally configured instead of sitting on its GPS+BeiDou factory default.
  **If your Wio Tracker L1 has been tracking noticeably fewer satellites than it should, this is why.**
- **The NMEA output stream was being over-run.** At 9600 baud a multi-GNSS receiver emits more than the
  link can carry once GSA and GSV multiply per constellation, and the failure mode is not "slow GPS"
  but a receiver that appears dead. The output is now trimmed to what the firmware actually parses.
- **BeiDou satellites were being discarded by the parser** — the pre-4.10 `$BDGSV` talker was not
  recognised, so BeiDou always read as zero even while tracking normally.
- **The navigation dynamic model is now set explicitly.** It is stored *in the module* and survives
  reflashing the host, so a second-hand module can arrive stuck in an airborne or automotive model.
  Repeaters and room servers get stationary; everything else automotive.

### Fixed: GPS sleep put u-blox modules to sleep permanently

`UBX-RXM-PMREQ` was sent with the wrong wake-source bit — `extint0` instead of `uartrx` — combined with
an infinite duration. On boards with no hardware GPS power control the module was told to enter backup
mode forever, wakeable only by a pin that is not routed on the RAK12500. **The wake byte we sent on the
UART could never wake it, and backup also powers down the I2C interface, so the module went completely
silent on every interface.** A reboot did not help either, because the supply rail is `regulator-boot-on`
and never toggles — only physically removing power recovered it.

In practice: any affected node got one fix after boot, then lost its GPS until someone power-cycled the
hardware. Fixed, and confirmed on hardware — the module now enters backup and wakes on the next duty
cycle with a ~3 second hot start.

### New: `set gps diag` / `get gps diag`

GNSS configuration is written blind — nothing in the protocol path confirms the module accepted it, and
a module with no sky view, one that is silent, and one stranded at the wrong baud rate all look
identical to the firmware. `set gps diag 1` re-runs configuration on the next `gps off` / `gps on`, and
`get gps diag` reports what happened:

```
> diag=on cfg=uart age=216s rx=559 mod=u-blox sent=17/464B sys=G9/R8/E6/B0/?0
```

`rx=` is parsed NMEA sentences (non-zero means the module is genuinely talking), `mod=` is the module
identity if it answered a version query, and `sys=` is tracked satellites per constellation, which is
the only real proof the constellation config took. Not persisted — it clears on reboot.

`CONFIG_ZEPHCORE_GPS_NMEA_DUMP` (see `boards/common/gps_debug.conf`) additionally logs every NMEA
sentence to the console for field diagnosis.

### Fixed: SX126x RSSI/AGC calibration used the wrong band on sub-GHz builds

Datasheet rev 2.2 (§6.1.6, new in Dec 2024) documents that the SX126x RSSI/AGC calibration registers are
**band-specific**. ZephCore applied the 868–915 MHz set unconditionally, so every 433 MHz build was
running the wrong AGC gain selection — that is **packet loss, not just wrong telemetry**. New patch
`0011` adds the per-band table plus the shared-register `0x08AC` / `AgcSensiAdjust` fix.

### RAK3401 1-watt: external-PA RX register patch enabled

The undocumented register `0x8B5` RX improvement (`CONFIG_ZEPHCORE_SX126X_HELTEC_REG_PATCH`) is not
Heltec-specific despite its name — upstream MeshCore also enables it for the RAK3401's SKY66122 FEM. It
is now on for `rak3401_1watt`, and the Kconfig help was corrected to say "boards with an external PA/FEM
in the RX path."

### The AGC-reset workaround is gone

`set/get agc.reset.interval` has been **removed** — it now replies `use rxduty instead`, and `set` is
accepted-and-ignored for prefs compatibility. It was a periodic blind AGC kick that only existed because
of the fixed tick; the in-driver RX restore and duty-cycle re-arm paths handle the real cases.

### Observer role fixes

Several observer settings looked accepted and then never reached the hardware:

- **`set freq/sf/bw/cr` never reached the radio.** The radio was bound to a static placeholder
  `NodePrefs` that is never loaded from flash, so it stayed on compiled-in defaults forever — the value
  was written to flash and echoed back by the CLI and MQTT, then "reverted" on reboot. The radio is now
  bound to the mesh's live prefs before `begin()`, matching repeater and room server.
- **First boot discarded the observer defaults.** The store is shared with the repeater, and its no-file
  branch re-applies *repeater* defaults over whatever the caller passed in. The observer now detects
  first boot itself and re-applies (and persists) its own `cr` / `tx_power` once.
- **`set name Observer` reverted on every reboot** — "Observer" was in the regenerate-if-unset list. It
  is a name a user can legitimately set, so it is no longer treated as unset.
- **`set freq` / `set bw` now refuse values the loader will reject** (300–1000 MHz, 7–500 kHz). Out-of-
  range values used to save fine, then get silently reset to defaults on the next boot — taking
  `bw`/`sf`/`cr`/`tx_power` with them.
- **USB-console routing fixed** on Station G2 and XIAO ESP32-S3, so non-companion roles keep their
  console and serial CLI.
- The observer `help` banner was reorganised (network / node / radio / query) and now lists the query
  commands that already existed.

## Upstream sync

Synced with Arduino MeshCore `dev`:

- **Room server: `room.post <message>`** — post to the shared room as the server itself. Pushed to
  clients like any other post.
- **Fixed: an empty guest password granted read+write to anyone.** An unset `guest.password` (the
  default) now *disables* guest login instead of matching an empty submitted password. To run an open
  room server use `set allow.read.only on`, which grants read-only. Both password compares still run
  unconditionally, so timing behaviour is unchanged.
- **Companion telemetry: MCU die temperature is now reported under base permission**, matching Arduino
  MeshCore and ZephCore's own repeater/room-server telemetry. It is still suppressed when an external
  sensor already supplied a temperature, so a node never emits two.
- **Advert names are truncated on a UTF-8 code-point boundary** — a name whose multi-byte character
  straddled the advert size limit used to go out mangled.
- **ThinkNode M3: GPS reset pin left floating.** Driving the REINIT pin (P0.25) stops the module
  producing NMEA on this hardware; upstream fixed it by never driving it. The `gps-reset` alias is gone,
  which compiles out every reset path — `GPS_EN` alone handles wake/standby.
- **New patch `0013` bounds the SX126x RX-busy latch with a payload deadline.** Continuous RX has no
  symbol timer, so a `HEADER_VALID` whose packet never completes would pin the TX gate open forever and
  silently mute the node. The bound is max-length airtime at the current SF/BW +25% +100 ms —
  deliberately generous, a stuck-state safety net rather than a timing mechanism.

## Other changes

- **west manifest bumped** (routine pinned-tree advance).
- `ARCHITECTURE.md`, `ADAPTIVE_CAD.md` and `Repeater_CLI_commands.md` updated for the probe unification
  and the removed commands.

## Recommended upgrade checklist

1. **Heltec V3 / V4 / V4.3 / Wireless Tracker repeater?** You get 80 MHz *and* light sleep
   automatically. Do your configuration in the first 10 minutes after boot, or reconnect your terminal
   (which resets the node) to get the window back.
2. **Running a 433 MHz SX126x node?** Update — the RSSI/AGC calibration band fix affects reception, not
   just readings.
3. **Running an observer?** Update, then re-apply your radio settings once — they may never have reached
   the hardware before.
4. **Scripts or notes using `cad.probe.interval` / `agc.reset.interval`?** Update them to
   `probe.interval`; the AGC command is gone.
5. **ESP32-S3 / ESP32-C boards that skipped v1.16.5:** flash `-merged.bin` once over USB/serial. Data survives.
6. **Everything else, from v1.16.2 onward:** just flash — bonds and data survive.
7. **From v1.16.1 or older:** just flash — self-migrates on first boot; re-bond once.
8. **From Official MeshCore:** just flash — auto-formats on first boot.
9. **Anything odd?** Format first.
