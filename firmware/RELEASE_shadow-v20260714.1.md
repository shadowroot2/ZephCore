# Shadow ZephCore tested firmware release

Tag: `shadow-v20260714.1`
Commit: `19e4d46`
Date: 2026-07-14

This release contains the locally built and tested Shadow firmware images.

## Firmware images

- `thinknode_m6-repeater-gps-solar-6w.uf2`
  - ThinkNode M6 repeater
  - GPS startup fix
  - M6 solar telemetry shown as Current 6W

- `thinknode_m5-esp32s3-procpu-companion-blefix-d9f7af2-merged.bin`
  - ThinkNode M5 companion
  - BLE advertising fix
  - display backlight, buttons, buzzer, LEDs and battery tuning

- `heltec_v3-companion-fixed-merged.bin`
  - Heltec V3 companion merged image
  - battery display tuning
  - low-voltage sleep path fixes

- `t1000_e-comp-light.uf2`
  - T1000-E companion
  - light sensor telemetry support

- `thinknode_m1-companion-autoshutdown.uf2`
  - ThinkNode M1 companion
  - low-voltage shutdown notification support

- `waveshare_rp2040_lora-repeater.uf2`
  - Raspberry Pi Pico W + Waveshare SX1262 repeater
  - tested LoRa advert visibility

## SHA256

```text
3878dc4c49b9411aebe4a6b89512359b310213dbfeba5a9c074172658b4f3eb6  thinknode_m6-repeater-gps-solar-6w.uf2
9573fdd0acf3e2c104df8c2ae0aef7e353ec0ef969225254d28e99648f6f3321  thinknode_m5-esp32s3-procpu-companion-blefix-d9f7af2-merged.bin
63d4b95082768d91493ab988ff27be71ef6ee2e2d91e1526040cc5649b3cd372  heltec_v3-companion-fixed-merged.bin
ba42112f9a5acddeb577f74327cac569ba9dfd4e6fef67ec7cf77e0cfaee07b9  t1000_e-comp-light.uf2
68b3af067466c54c3a4a232704798d72976925cd873bc9ea86898db2e2b80e4c  thinknode_m1-companion-autoshutdown.uf2
d2abd923acec7e7b26e5fcf6125f6ae7538ed126b634085006321dcb3b6ac57a  waveshare_rp2040_lora-repeater.uf2
```
