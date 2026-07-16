# ZephCore — MeshCore для Zephyr RTOS

ZephCore — это порт прошивки [MeshCore](https://github.com/meshcore-dev/MeshCore/) для LoRa mesh-сетей с Arduino-стека на [Zephyr RTOS](https://zephyrproject.org/). Цель проекта — сохранить совместимость с оригинальным протоколом MeshCore, мобильными приложениями MeshCore и существующими узлами сети.

## Авторство и назначение этой ветки

Реальный автор ZephCore и основной работы над прошивкой — [liquidraver](https://github.com/liquidraver), репозиторий upstream: [liquidraver/ZephCore](https://github.com/liquidraver/ZephCore).

Эта ветка в репозитории [shadowroot2/ZephCore](https://github.com/shadowroot2/ZephCore) — моя рабочая ветка с разбором найденных проблем, исправлениями под конкретное железо и добавлением поддержки новой платы. Я проделал работу над ошибками, сравнил поведение с MeshCore/Meshtastic на реальных устройствах и довел до рабочего состояния несколько сборок repeater/client.

## Что добавлено в этой ветке

В этой ветке я добавил и исправил:

- **ThinkNode M6 repeater**: исправлен старт GPS. M6 теперь использует рабочую схему `luatos,air530z`, питание GPS через `GPS_EN`, а `GPS_STANDBY` удерживается в активном состоянии. GPS на M6 снова получает спутники и фикс.
- **ThinkNode M6 solar telemetry**: добавлен бинарный признак зарядки батареи и отображение мощности `6W` только для M6, когда реально активен сигнал зарядки.
- **Seeed T1000-E**: добавлена поддержка аналогового датчика освещенности и передача luminosity в телеметрию. Battery ADC сверено с MeshCore/Meshtastic: используется тот же 2:1 делитель, в ZephCore это `vbat-mv-multiplier=<7236>`.
- **ThinkNode M5 companion**: добавлена новая ESP32-S3 плата с E-Ink, PCA9557, buzzer, backlight, кнопками, SX1262, GPS и батарейной кривой. Исправлены BLE advertising, старт Air530Z GPS по схеме M1/M6, UART GPS и телеметрия температуры MCU.
- **ESP32 companion power tuning**: для ThinkNode M5 и Heltec V3 отключен Wi-Fi, BLE TX power снижен до более холодного режима, чтобы уменьшить нагрев и расход батареи.
- **Heltec V3 companion**: учтены фиксы отображения батареи и ухода в sleep при низком питании. Коэффициент батареи откалиброван по MeshCore/Meshtastic: `vbat-mv-multiplier=<5420>`, чтобы полный 1S LiPo не отображался как завышенные `4.45V`.
- **Low-battery shutdown/notify**: companion-сборки по умолчанию используют порог `3250 mV` на nRF52; перед уходом в сон/выключение может отправляться сообщение в Public с напряжением батареи и uptime.
- **Локальное время UI**: добавлен runtime offset часового пояса через CLI `get tz` / `set tz <minutes>`. Глобальный дефолт этой ветки — `300` минут (GMT+5); RTC, GPS и протокольные timestamps остаются UTC.
- **Raspberry Pi Pico W + Waveshare SX1262**: добавлена поддержка новой платы в режиме repeater. Сборка идет под `rpi_picow`, пины сверены с рабочей прошивкой MeshCore `PicoW_repeater-v1.14.1`.
- **BLE в repeater/room/observer**: Bluetooth полностью отключается для repeater, room-server и observer сборок. BLE-конфиги вынесены отдельно и подключаются только для client/companion.
- **Базовый LoRa-пресет**: частота `867.935 MHz`, SF8, BW 62.5 kHz, CR 4/8, duty cycle 50%.
- **Multi ACKs**: включены по умолчанию для новых настроек узла.
- **RP2040 RNG**: добавлен fallback RNG для платформ без аппаратного CSPRNG.

## Проверочные прошивки

Актуальные собранные образы этой ветки лежат в `firmware/`:

| Файл | Плата | Роль | Формат |
|------|-------|------|--------|
| `shadow-20260715-thinknode-m1-client-tz300.uf2` | ThinkNode M1 | client/companion | UF2, UI timezone default GMT+5 |
| `shadow-20260715-thinknode-m5-client-tz300-merged.bin` | ThinkNode M5 | client/companion | ESP32-S3 merged-bin, flash offset `0x0`, UI timezone default GMT+5 |
| `shadow-20260715-thinknode-m6-repeater.uf2` | ThinkNode M6 | repeater | UF2 |
| `shadow-20260715-heltec-v3-client-ble6-wifi-off-tzcli-merged.bin` | Heltec V3 | client/companion | ESP32-S3 merged-bin, flash offset `0x0`, BLE 6 dBm, Wi-Fi off |
| `shadow-20260715-t1000-e-client-tz300.uf2` | Seeed T1000-E | client/companion | UF2, light telemetry, battery/autoshutdown fixes |

## Зачем Zephyr?

Оригинальная Arduino-версия построена вокруг `loop()`. В ZephCore эта модель заменена на событийные примитивы Zephyr (`k_event_wait`, `k_poll`, `k_msgq`), поэтому процессор может спать в WFI между событиями и просыпаться только по делу.

Основные преимущества:

- **Нормальная модель драйверов**: LoRa, GNSS, дисплеи, датчики и BLE используют subsystem-драйверы Zephyr вместо Arduino-библиотек.
- **Иерархическая конфигурация сборок**: настройки плат собираются через Kconfig и devicetree overlays.
- **DFU/UF2**: можно получать Arduino-совместимые zip-пакеты для OTA и UF2-файлы для drag-and-drop прошивки.
- **Совместимость с bootloader/SoftDevice**: для многих nRF52-плат не требуется перепрошивать bootloader.

## Поддерживаемые платы

### nRF52840

| Плата | Радио | Особенности |
|-------|-------|-------------|
| **Wio Tracker L1** | SX1262 | GPS L76KB, OLED SH1106, joystick, buzzer, QSPI flash |
| **Seeed T1000-E** | LR1110 | GPS AG3335, LEDs, button, аналоговый датчик освещенности |
| **RAK4631 / RAK WisMesh Pocket** | SX1262 | GPS u-blox MAX-7Q, optional WisBlock OLED SSD1306, I2C sensors |
| **RAK3401 1W** | SX1262 + SKY66122 | 30 dBm PA, GPS optional, I2C sensors |
| **RAK WisMesh Tag** | SX1262 | GPS AT6558R, accelerometer, buzzer |
| **ThinkNode M1** | SX1262 | GPS, e-paper SSD1681, QSPI flash, buzzer, RGB LEDs |
| **ThinkNode M3** | LR1110 | GPS, buzzer, two buttons, RGB LEDs |
| **ThinkNode M6** | SX1262 | GPS L76K/Air530Z-compatible, QSPI flash, RGB LEDs, solar charge flag, 6W panel telemetry |
| **LilyGo T-Echo** | SX1262 TCXO 1.8V | GPS L76K, 1.54" e-paper SSD1681, BME280, QSPI flash |
| **Heltec T114** | SX1262 | 1.14" TFT ST7789V; screenless build через `no_display.conf` |
| **Heltec Mesh Node T096** | SX1262 + KCT8103L PA/FEM | UC6580 GNSS, ST7735S 160×80 TFT, button, LED, battery ADC |
| **Ikoka Nano 30dBm** | SX1262 E22-900M30S | 30 dBm PA, RGB LEDs |
| **GAT562 30S Mesh Kit** | SX1262 30 dBm / 1 W PA | RAK4631 core, OLED SSD1306, joystick, buzzer, GPS, BME280 pad, 2x18650 + solar |
| **SenseCAP Solar** | SX1262 | GPS L76K, QSPI flash, battery monitor |
| **XIAO nRF52840 + Wio-SX1262** | SX1262 | Bare XIAO + Wio-SX1262 expansion |
| **ProMicro SX1262** | SX1262 E22-900M30S | GPS, battery ADC, button, LED |

### ESP32

| Плата | MCU | Радио | Особенности |
|-------|-----|-------|-------------|
| **XIAO ESP32-C3** | ESP32-C3 | SX1262 | BLE 5.0 |
| **XIAO ESP32-C6** | ESP32-C6 | SX1262 | BLE 5.0, Wi-Fi 6 |
| **XIAO ESP32-S3** | ESP32-S3 | SX1262 | BLE 5.0, 8 MB flash, 8 MB PSRAM |
| **Station G2** | ESP32-S3 | SX1262 + PA | OLED SH1106, GPS, 16 MB flash, 8 MB PSRAM |
| **LilyGo TLoRa C6** | ESP32-C6 | SX1262 | BLE 5.0, Wi-Fi 6 |
| **Heltec V3** | ESP32-S3 | SX1262 | OLED SSD1306, 8 MB flash, откалиброванный battery ADC, sleep при низком питании |
| **Heltec V4.2** | ESP32-S3 | SX1262 + GC1109 PA | OLED SSD1306, 16 MB flash, 2 MB PSRAM |
| **Heltec V4.3** | ESP32-S3 | SX1262 + KCT8103L PA | OLED SSD1306, 16 MB flash, 2 MB PSRAM |
| **Heltec Wireless Tracker** | ESP32-S3 | SX1262 | ST7735R 160x80 TFT, UC6580 GPS |
| **Heltec Wireless Tracker V2** | ESP32-S3FN8 | SX1262 + KCT8103L PA/FEM | ST7735R 160x80 TFT, UC6580 GNSS, battery ADC |
| **ThinkNode M5** | ESP32-S3 | SX1262 | E-Ink SSD1681, PCA9557, GPS, buzzer, backlight, battery ADC, BLE companion |
| **LilyGo T-Beam v1.2** | ESP32 PICO-D4 | SX1262 | AXP2101 PMU, GNSS, USB-UART CLI |
| **TTGO LoRa32** | ESP32 PICO-D4 | SX1276 | Reference board, USB-UART CLI |

### RP2040

| Плата | MCU | Радио | Особенности |
|-------|-----|-------|-------------|
| **Raspberry Pi Pico W + Waveshare SX1262** | RP2040 | SX1262 | Repeater-only, USB CDC CLI, LittleFS settings, UF2 output |

### Другие платформы

| Плата | MCU | Радио | Особенности |
|-------|-----|-------|-------------|
| **XIAO nRF54L15 + Wio-SX1262** | nRF54L15 | SX1262 | FLPR multicore, RRAM storage |
| **XIAO MG24 + Wio-SX1262** | EFR32MG24 | SX1262 | BLE через Silicon Labs blob |
| **Seeed LoRa-E5 mini** | STM32WLE5JC | Встроенное sub-GHz | Нет BLE/USB; companion и CLI через USART1 |

ZephCore также работает как **native Linux process** на SBC (Femtofox / Luckfox Pico Mini, Raspberry Pi + RAK6421 HAT) с физическим SX1262 по SPI/GPIO и подключением companion-приложения по TCP; см. [LINUX_NATIVE.md](zephcore/LINUX_NATIVE.md).

Точные строки `west build -b`, способы прошивки и особенности плат описаны в [списке поддерживаемых плат](zephcore/boards/supported_boards.md) и [Board Porting Guide](zephcore/boards/example_board/README.md).

## Роли устройства

- **Companion** — режим по умолчанию. Подключается к мобильным приложениям MeshCore по BLE, хранит контакты, каналы и очередь offline-сообщений.
- **Repeater** — ретранслирует mesh-пакеты, настраивается через USB serial CLI. Команды описаны в [Repeater CLI Command Reference](zephcore/Repeater_CLI_commands.md).
- **Room Server** — store-and-forward shared room, аналог BBS. Клиенты входят с admin/guest password и публикуют сообщения, сервер доставляет новые посты остальным участникам.
- **Observer** — listen-only узел для ESP32, публикующий принятые LoRa-пакеты в MQTT через Wi-Fi.

В сборках **Repeater**, **Room Server** и **Observer** Bluetooth отключен полностью.

## Сборка

Потребуется [Zephyr SDK >= 1.0.1](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) и `west`. Версия Zephyr закреплена в `west.yml`.

Опционально: [adafruit-nrfutil](https://github.com/adafruit/Adafruit_nRF52_nrfutil) для генерации DFU zip на nRF52.

```bash
# Инициализация workspace, только первый раз
cd <папка репозитория>
west init -l zephcore
west update

# Companion, production
west build -b wio_tracker_l1 zephcore --pristine

# Companion с debug logging
west build -b wio_tracker_l1 zephcore --pristine -- \
  -DEXTRA_CONF_FILE="boards/common/debug.conf"

# Repeater
west build -b rak4631 zephcore --pristine -- \
  -DEXTRA_CONF_FILE="boards/common/repeater.conf"

# Repeater с debug logging
west build -b rak4631 zephcore --pristine -- \
  -DEXTRA_CONF_FILE="boards/common/repeater.conf;boards/common/debug.conf"

# Repeater с packet logging
west build -b rak4631 zephcore --pristine -- \
  -DEXTRA_CONF_FILE="boards/common/repeater.conf;boards/common/packet_logging.conf"

# ESP32 repeater + WiFi AP HTTP OTA
west build -b xiao_esp32s3/esp32s3/procpu zephcore --pristine --sysbuild -- \
  -DEXTRA_CONF_FILE="boards/common/repeater.conf;boards/common/wifi_ota.conf"

# Room Server
west build -b rak4631 zephcore --pristine -- \
  -DEXTRA_CONF_FILE="boards/common/room_server.conf"

# Observer
west build -b xiao_esp32c3 zephcore --pristine -- \
  -DEXTRA_CONF_FILE="boards/common/observer.conf"

# Factory reset / formatter
west build -b wio_tracker_l1 zephcore/tools/formatter --pristine

# BLE debug logging
west build -b rak4631 zephcore --pristine -- \
  -DEXTRA_CONF_FILE="boards/common/debug.conf" -DCONFIG_ZEPHCORE_BLE_LOG_LEVEL_DBG=y
```

### Быстрая сборка Pico W + Waveshare SX1262

Для Pico W repeater добавлен готовый скрипт:

```bash
./build_waveshare_rp2040_lora_repeater.sh
```

Результат будет записан в:

```text
firmware/picow-repeater.uf2
firmware/waveshare_rp2040_lora-repeater.uf2
```

## Примечания по платформам

- ESP32 требует один раз выполнить `west blobs fetch hal_espressif`.
- ESP32 production-сборки используют `CONFIG_ESP_SIMPLE_BOOT`; `wifi_ota.conf` требует MCUboot и `--sysbuild`.
- MG24 требует `west blobs fetch hal_silabs` и `pyocd`.
- nRF54L15 пока собирается без MCUboot.
- Для Heltec V3 serial-companion использует `uart0` как единый бинарный companion transport и текстовый CLI; console/shell в этом режиме отключаются.
- При смене платы или роли используйте `--pristine`.

## Архитектура

```text
Mobile App  <--BLE (NUS)--> [ Companion ]  <--LoRa-->  Mesh Network
                                  |
                            k_event_wait()
                           /      |       \
                    LORA_RX   LORA_TX_DONE  BLE_RX
```

Все основные пути исполнения событийные:

- **LoRa RX**: callback драйвера кладет пакет в ring buffer и будит mesh event loop.
- **LoRa TX**: отдельный поток ждет `k_poll()`, после завершения TX возвращает радио в RX и уведомляет mesh loop.
- **BLE**: NUS write handler кладет данные в `k_msgq`, TX идет через `bt_gatt_notify_cb()`.
- **USB**: CDC-ACM с бинарным V3 framing protocol и recovery по таймауту frame.
- **Main loop**: `k_event_wait()` блокируется до появления работы, housekeeping выполняется периодически.

## Отличия от Arduino MeshCore

| Область | Arduino | Zephyr |
|---------|---------|--------|
| Idle | Cooperative `loop()` | `k_event_wait(K_FOREVER)` и WFI между событиями |
| LoRa TX complete | ISR выставляет flag, `loop()` polling | ISR сигналит `k_poll_signal`, отдельный TX wait thread |
| BLE | Платформенный стек | Единый `bt_gatt` API |
| LoRa driver | RadioLib | Zephyr driver, DTS-configured SPI |
| Конфигурация | `platformio.ini` + `variant.h` | Kconfig + devicetree overlays |
| Потоки | Один `loop()` + ISR | Явные threads + system work queue |

## Adaptive Contention Window

ZephCore заменяет статические задержки `txdelay`, `rxdelay` и `direct.txdelay` адаптивным механизмом:

1. Узел считает дубликаты flood-пакетов, услышанные от соседей в коротком окне.
2. Эти данные попадают в EMA и управляют размером задержки перед ретрансляцией.
3. Если во время ожидания узел слышит, что сосед уже ретранслирует этот же пакет, он дополнительно отодвигает свой TX.

Это снижает задержку в редких линейных сетях и уменьшает коллизии в плотных сетях. Wire-протокол не меняется, совместимость с Arduino MeshCore сохраняется.

Полезные CLI-команды:

- `get txdelay` — показывает текущую adaptive delay оценку.
- `get backoff.multiplier` / `set backoff.multiplier` — управляет reactive backoff.
- `get tz` / `set tz <minutes>` — задает смещение локального времени UI в минутах, например `set tz 300` для GMT+5.
- `get autoshutdown` / `set autoshutdown <mV>` — показывает или задает порог ухода companion в сон/выключение по батарее.
- `get adc.multiplier` — показывает текущий ADC multiplier и прочитанное напряжение батареи в mV.

## Энергосбережение

- **LoRa RX duty cycle**: CAD-based прием снижает ток RX для SX1262 companion/repeater сборок.
- **Adaptive Power Control**: скомпилирован по умолчанию, включается командой `set tx apc`.
- **Production by default**: без логов и assert, reboot-on-fatal.
- **GPIO-gated GPS**: GPS включается только на время получения фикса, где это поддерживает плата.
- **BLE-off роли**: repeater, room-server и observer не тянут Bluetooth-стек.

## Конфигурация

Ключевые Kconfig-опции:

| Опция | По умолчанию | Описание |
|-------|--------------|----------|
| `CONFIG_ZEPHCORE_ROLE_COMPANION` | y | BLE companion mode |
| `CONFIG_ZEPHCORE_ROLE_REPEATER` | n | USB CLI repeater |
| `CONFIG_ZEPHCORE_ROLE_OBSERVER` | n | Listen-only WiFi+MQTT observer |
| `CONFIG_ZEPHCORE_RADIO_NATIVE` | y | SX1261/SX1262/SX1268, LLCC68, STM32WL |
| `CONFIG_ZEPHCORE_RADIO_LR1110` | n | LR1110/LR1120/LR1121 |
| `CONFIG_ZEPHCORE_RADIO_LR2021` | n | LR2021 |
| `CONFIG_ZEPHCORE_RADIO_SX127X` | n | SX1272/SX1276/SX1278 |
| `CONFIG_ZEPHCORE_LORA_RX_DUTY_CYCLE` | auto | CAD-based RX power saving |
| `CONFIG_ZEPHCORE_APC` | y | Adaptive Power Control, runtime off |
| `CONFIG_ZEPHCORE_DEFAULT_TX_POWER_DBM` | 22 | Начальная TX-мощность |
| `CONFIG_ZEPHCORE_MAX_TX_POWER_DBM` | 22 | Жесткий лимит TX-мощности |
| `CONFIG_ZEPHCORE_MAX_CONTACTS` | 350 | Количество контактов companion |
| `CONFIG_ZEPHCORE_MAX_CHANNELS` | 40 | Количество каналов companion |
| `CONFIG_ZEPHCORE_BLE_PASSKEY` | 123456 | BLE pairing PIN |
| `CONFIG_ZEPHCORE_GPS_POLL_INTERVAL_SEC` | 300 | GPS duty interval для companion |
| `CONFIG_ZEPHCORE_GPS_FIRST_FIX_TIMEOUT_SEC` | 300 | Cold-start окно для первого GPS fix |
| `CONFIG_ZEPHCORE_REPEATER_GPS_INTERVAL_SEC` | 172800 | GPS duty interval для repeater/room-server |
| `CONFIG_ZEPHCORE_UI_TIMEZONE_OFFSET_MINUTES` | 300 | Смещение локального времени UI в минутах; UTC timestamps не меняет |
| `CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS` | 3250 на nRF52 | Порог low-battery shutdown для companion, `0` отключает |
| `CONFIG_ZEPHCORE_WIFI_OTA` | n | WiFi AP + HTTP OTA для ESP32 repeaters |
| `CONFIG_ZEPHCORE_REPEATER_UPLINK` | n | WiFi+MQTT uplink для ESP32 repeater |
| `CONFIG_ZEPHCORE_PACKET_LOGGING` | n | Arduino-compatible packet logging |
| `CONFIG_ZEPHCORE_HOUSEKEEPING_INTERVAL_MS` | 5000 | Период housekeeping |

Базовые radio prefs для новых настроек:

```text
freq = 867.935 MHz
bw = 62.5 kHz
sf = 8
cr = 4/8
duty cycle = 50%
multi_acks = on
```

## Структура проекта

```text
zephcore/
  src/              Core mesh engine
  app/              Companion, Repeater, Room Server, Observer
  adapters/
    ble/            BLE NUS transport
    board/          GPIO, LED, power management
    clock/          Millisecond and RTC clocks
    datastore/      LittleFS filesystem wrapper
    gps/            GPS/GNSS integration
    mqtt/           MQTT publisher
    ota/            WiFi AP + HTTP firmware update server
    radio/          LoRa radio drivers
    rng/            Random number generator
    sensors/        Sensor discovery and telemetry
    transport/      TCP companion (Linux) и serial companion (STM32WL)
    usb/            USB serial transport
    wifi/           WiFi station client
  boards/
    nrf52840/       nRF52840 board overlays and configs
    esp32/          ESP32 board overlays and configs
    rp2040/         Raspberry Pi Pico / RP2040 overlays and configs
    nrf54l/         nRF54L15 overlays and configs
    mg24/           EFR32MG24 overlays and configs
    stm32wl/        Seeed LoRa-E5 overlays and configs
    linux_native/   native_sim presets для Femtofox и RAK6421
    common/         Shared Kconfig fragments and devicetree includes
  lib/              ED25519 crypto library
  patches/          Auto-applied patches to the Zephyr tree
```

## Лицензия

MIT License — см. [`zephcore/LICENSE`](zephcore/LICENSE). Лицензия совпадает с upstream MeshCore, на который этот порт сильно опирается.

Некоторые vendored-зависимости имеют свои совместимые лицензии; подробности указаны внизу `LICENSE`.

![ZephCore](https://github.com/user-attachments/assets/ddce17fd-7b83-4dc7-999f-0519593fcc3d)

Проект в значительной степени основан на [официальном репозитории MeshCore](https://github.com/meshcore-dev/MeshCore/) и работе его авторов.

Разработка и разбор проблем велись с использованием AI-инструментов. В исходной работе упоминались Claude и Cursor; доработка этой ветки, анализ ошибок, сравнение с MeshCore/Meshtastic и подготовка исправлений выполнялись также с помощью Codex.
