# Heltec Mesh Node T096

Custom ZephCore board target for the Heltec Mesh Node T096.

Build:

```bash
west build -b heltec_t096 zephcore --pristine
```

## Hardware

- MCU: nRF52840
- Radio: SX1262
- RF front-end: KCT8103L PA/FEM, official maximum LoRa output power up to 28+/-1 dBm
- GNSS: UC6580

## Confirmed Pinout

Sources: MeshCore `variants/heltec_t096` for comparison, verified against the official Heltec `Mesh_Node_T096_V0.2` schematic.

| Function | nRF52840 pin | Schematic net | ZephCore mapping |
| --- | --- | --- | --- |
| LoRa SCK | P1.08 | LoRa_SCK | `spi2` SCK |
| LoRa MISO | P0.14 | LoRa_MISO | `spi2` MISO |
| LoRa MOSI | P0.11 | LoRa_MOSI | `spi2` MOSI |
| LoRa NSS / CS | P0.05 | LoRa_NSS | `cs-gpios` |
| LoRa reset | P0.16 | LoRa_RST | `reset-gpios` |
| LoRa busy | P0.19 | LoRa_BUSY | `busy-gpios` |
| LoRa DIO1 | P0.21 | DIO1 | `dio1-gpios` |
| SX1262 DIO2 / FEM CPS | SX1262 DIO2 | PA_CPS | `dio2-tx-enable` |
| FEM enable | P0.12 | PA_CSD | `antenna-enable-gpios` |
| FEM TX select | P1.09 | PA_CTX | `tx-enable-gpios` |
| FEM rail enable | P0.30 | VFEM_Ctrl | fixed regulator `vfem_enable` |
| GNSS UART RX into MCU | P0.23 | GNSS_TX | `uart0` RX |
| GNSS UART TX from MCU | P0.25 | GNSS_RX | `uart0` TX |
| GNSS reset | P1.14 | GNSS_RST | `gps-reset` alias |
| GNSS power | P0.06 | VGNSS_CTRL | fixed regulator `gps_power` |
| GNSS PPS | P1.11 | PPS | documented, not consumed |
| LED | P0.28 | LED | `led0`, `lora-tx-led` |
| Button | P1.10 | Button | `sw0` |
| Battery ADC | P0.03 / AIN1 | ADC_IN | ADC channel 1 |
| Battery ADC enable | P1.15 | ADC_Ctrl | fixed regulator `vbat_enable` |

## Not Enabled

- Display: the schematic confirms FFC nets (`LEDA`, `RESET`, `RS`, `SDIN`, `SCLK`, `CS`) but not a controller node in this board port.
- External flash: the schematic shows `F_SPI_*` nets and a flash footprint, but the flash device and parameters are not confirmed.

## T096 vs T114 Differences

- LoRa SPI/control pins are different across SCK, MISO, MOSI, CS, reset, busy, and DIO1.
- T096 adds a KCT8103L PA/FEM path with `PA_CSD`, `PA_CTX`, `PA_CPS`, and `VFEM_Ctrl`; T114 uses SX1262 DIO2 RF switch without those MCU PA/FEM GPIOs.
- T096 GNSS is UC6580 at 115200 baud with `VGNSS_CTRL`, `GNSS_RST`, and `PPS`; T114 uses ATGM336H-style wiring at 9600 baud.
- T096 battery ADC is P0.03/AIN1 with enable P1.15; T114 battery ADC is P0.04/AIN2 with enable P0.06.
- T096 LED is P0.28 active-high; T114 LED is P1.03 active-low.
- T096 button is P1.10, matching T114's user button pin.

## Hardware Smoke Test Plan

1. Flash UF2 by double-tap reset and drag-drop.
2. Confirm USB CDC/logging appears.
3. Confirm BLE advertises as ZephCore/MeshCore companion.
4. Confirm MeshCore app pairing works.
5. Confirm SX1262 init succeeds with no reset, busy, or DIO1 errors.
6. Confirm RX starts.
7. Test low-power TX first.
8. Confirm PA/FEM control before testing higher TX power.
9. Test PA progressively.
10. Confirm GNSS only after radio and BLE are stable.
