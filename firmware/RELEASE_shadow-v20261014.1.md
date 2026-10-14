# Shadow ZephCore 1.17.4 — 2026-10-14

Десять свежих образов из `dev`: репитеры M6 и XIAO nRF52840 + Wio-SX1262;
компаньоны M1, M3, M5, Heltec V3, Heltec T114 и T1000-E; BLE-мост
M6 868 ↔ Heltec V3 433.

## Основные изменения

- BLE-мост: повторная проверка и очистка асимметричных bond, восстановление
  после ошибок SMP, последовательный переход pairing → MTU → GATT и
  подтверждение handshake после фактической отправки ACK.
- Команда `bridge` показывает имя платы, состояние, RSSI активного соединения,
  `up`, фазу, последние ошибки, повторы, число auto-unpair и очередь. До
  установления моста RSSI не выводится. `bridge bonds` и `get bridge.mac`
  доступны также в удалённом help.
- BLE TX для repeater-bridge: +4 dBm на nRF и +6 dBm на ESP32.
- XIAO repeater: исправления выключения при USB/солнечной панели, телеметрии
  зарядки, восстановления радиодрайвера и длинного CLI help.
- Компаньоны: последние исправления кнопок, индикации и локального отображения
  исходящих SOS/Tracking в настроенных группах. Исправлена конфигурация
  devicetree M5 для сборки MCUboot.

## Прошивки и SHA-256

Все образы находятся в каталоге `firmware/1.17.4/shadow-v20261014.1/`.

| Плата и роль | Файл | SHA-256 |
| --- | --- | --- |
| M6 repeater 868 | `thinknode-m6-repeater-868.uf2` | `dc83f1b28e8890f1a69838db98ba42808482371753d168e669a050eecea0aefa` |
| XIAO nRF52840 + Wio-SX1262 repeater 868 | `xiao-nrf52840-wio-sx1262-repeater-868.uf2` | `025ed8a062d90d107f940800a03f878d44709c95dc64d8425f49338cd7c35682` |
| M1 companion 868 | `thinknode-m1-companion-868.uf2` | `78f65a9efb1e1f5d4d6fe6ec5d6eedca76fa776e27a1333f124642cc14c0954d` |
| M3 companion 868 | `thinknode-m3-companion-868.uf2` | `542995e6c7762608c3738505c77e54f3cd314105b232b21e30b0cfb000596f1e` |
| M5 companion 868 | `thinknode-m5-companion-868-merged.bin` | `90cb0b0045a5cf9e376b966eebae325bfb4d1249b9c48865c3ed600b87f328fd` |
| Heltec V3 companion 868 | `heltec-v3-companion-868-merged.bin` | `ac237b12b5f8b89f6b81d69dd40eeb4259792b07fb393b88a24aed88cc745191` |
| Heltec T114 companion 868, с дисплеем | `heltec-t114-companion-868.uf2` | `a936d1425ab1d6be829fda6f42918fcc90fb047ae75bd5641e21a710012ae7b9` |
| T1000-E companion 868 | `t1000e-companion-868.uf2` | `710ecff7d1a9781ecbbb9df0971329e9a34aef1427f0c8a4e123658366512c39` |
| M6 repeater-bridge 868 | `thinknode-m6-repeater-bridge-868.uf2` | `f34a220ca2bbc332f92d5ca9d470155da5d01c3587f1256dd99055a7902960ac` |
| Heltec V3 repeater-bridge 433 | `heltec-v3-repeater-bridge-433-merged.bin` | `090daadcaf760a457870916641b1057677ca36bac2d643f81fee8e7c49dba7ad` |

## Прошивка и ограничения

- UF2 записывается через загрузчик соответствующей платы. ESP32-файлы —
  полные merged BIN для записи с адреса `0x0`: MCUboot + подписанное
  приложение. M5 использует 4 MB flash и slot0 по `0x20000`; Heltec V3 —
  8 MB flash и slot0 по `0x10000`. Не стирайте весь flash, если нужно
  сохранить настройки и BLE bond.
- Частоты в именах — заводские профили для пустых настроек. Сохранённые
  радиопараметры имеют приоритет; после прошивки проверьте `get radio`.
- Чистые сборки всех десяти целей и повторная линковка из одного коммита
  прошли. BLE bridge host regression — PASS; UF2 family/адреса и ESP32
  image-info/checksum/hash проверены. Аппаратная проверка RSSI и длительного
  автовосстановления моста с этими образами ещё требуется.
