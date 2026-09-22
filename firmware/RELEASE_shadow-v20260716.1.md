# Shadow ZephCore v20260716.1

Дата: 2026-07-16
Commit: `3ed0b86` (`Merge upstream master v1.16.5`)

Проверочный релиз ветки `shadowroot2/ZephCore` после аккуратной интеграции `master` автора. Все перечисленные ниже профили собраны с чистого build-каталога и успешно слинкованы.

## Что изменилось

### Новое из upstream v1.16.5

- **Adaptive CAD / SmartCAD** автоматически подстраивает порог занятости LoRa-канала под условия конкретного узла. Управление: `get cad`, `set cad.auto off`, `set cad.offset`.
- **V-Contact** добавляет локальный контакт `v<имя-узла>`: через чат с ним доступны CLI-команды companion, а также уведомления о причине перезагрузки и низком заряде.
- Исправлены надёжность приёма LR1110/LR2021 (в том числе обработка CRC) и восстановление после ложных preamble/RX-busy состояний.
- Исправлены и унифицированы ESP32-разделы и Wi‑Fi OTA; приложение ESP32-S3/ESP32-C теперь располагается по авторской схеме с `0x10000`.
- Обновлены экранные UI, CAD-документация, драйверы радио и ряд платовых исправлений.

### Сохранённые и добавленные изменения ветки Shadow

- ThinkNode M5: поддержка платы, дисплей E-Ink, SX1262, GPS, батарея, buzzer, кнопки и BLE companion.
- ThinkNode M6: исправленный запуск GPS и индикация солнечной зарядки.
- T1000-E: телеметрия датчика освещённости.
- Heltec V3: serial companion, индикация состояния BLE и платовые настройки батареи/питания.
- Pico W + Waveshare SX1262: готовые сборки repeater и room server.
- Часовой пояс UI перенесён в конец сохранённого формата настроек. Старые файлы мигрируются без сдвига остальных пользовательских предпочтений; для редкого неоднозначного старого формата repeater часовой пояс нужно задать повторно: `set tz <минуты>`.

## Образы прошивки

| Файл | Плата | Роль | Как прошивать |
|------|-------|------|---------------|
| `thinknode_m6-repeater-3ed0b86.uf2` | ThinkNode M6 | repeater | UF2 через загрузчик платы |
| `thinknode_m5-esp32s3-procpu-companion-3ed0b86-merged.bin` | ThinkNode M5 | companion | полный ESP32-S3 образ, записывать с `0x0` |
| `heltec_v3-companion-serial-3ed0b86-merged.bin` | Heltec V3 | serial companion | полный ESP32-S3 образ, записывать с `0x0` |
| `t1000_e-companion-3ed0b86.uf2` | Seeed T1000-E | companion | UF2 через загрузчик платы |
| `thinknode_m1-companion-3ed0b86.uf2` | ThinkNode M1 | companion | UF2 через загрузчик платы |
| `picow-repeater.uf2` | Raspberry Pi Pico W | repeater | UF2 через BOOTSEL |
| `waveshare_rp2040_lora-repeater.uf2` | Waveshare RP2040 LoRa | repeater | UF2 через BOOTSEL |
| `picow-room-server.uf2` | Raspberry Pi Pico W | room server | UF2 через BOOTSEL |
| `waveshare_rp2040_lora-room-server.uf2` | Waveshare RP2040 LoRa | room server | UF2 через BOOTSEL |

Для ESP32-S3 при переходе на этот релиз используйте именно полный `-merged.bin` по USB/serial. В релиз намеренно не включены app-only `update.bin`.

## Проверка сборок

- ThinkNode M6 repeater: flash 30.35%, RAM 31.71%.
- T1000-E companion: flash 48.86%, RAM 85.49%.
- ThinkNode M1 companion: flash 54.15%, RAM 87.44%.
- ThinkNode M5 companion, Heltec V3 serial companion, Pico W/Waveshare repeater и Pico W/Waveshare room server: сборка и линковка успешно завершены.

Высокое использование RAM на T1000-E и ThinkNode M1 соответствует текущей конфигурации nRF52840, но остаётся в пределах доступной памяти.

## SHA-256

```text
7ff949ff4d41e854a36493daf4edf9ac57cc00d5313c25aeca05963e88194a61  thinknode_m6-repeater-3ed0b86.uf2
e5dbae7cddb302b5f77cf8c16778c66462036f9b39103bd4cad18b02c2d50cee  thinknode_m5-esp32s3-procpu-companion-3ed0b86-merged.bin
2c1bcded3cfd391a7447a2b63782363d470ba0f4c6082bd40f1cf55b30a1ebba  heltec_v3-companion-serial-3ed0b86-merged.bin
cdc3ad7194b8d4c54cf787d5c9fabfe6e5973c4ef813e0583208889f6010b2b1  t1000_e-companion-3ed0b86.uf2
9110e1c0eccf6ca7bb143aafd697c87029cc19632bca1b4a0f8fff24c597389d  thinknode_m1-companion-3ed0b86.uf2
c033c93da63bf009b9b76847fb743ec533ab8de206ed9e31532fa81597bf21ce  picow-repeater.uf2
c033c93da63bf009b9b76847fb743ec533ab8de206ed9e31532fa81597bf21ce  waveshare_rp2040_lora-repeater.uf2
17630bfa24317ec17578d9373e4522553b3923342788c56cb3add521c92c9222  picow-room-server.uf2
17630bfa24317ec17578d9373e4522553b3923342788c56cb3add521c92c9222  waveshare_rp2040_lora-room-server.uf2
```
