# Локальная матрица сборок ShadoW

Единая точка запуска — `scripts/shadow_build.sh`. Без `--run` скрипт только
показывает точную команду и ожидаемый артефакт. После подтверждения пользователя
повторить ту же команду с `--run`.

## Профили

| Профиль | West target | Проверявшиеся роли | Артефакт |
|---|---|---|---|
| `thinknode_m1` | `thinknode_m1` | companion | UF2 |
| `thinknode_m3` | `thinknode_m3` | companion | UF2 |
| `thinknode_m6` | `thinknode_m6` | repeater, bridge | UF2 |
| `t1000_e` | `t1000_e` | companion | UF2 |
| `lilygo_techo` | `lilygo_techo` | companion | UF2 |
| `heltec_t114` | `heltec_t114` | companion | UF2 |
| `rak4631` | `rak4631` | repeater | UF2 |
| `xiao_nrf52840` | `xiao_nrf52840` | repeater | UF2 |
| `heltec_v3` | `heltec_wifi_lora32_v3/esp32s3/procpu` | companion, repeater, bridge | merged/update BIN |

## Варианты

- `lilygo_techo companion 433` добавляет
  `boards/nrf52840/lilygo_techo/lilygo_techo_433.conf`.
- `thinknode_m6 bridge 868` использует repeater + nRF BLE bridge config.
- `heltec_v3 bridge 433|868` использует repeater + ESP-NOW/BLE bridge config и
  соответствующий частотный профиль.
- `companion` без варианта использует board defaults.

## Примеры

```sh
scripts/shadow_build.sh thinknode_m1 companion
scripts/shadow_build.sh lilygo_techo companion 433
scripts/shadow_build.sh thinknode_m6 bridge 868
scripts/shadow_build.sh heltec_v3 companion
scripts/shadow_build.sh heltec_v3 bridge 433
```

После подтверждения:

```sh
scripts/shadow_build.sh thinknode_m1 companion --run
```

Build trees всегда находятся в `builds/<profile>-<role>[-variant]`. Упаковку
после успешной сборки выполняет `scripts/package_firmware.sh`.
