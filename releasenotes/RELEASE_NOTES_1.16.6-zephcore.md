# ZephCore 1.16.6-zephcore

> [!IMPORTANT]
> ## Before you upgrade
>
> **From v1.16.5** — clean flash, no re-bond, bonds and data survive.
>
> **From v1.16.2 / v1.16.3 / v1.16.4** — clean flash, bonds and data survive. If you are on an
> **ESP32-S3 / ESP32-C board** and have *not* yet taken the v1.16.5 update, you still owe the one-time
> serial reflash described there: the app moved to flash offset `0x10000` in v1.16.5, and a board on the
> old layout cannot cross that update over WiFi-OTA or the browser flasher — **flash the `-merged.bin`
> once over USB/serial.** Affected boards: *Heltec V3 / V4 / V4.3, Station G2, Wireless Tracker / V2,
> XIAO ESP32-S3 / C3 / C6, LilyGo T-Lora C6.* Identity, contacts, channels, prefs, and BLE bonds are
> preserved. nRF52, classic ESP32 (T-Beam / PICO-D4 / TTGO LoRa32), STM32WL, and native Linux are
> unaffected.
>
> **From v1.16.1 or older** — flash it; on first boot it clears BLE bonds automatically (identity,
> contacts, channels, prefs preserved). Re-bond your phone/desktop once.
>
> **Coming from Arduino MeshCore** — flash it; auto-formats on first boot (new identity, clean storage).
>
> Take this with a grain of salt — try the formatters if anything anomalous happens with your node.

---

A maintenance release: an important **admin-password fix**, **GPS standby power savings**, and the
removal of **Adaptive Power Control**.

## Highlights

### Fixed: uppercase letters in an admin password were silently lowercased

**If you set or changed an admin password on v1.16.5 and can no longer log in, this is why.** v1.16.5
introduced case-insensitive CLI keywords by lowercasing the first two words of every command before
matching them. That is safe for `get cad` — but `password <value>` puts the *value* in the second word,
so `password MyPass` was stored as `mypass`, and the password you typed afterwards never matched.
(`set guest.password <value>` has three words and was never affected.)

The normalizer has been removed and CLI commands are case-sensitive again, matching upstream Arduino
MeshCore. **If you are locked out, re-flash and reconfigure, or log in with the all-lowercase form of the
password you set.**

The one place autocapitalization genuinely hurts — typing commands to the V-Contact from a phone keyboard,
which capitalizes the first letter of every line — is handled narrowly instead: only character 0 of a
V-Contact chat line is folded. No command takes an argument at position 0, so no value can be touched.

### GPS standby now actually saves power

`CONFIG_PM_DEVICE` (device power management) is enabled for the first time, carefully scoped: system-managed
PM stays off, nothing suspends automatically, and every PM call is made explicitly from the main thread.
This buys two things:

- **The GNSS UART is suspended while GPS is off or in standby.** An armed nRF UARTE receiver keeps the
  high-frequency clock running — roughly **0.5–1 mA continuously on nRF52840** — even with the GPS module
  itself powered down. Nodes with GPS disabled in prefs were paying this for their entire uptime. The UART
  is resumed before every wake, so no NMEA is lost.
- **GNSS drivers that boot suspended now get resumed.** `gnss-nmea-generic` initializes suspended under
  device PM and never opens its data pipe until told to — this was the old "enabling PM breaks GPS" trap,
  now handled once at boot with retries.

(Only nRF UARTE ports are gated in. Other UART drivers are deliberately left alone — the saving is
UARTE-specific and their suspend/resume round-trip is unverified.)

### Removed: Adaptive Power Control

APC tried to save battery by lowering TX power when neighbors reported more signal margin than they
needed. It has been removed entirely, because the saving it chased is not one most nodes actually pay.

TX power is only spent at the instant of a transmission. A companion that sits idle for days and sends a
handful of messages spends almost nothing on transmit to begin with — its drain is BLE advertising, the
radio sitting in receive, and the MCU. APC could not meaningfully reduce that. On the nodes where transmit
volume *is* high enough to matter, the feature never got a clean run: its measurement was structurally
confounded (the echo it measured comes from the neighbor's own fixed-power transmission and does not
respond to your reduction), and the staleness timer that ramped power back up could not tell "our link
degraded" from "the mesh was quiet." Rather than ship another round of tuning on a feature with no
demonstrated payoff and a real downside — a node that reduces too far goes silent, and cannot detect that
it has — it's gone.

What changes for you:

- **`set tx apc` and `set`/`get apc.margin` are removed.** `set tx <dbm>` sets a fixed power, as it always
  did, and `get tx` now answers with a plain number (`> 22`) exactly like upstream Arduino MeshCore.
- **Nodes that had APC enabled now transmit at their configured `tx` power.** If you had turned it on,
  check that your configured power is what you actually want — it is now used verbatim.
- **The APC line is gone from the on-device radio display**, and the TX row now shows a single power
  value instead of an `effective/max` pair — there is only one number now, so it is only printed once.
- **Your saved settings are safe.** APC's two prefs bytes are kept reserved at their original offsets, so
  no stored configuration shifts and nothing else is misread on upgrade.

## Other fixes and improvements

- **Fixed: Heltec V3 failed to build from source** once device PM was enabled. The upstream Zephyr board
  definition force-enables the GPIO power-domain driver without the devicetree guard that normally comes
  with it, and ZephCore's overlay deliberately replaces both of that board's power domains with plain
  regulators — so the driver was being compiled for hardware that no longer existed in the devicetree, and
  tripped over a helper that only exists under a config it never turned on. The power-domain subsystem is
  now explicitly off for V3, which is simply the truth about that board. Binary releases were unaffected;
  this only bit source builds. (V4 / V4.3 use ZephCore-local board definitions and were never affected.)
- **Air530z GPS driver patch** comments corrected to match the new PM strategy.

## Recommended upgrade checklist

1. **Set an admin password on v1.16.5?** Try the all-lowercase form if you are locked out; otherwise
   re-set it after upgrading.
2. **ESP32-S3 / ESP32-C boards that skipped v1.16.5:** flash `-merged.bin` once over USB/serial. Data survives.
3. **Everything else, from v1.16.2 onward:** just flash — bonds and data survive.
4. **From v1.16.1 or older:** just flash — self-migrates on first boot; re-bond once.
5. **From Official MeshCore:** just flash — auto-formats on first boot.
6. **Anything odd?** Format first.
