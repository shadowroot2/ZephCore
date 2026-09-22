# Shadow ZephCore v20260715.1

Проверочный релиз форка `shadowroot2/ZephCore` с собранными и протестированными образами для текущего набора плат.

## Прошивки

| Файл | Плата | Роль | Примечание |
|------|-------|------|------------|
| `shadow-20260715-thinknode-m1-client.uf2` | ThinkNode M1 | client/companion | Клиентская сборка с актуальными общими правками |
| `shadow-20260715-thinknode-m5-client-merged.bin` | ThinkNode M5 | client/companion | ESP32-S3 merged-bin, flash offset `0x0`; BLE, GPS, battery, temperature |
| `shadow-20260715-thinknode-m6-repeater.uf2` | ThinkNode M6 | repeater | GPS fix и телеметрия solar `6W`; BLE выключен |
| `shadow-20260715-heltec-v3-client-merged.bin` | Heltec V3 | client/companion | ESP32-S3 merged-bin, flash offset `0x0`; battery/sleep правки |
| `shadow-20260715-t1000-e-client.uf2` | Seeed T1000-E | client/companion | Light telemetry и актуальные battery/client правки |

## Основные изменения

- ThinkNode M5: исправлен старт Air530Z GPS по схеме M1/M6, UART GPS, BLE advertising и добавлена телеметрия температуры MCU.
- ThinkNode M6: сохранен рабочий GPS fix и вывод признака зарядки солнечной панели как `Current 6W`.
- Heltec V3: учтены правки отображения батареи и ухода в sleep при низком питании.
- T1000-E: сохранена поддержка датчика освещенности в телеметрии.
- Companion/client сборки собраны с актуальными правками батареи; repeater-сборка M6 собрана без BLE.
