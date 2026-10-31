# ShadoW ZephCore 1.18.1 — 2026-10-31

На базе [ZephCore автора liquidraver](https://github.com/liquidraver/ZephCore/releases/tag/1.18.1-zephcore), с совместимостью протокола [MeshCore](https://github.com/meshcore-dev/MeshCore).

## Из основной прошивки

- Часовые пояса с шагом 15 минут: `5.5`, `5.75`, `-3.5`. Экранные репиттеры учитывают выбранный пояс; RTC и сетевые метки остаются UTC.
- Исправлено зависание Wi-Fi companion, когда приложение перестаёт принимать данные: TCP send timeout работает через Kconfig.
- Режимы heartbeat `hb` и `off` не вызывают вспышку при входящем сообщении.
- Обновлён Zephyr: исправлена повторная инициализация GPS и внешней flash после неудачного запуска.

## Доработки ShadoW

- Заставка: **v1.18.1** отдельной строкой перед датой сборки.
- **Enter DFU Mode** доступен на nRF companion с экраном и поддержкой загрузчика. Запуск через экран и `start dfu` показывает **DFU Mode**, затем отключает подсветку; кадр E-Paper сохраняется.
- Нерабочий экран обновления ESP32 удалён из обеих UI.
- M6 repeater: готовое количество спутников снова передаётся в поле **«Освещение»**. Подсчёт спутников, настройки и работа GPS не менялись.
- M6: восстановлено поле **«Мощность»** — **6 W при активной зарядке панели**, **0 W без неё**. Это номинал панели, не измеренная текущая мощность. Active-low charge-detect учитывается GPIO API без ручной инверсии.
- Сохранены настройки часового пояса прежних ShadoW-сборок; половины и четверти часа не обрезаются при загрузке.

## Прошивки в этом релизе

| Устройство | Роль | Файлы |
|---|---|---|
| ThinkNode M1 | companion | UF2 |
| ThinkNode M5 | companion | merged BIN, signed update BIN |
| Heltec V3 | companion | merged BIN, signed update BIN |
| Heltec T114 | companion | UF2 |
| T-ECHO 433 | companion | UF2 |
| ThinkNode M6 | repeater | UF2 |

Это набор из пяти экранных companion и одного repeater; bridge и room server в него не входят.

**ESP32:** для первичной прошивки использовать **`-merged.bin` с offset `0x0`**. Файл `-update.bin` содержит только подписанное приложение и предназначен для штатного механизма обновления, не для записи с `0x0`.

## Проверки и ограничения

- **138 PASS**: 128 native cases и 10 Python harness tests.
- **6/6 pristine builds PASS**; ESP32-S3 собраны с sysbuild.
- Проверены 4 UF2 и 4 BIN: роль, версия, профиль T-ECHO 433, UF2 blocks/family/адреса, совпадение с build output, MCUboot integrity и размещение загрузчика/приложения.
- Препроцессором с фактическими build flags подтверждено отсутствие update page на ESP32 и наличие на экранных nRF.
- В логах остаются предупреждения Kconfig/DTS, unused и format-truncation для ограниченного remote reply; они не подавлялись.

### Память приложения

Байты; для ESP32 показаны DRAM и IRAM.

| Устройство | Flash | RAM / DRAM | IRAM | RAM / DRAM, % |
|---|---:|---:|---:|---:|
| thinknode_m1-companion | 437320 | 222032 | — | 84.70 |
| thinknode_m5-companion | 659764 | 267584 | 84708 | 70.29 |
| heltec_v3-companion | 1030324 | 379784 | 93648 | 99.77 |
| heltec_t114-companion | 429732 | 224656 | — | 85.70 |
| lilygo_techo-companion-433 | 431040 | 221008 | — | 84.31 |
| thinknode_m6-repeater | 249464 | 81564 | — | 31.11 |

**Heltec V3 companion: DRAM занята на 99,77%, свободно 888 байт.**


Работа новых сценариев на физических устройствах ещё не подтверждена. Успешная сборка и проверка файлов не заменяют аппаратную проверку.
