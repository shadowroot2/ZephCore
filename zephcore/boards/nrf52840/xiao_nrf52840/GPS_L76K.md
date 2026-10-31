# XIAO nRF52840 + Wio-SX1262 + Seeed L76K GNSS

Optional companion variant: `boards/nrf52840/xiao_nrf52840/gps_l76k.conf`.
Its same-named overlay is paired automatically. The plain XIAO firmware stays
GPS-free. The L76K uses the same CASIC/PCAS `luatos,air530z` GNSS driver as
ThinkNode M6, at 9600 baud. Satellites-in-view reporting is enabled.

Connections: 3V3, GND, XIAO D6/TX to GNSS RX, XIAO D7/RX to GNSS TX.
The UART occupies D6/D7, so I2C and any I2C sensors on those pins are disabled
in this variant.

The GNSS add-on's RESET pin is connected to XIAO D10. Wio-SX1262 uses D10 as
SPI MOSI. **Do not stack these two add-ons with RESET/D10 connected.** Use
four-wire GPS wiring, or isolate the GPS add-on's RESET/D10 contact before
stacking. The L76K reset input has an internal pull-up. GPS WAKEUP is on D0,
shared with the XIAO user button; firmware leaves it as a pulled-up input.
A button press can momentarily put GPS in standby.

The add-on has no independent power-enable line. The GNSS driver accepts that
configuration, but cannot shut down this L76K: budget about 41 mA while the
XIAO is powered, even with GPS polling off. `gps off` stops application GPS
work, not receiver supply current. Low-power operation needs an external
switchable 3V3 rail and a dedicated control GPIO.

Build target, after explicit build confirmation:

```sh
west build -b xiao_nrf52840 zephcore -d build_xiao_nrf52840_companion_gps_l76k --pristine -- -DEXTRA_CONF_FILE=boards/nrf52840/xiao_nrf52840/gps_l76k.conf
```

Hardware validation remains required: GNSS NMEA/PCAS identification, fix,
button behaviour, LoRa RX/TX, and current consumption.
