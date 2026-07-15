# Shadow ZephCore Context

Краткий контекст нашей работы над форком ZephCore.

## Общие правила

- `tap` не трогать.
- Логику отправки mesh-сообщений не менять без отдельной просьбы.
- Готовые прошивки складывать в `firmware/`.
- После анализа спрашивать перед сборкой, если пользователь прямо не сказал собирать.
- Для ESP32/S3 пока не создавать отдельный signed app image, если пользователь не попросит. Нужен merged-bin.

## Репозиторий

- Основной рабочий путь: `/Users/shadow/Work/CodeX/ZephCore`.
- Пользовательский GitHub: `https://github.com/shadowroot2/ZephCore`.
- Текущая рабочая ветка checkout: `dev`.
- `README.md` переведен на русский, добавлено упоминание автора оригинальной прошивки и нашей работы над ошибками/поддержкой новых плат.

## ThinkNode M6 Repeater

Что было найдено:

- GPS-модуль на M6 не получал корректный старт как на M1.
- Гипотеза про UART/парсер не подтвердилась как первичная: питание/старт GPS и порядок включения были ключевыми.
- Рабочая логика была сопоставлена с ThinkNode M1 и Meshtastic.

Что сделано:

- Логика старта GPS для M6 приведена к рабочей модели M1, но без физического тумблера: управление только программно из CLI.
- Убран временный `gps info` и лишняя GPS-отладка после подтверждения работы.
- Добавлен/учтен патч, который реально заставил GPS принимать данные.
- Подтверждено пользователем: M6 начал видеть спутники и получил fix.

Дополнительно:

- Для M6 оставлена телеметрия солнечной панели как `Current 6W`.
- Параметр мощности 6W должен быть только у M6.

## ThinkNode M1

- Использовался как эталон для GPS-старта.
- GPS на M1 работал сразу, потому что цепочка питания/старта уже была правильной.
- При сборках M1 не нужен repeater, если пользователь просит companion/client.

## T1000-E

- Проверялась поддержка датчика освещенности.
- Сравнивались ZephCore, MeshCore и Meshtastic.
- Добавлялась поддержка отображения освещенности в телеметрии.
- После правок нужно следить, чтобы BLE не был отключен в companion-сборке.

## RPi Pico / PicoW + Waveshare SX1262

Что было важно:

- Правильная плата: `rpi_picow`.
- Рабочая официальная прошивка для сравнения: `PicoW_repeater-v1.14.1-467959c`.
- MeshCore 1.14 был нерабочий для этого случая, сравнивать надо было с 1.14.1.

Что сделано:

- Сопоставлены пины и инициализация SX1262/Waveshare, без изменения логики advert/mesh.
- Исправлена конфигурация так, что advert начал отправляться.
- Убрана лишняя телеметрия `0W`, потому что у PicoW/Waveshare нет солнечной панели.
- Настройки начали сохраняться после правок persistence/storage.
- Для repeater и room сборок BLE должен быть выключен.

## Heltec v3 Companion

Что сделано:

- Собиралась fixed merged-прошивка с учетом пользовательского `build_heltec_v3_fixed.sh`.
- Добавлялись фиксы отображения аккумулятора и поведения при низком питании.
- Порог авто-выключения/сна снижен с `3.3V` до `3.25V`.
- Было выяснено, что на Heltec v3 нужен уход в sleep, а не настоящее выключение.
- Проверялись баги UI: broadcast advert не должен возвращать меню назад на один пункт; страницы Buzzer/GPS должны обновлять состояние.
- Пользователь сообщил, что при полном заряде Heltec V3 показывал `4451 mV`.
  Причина: в ZephCore overlay стоял завышенный `vbat-mv-multiplier=<5720>`.
  Официальный MeshCore для Heltec V3 использует `ADC_MULTIPLIER=5.42`, поэтому ZephCore multiplier исправлен на `5420`.
  Ожидаемый пересчет: `4451 * 5420 / 5720 ~= 4218 mV`, что похоже на полный 1S LiPo при зарядке.

Важное:

- Дубль прошивки больше не собирать.

## ThinkNode M5 Companion

Плата:

- ESP32-S3, E-Ink, PCA9557 GPIO expander.
- По железу похож на ThinkNode M1, но MCU ESP32-S3.
- Для сравнения использовались MeshCore 1.16 и Meshtastic M5.

Что сделано:

- Добавлена новая board-директория `zephcore/boards/esp32/thinknode_m5/`.
- Добавлены E-Ink/PCA9557, кнопки, buzzer, backlight, battery ADC, LoRa SX1262.
- CPU выставлен на 240 MHz.
- Flash режим оставлен DIO, частота 80 MHz: QIO на этой плате приводил к проблемам загрузки.
- Backlight настроен как на M1: включается по кнопке и гаснет по таймауту.
- Общий дефолт подтверждения действий выставлен на `3000 ms`.
- Батарейная кривая для M5 добавлена отдельно, потому что при полном заряде ZephCore показывал примерно `95% / 4150mV`.
- Новое наблюдение пользователя: полный заряд M5 сейчас показывает около `4127 mV`.
  Это нормально для этой платы/зарядного тракта. Текущая M5 OCV-кривая имеет верхнюю точку `4100 mV`, поэтому `4127 mV` должен отображаться как `100%`.
  Если при `4127 mV` процент не `100%`, проверять надо не делитель, а прошивку/линковку board-specific `battery_curve.c` или сохраненный runtime `adc_multiplier` в prefs.
- Power Off через меню теперь подтверждается нормально.
- GPS на M5 доведен до рабочего состояния по модели M1/M6:
  - `luatos,air530z` + easy init;
  - GPS power через `GPIO11`;
  - GPS reset через `GPIO13`;
  - физический GPS switch через `GPIO10`;
  - UART GPS: MCU TX `GPIO20`, MCU RX `GPIO19`;
  - временный `gps duty=0` и экранная отладка `Use/View/CB` убраны после подтверждения фикса.
- В GPS-экране оставлено полезное отображение количества спутников во время поиска.
- Добавлена телеметрия температуры для M5 через ESP32-S3 `coretemp`; fallback ограничен `CONFIG_ESP32_TEMP`, чтобы не ломать другие ESP32-S3 сборки без драйвера.

BLE root cause:

- BLE стек стартовал, но advertising падал с `err=-22`.
- Причина была не в имени, не в USB serial и не в scan response.
- В `board.conf` было ошибочно выключено:
  - `CONFIG_ESP32_BT_CTLR_LE_MASTER=n`
- В Zephyr ESP32 этот флаг нужен для connectable advertising, хотя приложение не работает как BLE central.
- Исправлено:
  - `CONFIG_ESP32_BT_CTLR_LE_MASTER=y`
  - `CONFIG_ESP32_BT_CTLR_LE_SCAN=n` оставлено выключенным.
- Пользователь подтвердил: Bluetooth заработал.

Последняя M5 прошивка:

- `firmware/shadow-20260715-thinknode-m5-client-merged.bin`

Проверенный итоговый BLE config:

```text
CONFIG_ESP32_BT_CTLR_LE_MASTER=y
# CONFIG_ESP32_BT_CTLR_LE_SCAN is not set
CONFIG_ESP32_BT_CTLR_LE_MAX_ACT=2
# CONFIG_BT_EXT_ADV is not set
CONFIG_BT_PRIVACY=y
# CONFIG_BT_DIS is not set
# CONFIG_BT_GATT_SERVICE_CHANGED is not set
```

LED note:

- После BLE-фикса красный/синий диоды оказались рабочими; они просто были выключены через настройку `LEDs off`.
- Не считать это регрессией BLE.

## Пресеты и BLE

- Базовый пресет менялся на Custom `867.935 MHz`, Duty Cycle `50%`.
- В пресете должен быть включен `Multy AKS` по умолчанию.
- В repeater и room сборках BLE должен быть выключен.
- В companion/client сборках BLE нужен.

## Полезные команды

M5 build:

```sh
CMAKE_PREFIX_PATH=/Users/shadow/Work/CodeX/ZephCore/zephyr-sdk-1.0.1 \
CCACHE_DIR=/Users/shadow/Work/CodeX/ZephCore/.ccache \
./.venv/bin/west build -b thinknode_m5/esp32s3/procpu zephcore --pristine --sysbuild
```

M5 merged-bin:

```sh
python3 -m esptool --chip esp32s3 merge-bin \
  --output firmware/shadow-20260715-thinknode-m5-client-merged.bin \
  --flash-mode dio --flash-freq 80m --flash-size 4MB \
  0x00000 build/mcuboot/zephyr/zephyr.bin \
  0x20000 build/zephcore/zephyr/zephyr.signed.bin
```

## Проверочные сборки 2026-07-15

Собранные и сложенные в `firmware/` образы:

- `shadow-20260715-thinknode-m1-client.uf2` — ThinkNode M1 client/companion.
- `shadow-20260715-thinknode-m5-client-merged.bin` — ThinkNode M5 client/companion, ESP32-S3 merged-bin, offset `0x0`.
- `shadow-20260715-thinknode-m6-repeater.uf2` — ThinkNode M6 repeater с GPS fix и solar `6W`.
- `shadow-20260715-heltec-v3-client-merged.bin` — Heltec V3 client/companion, ESP32-S3 merged-bin, offset `0x0`, с battery/sleep правками.
- `shadow-20260715-t1000-e-client.uf2` — Seeed T1000-E client/companion с light telemetry.

SHA в ответы не выводить, пользователь попросил не показывать.
