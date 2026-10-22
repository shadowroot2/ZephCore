# ZephCore 1.18.0-zephcore

Safer off-grid mode, working buttons on four boards with a display, a WiFi page on the display of
WiFi companions, a GPS clock fix, a new KISS modem role, and the changes from upstream MeshCore 1.18.0.

> [!NOTE]
> A normal upgrade keeps your identity, settings, contacts and phone pairing.

---

## Off-grid mode stays off the live mesh

Off-grid mode makes a companion forward packets for nearby handhelds when no repeater is in reach. It
belongs on 433.000, 869.495 or 918.000 MHz, but the display's Offgrid toggle turned forwarding on
without changing the frequency, so the node repeated everything on your normal mesh.

- **Turning Offgrid on now moves the node** to the nearest off-grid frequency; turning it off returns
  to the frequency you came from. Bandwidth, SF, CR and TX power are kept.
- **A companion never forwards anywhere else.** If you enabled Offgrid from the display before, it
  shows as off after the update; turn it on again if you want it.
- **`set repeat on|off` works on a companion** and does the same as the toggle. The display also
  follows changes made from the app.

More in [OFFGRID.md](../docs/OFFGRID.md). Thanks to **N8HOP** for the report.

## Button notices in vContact

When a companion is turned off with its button and an app is connected, vContact
reports `Powering off: button` before the device enters System OFF. The app has
eight seconds to fetch the notice. With no app connected, power-off is immediate.
Button changes to GPS, buzzer mode and LEDs also appear in vContact after the
companion applies them.

## XIAO nRF52840 charging heartbeat

The repeater keeps the normal single heartbeat pulse while external power is
connected instead of showing the three-pulse low-battery warning during charge.

## Buttons

- **Station G2:** the user button now works like on other one-button boards: one press for the next
  page, two for the previous page, hold for a second to run the item.
- **Heltec Mesh Node T1, both LR2021 LoRa Plus EVK kits:** Power Off, DFU and Offgrid can be
  confirmed again.
- On all four boards the confirmation is a second **hold** within 3 seconds.

## Display: WiFi page, and the right connection shown

- **New WiFi page** on WiFi companions with a one-button display (Heltec V3, V4, V4.3, Wireless
  Tracker, T3S3, Station G2 and others), next to the Bluetooth page. It shows whether WiFi is off,
  joining or connected, and once connected the network, IP address and port to enter in the app,
  and whether the app is connected over WiFi. On the ThinkNode M9 the same details are under
  **System > Info > WiFi**.
- **The home page names the connection:** "WiFi OK", "USB OK" or "BLE OK" instead of always
  "BLE OK", and "BLE OFF" when Bluetooth is turned off and no app is connected.
- **The Bluetooth page says "Connected" only for a Bluetooth connection.** An app connected over
  WiFi or USB used to show there too.

Thanks to **bisbille** for the report.

## GPS no longer sets the clock to an old time

Just after waking, a GPS receiver can report its last position and its own, possibly stale, clock
before any satellite confirms them. That was taken as a fix and could set a repeater's clock hours
back. Now only satellite-solved positions count, five in a row. A fix takes about two seconds longer.
This applies to every board with a GPS. Thanks to **Innocent Killer 101** for the report and the
measurements.

## No GPS fitted, no GPS searching

Boards with an optional GPS connector (Heltec V3, V4 and V4.3, RAK4631, XIAO nRF54L15 and others)
searched for a GPS that was not there: for five minutes after boot on a repeater, and again after
every telemetry request. On an ESP32 board that kept the processor out of light sleep the whole
time.

- **The first time the GPS powers up after a reboot, the node listens for it for 10 seconds.**
  That is at boot if your GPS is on, or when you first turn it on. If nothing answers, the GPS is
  treated as not fitted until the next reboot, as upstream MeshCore does: `gps` answers
  `Can't find GPS`, and telemetry no longer wakes it.
- Your GPS setting is kept, so a GPS you fit later is found at the next boot.

Thanks to **svenlange2** for the report.

## New: KISS modem role

A ZephCore node can now act as a plain LoRa modem for a program on a computer, as upstream
MeshCore's `kiss_modem` does. The program sends and receives raw packets over USB, sets the
radio, and uses the node's key to sign, verify, encrypt and decrypt. There is no mesh, no
Bluetooth and no CLI on the node in this role.

- It speaks upstream's protocol ([kiss_modem_protocol.md](../docs/kiss_modem_protocol.md)), so
  host software written for upstream's modem works with it.
- When the channel is busy, a packet waits for it to clear instead of failing at once.
- Radio settings outside the ranges ZephCore supports are refused.
- The modem can also talk on a UART of your choice, for example header pins wired to a Raspberry
  Pi, named `zephcore,kiss-uart` in the devicetree; USB then stays free for updates.
- No prebuilt image yet: build it with `boards/common/kiss_modem.conf`
  ([BUILDING.md](../docs/BUILDING.md)). It keeps the identity of a repeater, room server or
  observer on the same node; flashed over a companion, it erases the companion's data.

Thanks to **recrof** for the port.

## From upstream MeshCore 1.18.0

- **Acknowledgements on direct routes are relayed after a short random delay**, like other direct
  packets, and with `multi.acks 1` the two copies go out 300 ms apart. This means fewer lost
  acknowledgements.
- **Floods with transport code 0 are un-scoped**, as in upstream 1.18.0. Unlike upstream, ZephCore
  also applies your `flood.max.unscoped` and `region denyf *` settings to them. (because I don't really get it what is this transport code for, yet)
- **Room server:** `set repeat on` now answers `ERR - repeat now deprecated`. A room server never
  forwarded packets by default anyway.
