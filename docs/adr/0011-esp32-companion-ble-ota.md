# ADR 0011 — ESP32 companions update over BLE with MCUmgr

Status: accepted (2026-10-26)

## Context

nRF52 companions update over BLE through the Adafruit bootloader: `start ota` (or the buttonless DFU
characteristic, or the UI's DFU page) resets into the bootloader, and the Nordic DFU app uploads a `.zip`.
ESP32 companions had no BLE update path. WiFi OTA is repeater-only: on a companion it costs about 107 KB of
DRAM, which an S3 without PSRAM does not have. `start ota` on an ESP companion replied "rebooting to BLE OTA
DFU" and did nothing, and the button UI's DFU page ran into `CODE_UNREACHABLE`.

S3/C-series release images already boot through MCUboot (ADR 0001) with a slot1 the size of slot0, and ship
the signed app as `<board>-update.bin`. Writing slot1 from the running app and letting MCUboot copy it is the
same mechanism WiFi OTA uses.

Options considered:
- **MCUmgr SMP over BLE** (Zephyr's own transport, image and OS groups), uploaded with nRF Connect Device
  Manager or any MCUmgr client.
- **Nordic Legacy DFU receiver in the app** (service `0x1530`), so the Nordic DFU app and an
  adafruit-nrfutil `.zip` would work as on nRF52. Several hundred lines of protocol code ZephCore would own.
  Rejected by the architect: MCUmgr is enough.
- **WiFi OTA on companions.** Does not fit the no-PSRAM boards.

## Decision

ESP32 companions built with `--sysbuild` (S3/C-series, every release) carry MCUmgr SMP over BLE
(`boards/common/ble_ota.conf`, auto-included by CMake; `adapters/ota/ble_ota.c`):
- the SMP GATT service is registered at runtime only while an OTA window is open (`start ota`, the UI's
  DFU/BLE OTA entry; 600 s, kept open while an upload runs; `stop ota` closes it), so the boot GATT table and
  the handles bonded phones have cached do not change;
- the characteristic needs an authenticated link (passkey bond, `MCUMGR_TRANSPORT_BT_PERM_RW_AUTHEN`), and
  uploads outside the window are refused;
- the payload is the existing `<board>-update.bin` (or an NCS-style `.zip` around it); MCUboot copies slot1
  over slot0 on the reset the client sends (overwrite-only, as WiFi OTA);
- 1 KB SMP buffers, four of them, progressive erase.

A board without the DRAM opts out with `ble_ota: false` in its `zephcore.yml`. On 2026-10-26 that is the four
no-PSRAM WiFi companions (Heltec V3, Wireless Tracker, LilyGo T-Lora C6, XIAO ESP32-C6): they already run at
the largest contact count that links (ADR 0009) and BLE OTA needs about 6 KB more. The architect chose keeping
their contacts over BLE OTA. Classic ESP32 boards have no MCUboot and no BLE OTA.

## Consequences

- About 7.5 KB DRAM and 14 KB flash per capable companion. Nine of the thirteen ESP release companions have it.
- The first BLE-OTA-capable image still goes on over USB or the configurator; later updates can go over BLE.
- Bonded phones get a Service Changed when the window opens or closes; they rediscover.
- Speed depends on the central: about 24 KB/s from an iPhone (940 KB in about 40 s, 2475-byte buffers),
  2.7 KB/s from the bench PC's USB dongle.
- No Nordic DFU app support on ESP. No signing: MCUboot checks the image hash only (as WiFi OTA, ADR 0001's
  `BOOT_SIGNATURE_TYPE_NONE`); the bond and the window are the access control.
- `start ota` on a build with no OTA path now answers an error and never reboots.
