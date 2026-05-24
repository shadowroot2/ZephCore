# ZephCore Native Linux Port

Run ZephCore as a native Linux process on SBCs like the **Femtofox** (Luckfox Pico Mini + E22-900M30S) or **Raspberry Pi 4/5 + RAK6421 HAT**. The full mesh stack runs on top of Zephyr's `native_sim` board, talking to real SPI/GPIO via `/dev/spidev*` and `libgpiod v2`.

The companion app connects via a TCP socket on port **5000** (matching upstream MeshCore Linux convention), so the same mobile app TCP config works against both ZephCore Linux and MeshCore Linux.

---

## Prerequisites

On the **build host** (where you run `west build`):

```bash
# Zephyr SDK and west workspace as per the main CLAUDE.md.

# Cross-compile toolchains (for SBC targets):
sudo apt install gcc-arm-linux-gnueabihf      # Femtofox (RV1103 ARMv7-A)
sudo apt install gcc-aarch64-linux-gnu        # Raspberry Pi 4/5 (aarch64)
```

On the **target SBC** (where the binary runs):

```bash
# Grant your user access to /dev/spidev and /dev/gpiochip (or run as root).
sudo usermod -a -G spi,gpio $USER
```

No userspace library dependencies — the GPIO driver talks to `/dev/gpiochipN`
directly via the kernel's GPIO V2 chardev uAPI (ioctl). Requires Linux ≥ 5.10
(released Dec 2020 — every modern SBC distro has it). The SPI driver uses
spidev the same way (no library either).

### Enabling spidev on the SBC

**Femtofox / Luckfox Pico Mini:** edit `/etc/luckfox.conf` to enable SPI0, then reboot.

**Raspberry Pi:** add `dtparam=spi=on` to `/boot/firmware/config.txt` (RPi 5) or `/boot/config.txt` (RPi 4) and reboot. Verify `/dev/spidev0.0` exists.

---

## Quick Start

### 1. Host smoke build (x86-64 Linux, no real radio)

```bash
west build -b native_sim/native/64 zephcore --pristine
./build/zephyr/zephcore_native_linux.exe
```

The binary prints something like:

```
UART_0 connected to pseudotty: /dev/pts/3
TCP companion transport listening on :5000
```

`Ctrl-C` to stop.

### 2. Femtofox (Luckfox Pico Mini, ARMv7-A)

```bash
west build -b native_sim/native/64 zephcore --pristine -- \
  -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
  -DNATIVE_TARGET_HOST=arm \
  -DCROSS_COMPILE=/usr/bin/arm-linux-gnueabihf- \
  -DEXTRA_CONF_FILE="boards/common/femtofox.conf"

scp build/zephyr/zephcore_native_linux.exe root@femtofox.local:/opt/zephcore/
ssh root@femtofox.local /opt/zephcore/zephcore_native_linux.exe
```

### 3. Raspberry Pi 4 + RAK6421 (aarch64)

```bash
west build -b native_sim/native/64 zephcore --pristine -- \
  -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
  -DNATIVE_TARGET_HOST=aarch64 \
  -DCROSS_COMPILE=/usr/bin/aarch64-linux-gnu- \
  -DEXTRA_CONF_FILE="boards/common/rpi_rak6421.conf"
```

For RPi 5, use `boards/common/rpi5_rak6421.conf` (same wiring, `gpiochip4` instead of `gpiochip0`).

### 4. Repeater role (no companion)

Add `boards/common/repeater.conf` to the `EXTRA_CONF_FILE` list. The repeater CLI is accessible via the native PTY printed at boot (`/dev/pts/N`):

```bash
screen /dev/pts/3        # local
# or remotely:
socat /dev/pts/3 TCP-LISTEN:6000,reuseaddr,fork &
# then on a remote machine:
nc <sbc-ip> 6000
```

---

## Hardware Wiring

### Femtofox / Luckfox Pico Mini + E22-900M30S (TCXO)

Pin offsets are computed as `(Rockchip GPIO# − 32)` because the kernel splits banks into separate `gpiochip` devices.

| Signal | gpiochip | Offset | Rockchip GPIO |
|---|---|---|---|
| SPI bus | `/dev/spidev0.0` @ 2 MHz | — | — |
| CS | gpiochip1 | 16 | GPIO48 (1C0) |
| DIO1/IRQ | gpiochip1 | 23 | GPIO55 (1C7) |
| BUSY | gpiochip1 | 22 | GPIO54 (1C6) |
| RESET | gpiochip1 | 25 | GPIO57 (1D1) |
| RXEN | gpiochip1 | 24 | GPIO56 (1D0) |

SX1262 extras: DIO2 drives the RF switch (`dio2-tx-enable`), DIO3 powers the TCXO at 1.8V.

Source: `github.com/femtofox/femtofox` → `foxbuntu/.../femtofox_SX1262_TCXO.yaml`.

### Raspberry Pi 4/5 + RAK6421 HAT + RAK13300/RAK13302 (IO Slot 1)

BCM GPIO numbers (RPi 4: gpiochip0; RPi 5: gpiochip4):

| Signal | BCM | WisBlock slot 1 pin |
|---|---|---|
| SPI bus | `/dev/spidev0.0` (CE0 = GPIO 8) | 25–28 |
| DIO1/IRQ | 17 | 29 |
| BUSY | 12 | 30 |
| RESET | 13 | 31 |

No DIO2 RF switch (RAK13300 has discrete switch), no DIO3 TCXO.

Source: RAK6421 datasheet IO slot table + RAKWireless `meshtastic-rak6421-guide`.

---

## Running the Binary

The binary prints its PTY path and TCP listen port at startup. Logs go to stderr; pipe them where you want them.

### Companion app connection

In the ZephCore companion mobile app, choose TCP/Network mode and connect to:

- **Host:** the SBC's IP address
- **Port:** `5000` (default; configurable via `CONFIG_ZEPHCORE_LINUX_TCP_PORT`)

The wire framing is **2-byte big-endian length prefix + raw NUS payload**, matching upstream MeshCore Linux. Only one client connects at a time.

### Repeater CLI

The repeater role's USB CDC CLI maps onto Zephyr's `CONFIG_UART_NATIVE_PTY`. At boot:

```
UART_0 connected to pseudotty: /dev/pts/3
```

`screen /dev/pts/3` to attach locally. For remote access, bridge the PTY to TCP with `socat`:

```bash
socat /dev/pts/3 TCP-LISTEN:6000,reuseaddr,fork &
nc <sbc-ip> 6000
```

---

## Runtime Pin Override

For one-off hardware setups or custom wiring, override DT defaults at runtime:

```bash
./zephcore_native_linux.exe \
  --lora-spidev=/dev/spidev0.0 \
  --lora-gpio-chip=/dev/gpiochip1
```

Currently only the **paths** are runtime-overridable; pin offsets come from the DTS overlay you build with. To change pin numbers without rebuilding, create your own `boards/common/<custom>.conf` + `.overlay` and pass it via `-DEXTRA_CONF_FILE`.

Run `./zephcore_native_linux.exe --help` to see all available command-line arguments registered by the native_sim infrastructure (Zephyr drivers, our SPI/GPIO drivers, etc.).

---

## Troubleshooting

### Permission denied opening /dev/spidev0.0 or /dev/gpiochip*

```bash
sudo usermod -a -G spi,gpio $USER
# Log out and back in for group membership to take effect.
```

### `/dev/spidev0.0: No such file or directory`

SPI isn't enabled in the device tree. See "Enabling spidev on the SBC" above.

### `Linux kernel headers too old; GPIO V2 chardev uAPI required`

The host adapter requires the GPIO V2 uAPI (kernel ≥ 5.10, late 2020). All current SBC distros (Debian 12+, Ubuntu 22.04+, Raspberry Pi OS bookworm+) ship recent enough kernels. If you hit this, your distro is ancient — upgrade.

### TCP port 5000 already in use

Another service (Flask, Docker registry, AirPlay…) has port 5000. Override:

```bash
west build … -- -DCONFIG_ZEPHCORE_LINUX_TCP_PORT=15000
```

### "TCP companion client disconnected" loops

The wire framing is `2-byte BE length + payload`. If the companion app sends a different framing (raw NUS bytes, or 1-byte length, etc.), the transport will read a bogus length and bail. Verify the app is configured for MeshCore Linux TCP mode (port 5000 framing), not BLE-direct.

### LoRa packets fly out but nothing receives them

Check that DIO1 is wired and configured correctly. The interrupt path is the primary RX trigger. If DIO1 is misrouted, the driver will appear to TX fine but never reports RX.

Verify on the host with `gpioget`:

```bash
gpioget /dev/gpiochip1 23     # should toggle when a peer transmits
```

If the host can't see DIO1 transitioning, the radio isn't bringing the line up — check SX1262 RESET sequencing and DIO1 wiring.

### `bind(5000) failed: 98` (EADDRINUSE)

Another instance of the binary is already running, or you killed the last one with `kill -9` and the socket is still in TIME_WAIT. `SO_REUSEADDR` is set so this should clear in <1 minute; or `pkill zephcore_native_linux.exe`.

---

## Architecture

```
┌─────────────────────────────────────────────┐
│ ZephCore Mesh (C++)                         │
│   CompanionMesh / RepeaterMesh              │
└──────────────┬──────────────────────────────┘
               │ Zephyr APIs (k_event, k_msgq, k_thread)
               │
┌──────────────▼──────────────────────────────┐
│ Zephyr RTOS @ native_sim (running as Linux  │
│ process via pthreads)                       │
│                                             │
│   • SX1262 driver (unchanged)               │
│   • New spi_native_linux driver       ──┐   │
│   • New gpio_native_linux driver      ──┤   │
│   • LinuxTCPTransport (replaces BLE)  ──┤   │
└──────────────────────────────────────────┼──┘
                                           │
                              host syscalls + zsock_*
                                           │
┌──────────────────────────────────────────▼──┐
│ Linux kernel                                │
│   /dev/spidevX.Y    /dev/gpiochipN          │
│   AF_INET TCP socket    /dev/pts/N          │
└─────────────────────────────────────────────┘
```

The mesh C++ layer and the patched SX1262 Zephyr driver are bit-identical to the MCU builds. Only the SPI/GPIO host bridges and the companion transport differ.

---

## Limitations

- **Not BLE.** The companion app must support TCP/Network mode. No BlueZ integration.
- **Not for production use** per Zephyr's `native_sim` documentation. Works fine for hobby/lab deployments.
- **Single companion client.** One TCP connection at a time, mirroring `BT_MAX_CONN=1`.
- **No persistent flash.** Storage uses the host filesystem under `/lfs` via LittleFS over a flat backing file. Survives reboot if you persist the file; otherwise the node is regenerated each run.

---

## Files

- `boards/common/linux_common.conf` + `.overlay` — auto-applied when `BOARD=native_sim`
- `boards/common/femtofox.conf` / `.overlay` — Femtofox preset
- `boards/common/rpi_rak6421.conf` / `.overlay` — Raspberry Pi 4 preset
- `boards/common/rpi5_rak6421.conf` / `.overlay` — Raspberry Pi 5 preset
- `adapters/transport/LinuxTCPTransport.c` — TCP companion transport
- `patches/zephyr-new/drivers/spi/spi_native_linux*` — spidev SPI driver
- `patches/zephyr-new/drivers/gpio/gpio_native_linux*` — libgpiod v2 GPIO driver
- `patches/zephyr/0007-spi-gpio-native-linux.patch` — wires the new drivers into Zephyr's `drivers/spi/` and `drivers/gpio/` CMakeLists + Kconfig

---

## Debugging Log

(Updated as issues are found during bringup.)

### Verification status

End-to-end verified under WSL Ubuntu 24.04 (gcc 13.3):

- ✅ All 7 `patches/zephyr/*.patch` apply cleanly (including the new `0007-spi-gpio-native-linux.patch`).
- ✅ All files in `patches/zephyr-new/` copy correctly into the Zephyr tree.
- ✅ Platform detection routes `BOARD=native_sim` to `boards/common/linux_common.conf`.
- ✅ `native_sim/native/64` builds clean → `build/zephyr/zephcore_native_linux.exe` (~4.3 MB ELF).
- ✅ Binary runs. Zephyr OS boots, mesh event loop starts.
- ✅ `spi_native_linux` driver loads and attempts to open `/dev/spidev0.0` (fails in WSL: no SPI hardware).
- ✅ `LinuxTCPTransport` listens on port 5000.
- ✅ TCP client connect/disconnect works; 2-byte BE length framing parses correctly.

Build command verified (run from inside WSL with workspace at `/mnt/d/zephcore`):

```bash
export PATH="$HOME/.local/bin:$PATH"
export ZEPHYR_BASE=/mnt/d/zephcore/zephyr
cd /mnt/d/zephcore && west build -b native_sim/native/64 zephcore --pristine
```

The default 32-bit `native_sim` variant additionally requires `libc6-dev-i386`; the 64-bit variant works with stock `libc6-dev`.

### Known caveats found during implementation

- **Double-overlay listing** in `EXTRA_DTC_OVERLAY_FILE`: `linux_common.overlay` gets auto-paired twice — once when the platform conf is selected, once again when the EXTRA_CONF_FILE list is re-walked. Pre-existing CMakeLists.txt quirk affecting all platform confs; harmless (DTC merges idempotently) but cosmetic.

- **Pin paths are runtime-overridable, pin numbers are not.** Only `--lora-spidev=<path>` and `--lora-gpio-chip=<path>` are wired as command-line args. To change actual pin numbers without rebuilding, create a custom preset overlay and pass it via `-DEXTRA_CONF_FILE`. (A future iteration could expose pin offsets as cmdline args too.)
