# Shadow ZephCore v20260722.1

Дата: 2026-07-22  
Commit: `d359f5c` (`Document Pico W room server build`)

Проверочный release ветки Shadow после интеграции upstream `v1.16.5` и платовых исправлений. Все шесть профилей собраны с чистых build-каталогов.

## Что изменилось

- **ThinkNode M5**: исправлен USB CLI без BLE. Companion использует polling UART0, поэтому `help`, `gps` и остальные команды отвечают через USB-C.
- **ThinkNode M6**: GPS работает без принудительного RESET; в CLI, на экране и в телеметрии передаётся число видимых спутников. Телеметрия солнечной зарядки всегда показывает `0 W` или `6 W` по аппаратному `EXT_CHRG_DETECT`.
- **M1 / M5 / M6**: GPS RESET не трогается. M1 использует LiPo-кривую M5; при заряде ниже 25% отключаются buzzer и стартовая мелодия, heartbeat переключается на тройной импульс.
- **Heltec V3**: восстановлена температура MCU в телеметрии и emergency-сообщении; USB CLI работает без BLE. GPS-страница скрыта, так как у платы нет GPS.
- **T-1000E**: исправлены локальный USB CLI, световая индикация сообщений и отправок; luminosity остаётся показанием реального датчика освещённости.
- **Автоотключение Companion**: перед отключением по низкому заряду можно отправить сообщение в `#zephcore`; настройка `get/set autoshutdown.emergency on|off`. В сообщении есть напряжение, температура, uptime и, на GPS-платах, состояние GPS/ссылка на Google Maps при известных координатах.
- **CLI / V-Contact**: локальный `help` показывает только доступные командам платы и роли; через V-Contact он возвращается локальными частями и не ретранслируется по LoRa.
- **Pico W Room Server**: образ собирается для `rpi_pico/rp2040/w`, использует LoRa SX1262 и USB CDC CLI. Wi-Fi/CYW43 и встроенный зелёный LED отключены; создаётся только `picow-room-server.uf2`.

## Образы прошивки

| Файл | Плата | Роль | Как прошивать |
|------|-------|------|---------------|
| `thinknode_m1-companion-d359f5c.uf2` | ThinkNode M1 | Companion | UF2 через загрузчик платы |
| `thinknode_m5-esp32s3-procpu-companion-d359f5c-merged.bin` | ThinkNode M5 | Companion | полный образ, записывать с `0x0` |
| `thinknode_m6-repeater-d359f5c.uf2` | ThinkNode M6 | Repeater | UF2 через загрузчик платы |
| `heltec_v3-companion-d359f5c-merged.bin` | Heltec V3 | Serial Companion | полный образ, записывать с `0x0` |
| `t1000_e-companion-d359f5c.uf2` | Seeed T-1000E | Companion | UF2 через загрузчик платы |
| `picow-room-server.uf2` | Raspberry Pi Pico W + SX1262 | Room Server | UF2 через BOOTSEL |

`update.bin` в release не включены: для первичной прошивки ESP32-S3 нужен только полный `-merged.bin`.

## Проверка сборок

- ThinkNode M1 Companion: Flash 54.71%, RAM 66.52%.
- ThinkNode M6 Repeater: Flash 30.76%, RAM 31.85%.
- Seeed T-1000E Companion: Flash 49.40%, RAM 64.76%.
- ThinkNode M5 Companion: Flash 13.89%, DRAM 67.72%, IRAM 15.81%.
- Heltec V3 Serial Companion: Flash 6.92%, DRAM 66.71%, IRAM 15.72%.
- Pico W Room Server: Flash 12.39%, RAM 31.82%.

## SHA-256

```text
eaeb7a7ce5a6a78d166997f5bd3bd3235a51cb2db99d3d3f0754f5972151681d  thinknode_m1-companion-d359f5c.uf2
0e19ca0d5df14178dcea9384142a1420bb73cd1183c56a33400a3770d5b02fec  thinknode_m5-esp32s3-procpu-companion-d359f5c-merged.bin
e0977b3a4941b92396a1af21469816dfe60633a1fe2c13a4de4e54db7a933c04  thinknode_m6-repeater-d359f5c.uf2
4cc637fb7dda1b22a44319682d07c172c3f904bcffe74b89c7044fe88d4c7de9  heltec_v3-companion-d359f5c-merged.bin
3c6ffd84e1cd46e81b251ce4fba61b2394d6a4158855fca2238007da67b0fd9d  t1000_e-companion-d359f5c.uf2
848c7121bf4f3cbb5e48fd9569f19cf83b2dc0c8b71b7ec0ff11f72f38572d35  picow-room-server.uf2
```
