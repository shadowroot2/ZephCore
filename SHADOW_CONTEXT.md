# Shadow ZephCore Context

Краткий контекст нашей работы над форком ZephCore.

## Общие правила

- `tap` не трогать.
- Логику отправки mesh-сообщений не менять без отдельной просьбы.
- Готовые прошивки складывать в `firmware/`.
- После анализа спрашивать перед сборкой, если пользователь прямо не сказал собирать.
- Не создавать GitHub release и не отправлять изменения на GitHub без явной
  просьбы пользователя в текущем диалоге.
- После каждой сборки сообщать Flash и RAM; для ESP32 также сообщать IRAM.
- Для ESP32/S3 пока не создавать отдельный signed app image, если пользователь не попросит. Нужен merged-bin.
- Если сборка падает на `ccache: ... ~/Library/Caches/ccache/tmp ... Operation not permitted`,
  повторить ту же команду сборки с разрешением записи вне sandbox (`require_escalated`):
  причина в недоступном временном каталоге ccache, не в исходниках.

## Репозиторий

- Основной рабочий путь: `/Users/shadow/Work/CodeX/ZephCore`.
- Локальный Zephyr SDK: `/Users/shadow/Work/CodeX/ZephCore/zephyr-sdk-1.0.1`.
  Перед nRF-сборкой передавать этот путь через `CMAKE_PREFIX_PATH`.
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

- Статус зарядки: `EXT_CHRG_DETECT=P0.15`, `GPIO_ACTIVE_LOW`.
  `gpio_pin_get_dt()` уже возвращает логическое активное состояние с учетом
  active-low; правильная проверка — `gpio_pin_get_dt(&charge_detect) > 0`.
  Нельзя добавлять ручную инверсию: она уже приводила к ложным `6 W` без
  зарядки.
- В телеметрии M6 Repeater поле Power передается всегда: `6 W` при активном
  `P0.15`, `0 W` без зарядки.  Это номинальная мощность солнечной панели
  (`charge-power-mw = <6000>`), а не измерение текущей мощности.
- Параметр мощности солнечной панели должен быть только у M6.
- Для M6 GPS: Air530Z/L76K управляется через `GPS_EN=P0.06`,
  `GPS_STANDBY=P0.30` принудительно HIGH gpio-hog. `GPS_RESET` не трогать,
  deferred init и лишние reset/sleep aliases не возвращать. Пользователь
  подтвердил рабочий GPS/fix в этой конфигурации.

## ThinkNode M1

- Использовался как эталон для GPS-старта.
- GPS на M1 работал сразу, потому что цепочка питания/старта уже была правильной.
- При сборках M1 не нужен repeater, если пользователь просит companion/client.

## LilyGo T-Echo

- Companion-прошивка 433 МГц с профилем `434.030 MHz` и `50 mW` собрана в
  `firmware/lilygo_techo-companion-2026-08-13-433-434030-50mw.uf2`.
- Пользователь подтвердил: в этой прошивке дисплей и тач-кнопка подсветки
  работают.
- Пользователь подтвердил: сенсор подсветки P0.11 снова работает после
  перевода входа на GPIO SENSE (`sense-edge-mask = <0x00000800>`).
- Для поздней ревизии T-ECHO RGB-светодиод активен низким уровнем: красный
  `P1.03`, синий `P0.14`, зелёный `P1.01`. В companion 433 подтверждено:
  красный heartbeat и синий индикатор LoRa TX работают. Актуальный образ:
  `firmware/lilygo_techo-companion-2026-08-13-433-434030-50mw-rgbfix.uf2`.

## T1000-E

- Проверялась поддержка датчика освещенности.
- Сравнивались ZephCore, MeshCore и Meshtastic.
- Добавлялась поддержка отображения освещенности в телеметрии.
- После правок нужно следить, чтобы BLE не был отключен в companion-сборке.
- В поле luminosity оставлять реальные люксы: число спутников туда не писать.
- При отключенном BLE входящее сообщение должно трижды мигать LED даже при
  `LEDs off`; после завершенной физической отправки сообщения или advert LED
  загорается на 2 секунды независимо от BLE и настройки LEDs.
- Подтверждение переключения: LEDs on — один короткий blink, LEDs off — два;
  аналогичное подтверждение нужно для GPS и buzzer.
- QMA6100P акселерометр используется в companion для обнаружения падения.
- Пользователь подтвердил 2026-08-21: T1000-E обнаруживает падение. Рабочий
  критерий покоя QMA6100P — не min/max ускорения, а доля стабильных выборок
  относительно первой нормальной опорной точки (±500 mg): единичные выбросы
  датчика до 10 g не должны отменять событие.
- Пользователь подтвердил 2026-08-23 индикацию зарядки T1000-E. USB-C
  определяется только по `EXT_PWR_DETECT=P0.05`; `P1.03/CHRG` не использовать
  для полного заряда — он даёт ложный DONE. Полный заряд — только ADC
  `>=4190 mV`, постоянный зелёный. При подключённом USB: до 50% — одна,
  50–74% — две, 75–99% — три зелёные вспышки; вспышка 150 ms, интервал 200 ms,
  после серии пауза 1.5 s. Этот режим имеет приоритет над heartbeat и
  сообщениями. Подтверждённый образ:
  `firmware/t1000_e-companion-2026-08-23-charge-series-fast.uf2`.
- T1000-E Fall: фактическое падение с 1–1.5 м в QMA6100P даёт low-g около
  790 mg, удар около 1.43–1.5 g и устойчивое состояние после удара. Шкала
  чувствительности идёт от большей к меньшей: `1` — максимум, `5` — минимум.
  Подтверждённая рабочая основа: оставить штатные clock/filter-настройки
  QMA6100P после reset; не задавать PM/ODR принудительно и не добавлять
  общий фильтр по плавающей базовой величине датчика — он блокирует Fall
  на всех уровнях. Уровень 3: low-g ≤800 mg, удар ≥1.25 g в 350 мс,
  новая поза ≥450 mg и 8 с покоя (≥88% стабильных выборок). Удары рюкзака
  отсекаются двумя последними условиями. Сигнал проигрывается только после
  полного подтверждения Fall, включая `buzz off`. Пользователь подтвердил,
  что текущий вариант работает приемлемо. Текущий образ:
  `firmware/t1000_e-companion-2026-08-23-fall-qma-restored.uf2`.

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
- GPS-страница UI намеренно убрана.
- USB-C Heltec V3 — это мост CP2102 к `uart0`, не native USB Serial-JTAG.
  Для CLI без BLE обязателен `CONFIG_ZEPHCORE_COMPANION_SERIAL=y`; console/shell
  на `uart0` должны быть выключены, иначе их UART IRQ callbacks конфликтуют с
  Companion transport и команды не доходят до обработчика.
- В overlay включен `&coretemp { status = "okay"; };`: это восстанавливает
  температуру в телеметрии и в low-battery emergency-сообщении. Это температура
  кристалла ESP32-S3, не окружающего воздуха.

## ThinkNode M5 Companion

- USB-C is a UART0 bridge. The Companion transport and text CLI own UART0;
  `CONFIG_CONSOLE`, `CONFIG_UART_CONSOLE` and `CONFIG_SHELL` must remain off,
  otherwise the UART callbacks conflict and CLI input is not delivered.
- M5 selects `ZEPHCORE_COMPANION_SERIAL_POLLING`: if the UART0 bridge fails
  to deliver RX interrupts, the transport drains its hardware FIFO every 5 ms
  with `uart_poll_in()` and sends replies with `uart_poll_out()`. This is
  M5-only; all other serial-companion boards retain interrupt-driven I/O.
  Verified on hardware: local USB CLI (`help`, `gps`) responds correctly with
  polling enabled; the previous interrupt-driven path accepted no command
  bytes despite the host opening the serial port.

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

## Companion: лимиты, CLI, GPS и батарея

- Лимиты всех Companion: 200 контактов, 100 offline сообщений.
- `help` доступен только в локальном CLI и не должен пересылаться по LoRa.
  Выводить одну команду на строку, только доступные текущей роли/плате команды;
  сервисные команды находятся внизу. `gps [on|off|setloc|advert]` идет сразу
  после `time` на платах с GPS.
- Для Companion `help` также доступен через loopback vContact: полный список
  разбивается на несколько локальных chat-сообщений из-за лимита пакета. По
  LoRa он по-прежнему никогда не отправляется.
- vContact появляется в приложении после повторного подключения: при первом
  соединении приложение может запросить список контактов до синхронизации
  времени, а виртуальный контакт активируется только после валидного RTC.
  Подтверждено на M1 2026-07-27; это не означает, что `v.contact` отключён.
- Из Companion help исключены `guest.password`, `allow.read.only`, `rxduty`;
  `stop ota` не показывается на NRF companion/repeater.
- Для M1/M5/M6 показывать и передавать `Sats in view`, а не только спутники,
  использованные для fix; единый лимит — 32. В Cayenne LPP число видимых
  спутников передается через luminosity на GPS-платах, кроме T1000-E.
- Не трогать GPS RESET на M1, M5 и M6: пользователь подтвердил работоспособность
  M6 после его удаления.
- M1 использует ту же LiPo-кривую, что и M5 (100% от 4.10 V). При заряде ниже
  25% отключать buzzer и стартовую мелодию; heartbeat — три быстрых blink раз
  в пять секунд.
- Low-battery auto-shutdown (не ручное выключение) перед отключением отправляет
  в `#zephcore` voltage, temperature и uptime. Если публичного канала нет,
  он создается автоматически как `#zephcore`, с ключом public-channel от полного имени. На GPS-платах добавляет `GPS: off`,
  либо `Sats in view` и Google Maps ссылку только при ненулевых координатах.
  Префикс: `Low batt - Shutting down`. При выключении из меню очищать экран и
  показывать `Power OFF`.
- На M1 подтверждено: при auto shutdown E-Ink должен удерживать экран
  `Low Battery / Shutting Down` весь восьмисекундный grace period. Обычная
  перерисовка UI в этот период отключена, иначе возвращается старый экран.
- Отправка этого emergency-сообщения в `#zephcore` при автоотключении — отдельная
  сохраняемая настройка `get/set autoshutdown.emergency on|off` (по умолчанию `on`). При
  `off` автоотключение и vContact/flash fallback остаются активными; меняется
  только отправка emergency-сообщения. Поле добавлено в конец prefs blob, поэтому старые
  пользовательские файлы без него мигрируют безопасно с дефолтом `on`.
- T-1000E: мелодия `findme` на C6 (~1047 Гц), Morse `ZEPHCORE`, подтверждена
  пользователем как подходящая. Это общая мелодия всех companion-плат с buzzer.
- ProMicro nRF52840 + E22/SX1262: SH1106 1.3" на I²C0 (`P1.04` SDA,
  `P0.11` SCL, `0x3C`) подтверждён рабочим; прошивка, BLE и телеметрия
  работают без GPS. Кнопка `P1.00` переведена на схему Heltec V3: один tap —
  следующая страница, два — предыдущая, удержание 1 с — enter; аппаратная
  проверка этой доработки ещё требуется. При `TX=22` BLE отваливается и сам переподключается: E22-900M30S
  с внешним PA перегружает питание ProMicro; штатный безопасный дефолт — 10 dBm.
  GPS-модуль — ATGM336H (не Air530Z): UART NMEA-0183 9600, `P0.22` GPS_TX,
  `P0.20` GPS_RX, `P0.24` GPS_EN. Для него используется
  `gnss-nmea-generic` с `zephyr,deferred-init`, чтобы GPS manager включил
  питание до открытия UART; число спутников в GSV — до 32. Требуется
  аппаратная проверка BLE/USB и GPS после этой замены.

## Repeater Bridge

- Отдельная роль `repeater-bridge` включает полный код обычного repeater и
  передаёт только group/flood пакеты и adverts между двумя LoRa-сетями.
  Direct, ACK и admin-пакеты не мостятся.
- На nRF52 транспорт моста — зашифрованный BLE GATT. На ESP32 доступны оба
  транспорта: по умолчанию ESP-NOW (Wi-Fi channel 1), либо BLE GATT для
  моста с nRF. Команды: `get bridge`, `get bridge.type`,
  `set bridge.type ble|esp-now`, `set bridge.peer <MAC>`,
  `set bridge.key <32-hex>`, `bridge keygen`. Команда `bridge` выводит статус
  линка, `bridge on|off` сохраняет включение канала. `bridge keygen` разрешена
  только из локального USB CLI, поскольку возвращает ключ. Peer, ключ, тип и
  состояние хранятся во flash отдельно от `NodePrefs`.
- Heltec V3 repeater-bridge 868: подключение через выбранный configurator
  подтверждено пользователем как работающее 2026-08-14.
- ESP-NOW между repeater-bridge подтверждён пользователем как рабочий:
  линк устанавливается и пересылка пакетов работает (2026-08-14).
- BLE bridge M6 868 ↔ Heltec V3 433 повторно подтверждён пользователем
  2026-08-22 после исправления CCC discovery: статус `connected`,
  `bridge ping` — 146 ms. Критическая деталь: в `bt_gatt_subscribe()`
  `value_handle` нужно сохранять из характеристики до discovery CCC; у CCC
  дескриптора value handle равен 0, из-за чего READY/ACK-уведомления терялись.
  Для этой пары M6 `9E:E8:62:0B:0D:D5` (random), Heltec
  `76:79:3A:43:CA:48` (random). После смены peer нужно указать адрес именно
  второй платы, а не сохранённый от прежней пары.

## Полезные команды

### ESP-платы: правило выпуска

- Для любой ESP-платы публиковать только полный `-merged.bin`, прошиваемый с
  адреса `0x0`: MCUboot + подписанное приложение в одном файле.
- Смещение приложения брать из `slot0_partition` конкретной платы. Для Heltec
  V3 это `0x10000`; `0x20000` для него неверно и оставляет MCUboot без приложения.

M5 build:

```sh
CMAKE_PREFIX_PATH=/Users/shadow/Work/CodeX/ZephCore/zephyr-sdk-1.0.1 \
CCACHE_DIR=/Users/shadow/Work/CodeX/ZephCore/.ccache \
./.venv/bin/west build -b thinknode_m5/esp32s3/procpu zephcore --pristine --sysbuild
```

M1 companion build:

```sh
CMAKE_PREFIX_PATH=/Users/shadow/Work/CodeX/ZephCore/zephyr-sdk-1.0.1 \
CCACHE_DIR=/Users/shadow/Work/CodeX/ZephCore/.ccache \
./.venv/bin/west build -b thinknode_m1 -d build_thinknode_m1_companion zephcore --pristine
mv build_thinknode_m1_companion/zephyr/zephyr.uf2 firmware/thinknode_m1-companion-<commit>.uf2
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

- `thinknode_m1-companion-8faf0fd.uf2` — ThinkNode M1 companion после merge
  upstream 1.16.8; сборка успешна 2026-08-11, FLASH 397032 B (57.02%),
  RAM 174248 B (66.47%).
- `shadow-20260715-thinknode-m1-client.uf2` — ThinkNode M1 client/companion.
- `shadow-20260715-thinknode-m5-client-merged.bin` — ThinkNode M5 client/companion, ESP32-S3 merged-bin, offset `0x0`.
- `shadow-20260715-thinknode-m6-repeater.uf2` — ThinkNode M6 repeater с GPS fix и solar `6W`.
- `shadow-20260715-heltec-v3-client-merged.bin` — Heltec V3 client/companion, ESP32-S3 merged-bin, offset `0x0`, с battery/sleep правками.
- `shadow-20260715-t1000-e-client.uf2` — Seeed T1000-E client/companion с light telemetry.

SHA в ответы не выводить, пользователь попросил не показывать.

## Repeater Bridge UI

- Пользователь подтвердил 2026-08-14: счётчики `Forwarded` / `Skipped` на
  странице `BRG INFO` работают; обновление экрана приходит с задержкой
  maintenance-прохода (до 60 с в Heltec V3 bridge), не сразу после пересылки.

## ThinkNode M3 Companion

- Плата: Elecrow ThinkNode M3, `nRF52840 QIAA` + `LR1110`, BLE, GNSS NMEA
  (UART0, 9600), LoRa и buzzer. Цель сборки Zephyr: `thinknode_m3`;
  выпускаемый образ — UF2 в `firmware/`.
- GNSS: питание `P0.14`, wake/standby `P0.21`; `P0.25` (`GPS_RESET`/REINIT)
  нельзя назначать и трогать — на реальной плате это останавливает NMEA.
  Модуль использует общий NMEA-драйвер, без чип-специфичных команд.
- На плате есть AHT10 (I²C0, `0x38`, SDA `P0.26`, SCL `P0.27`) и RTC PCF8563.
  NFC-антенны нет: возможность NFC nRF52840 не использовать.
- AHT10 нельзя обслуживать стандартным узлом AHT20 Zephyr: нужен init
  как в Meshtastic `Adafruit_AHTX0`: пауза 20 ms, soft-reset `BA`, ожидание
  idle, `E1 08 00`, ожидание calibrated, затем `AC 33 00` и чтение 6 байт.
  Статус читается непосредственно, без команды `0x71`. В ZephCore он читается
  напрямую в `adapters/sensors/ZephyrEnvSensors.cpp`; узел `aht20` в DTS
  отключён.
  Телеметрия M3 должна передавать температуру и влажность AHT10, а не MCU temp.
  Поправка температуры датчика: `-5.0 °C`
  (`CONFIG_ZEPHCORE_AHT_TEMP_OFFSET_MILLIC=-5000`), как в Meshtastic.
- Питание AHT10 — `P0.03` (`DHT_POWER`), обязано быть HIGH gpio-hog в
  `boards/nrf52840/thinknode_m3/board.overlay`. Без него AHT10 не отвечает,
  поэтому в телеметрии нет температуры и влажности. Пользователь подтвердил
  2026-08-21: после включения `DHT_POWER` и последовательности Meshtastic
  температура и влажность в телеметрии работают.
- SC7A20H акселерометр M3 используется в companion для обнаружения падения;
  его питание `ACC_POWER=P0.02` активно низким уровнем.
- Батарея: пользователь подтвердил 2026-08-21 рабочее показание `4.20 V`.
  Конфигурация совпадает с Meshtastic: AIN3 / `P0.05`, 12 бит,
  `ADC_GAIN_1_4` + internal ref (эквивалент `AR_INTERNAL_2_4`) и множитель
  `4200` для делителя 1.75:1. Для этой платы не возвращать `GAIN_1_6` и
  множители `5920`/`6300`; после старых сборок выполнить
  `set adc.multiplier 0`, чтобы снять сохранённый override.
  Отдельная M3-кривая считает `100%` от `4130 mV` (верхняя точка 4.13 V),
  поскольку после зарядки тестовая плата устойчиво давала 4139 mV.
- RGB светодиод active-low, питание LED `P0.29`:
  - зелёный `P1.03`: heartbeat; при непрочитанных сообщениях — два импульса;
  - красный `P1.01`: низкий заряд ниже 20%, три коротких импульса; этот режим
    имеет приоритет над периодическим зелёным индикатором;
  - синий `P1.05`: передача LoRa.
- Индикация зарядки M3 подтверждена пользователем 2026-08-21:
  `EXT_PWR_DETECT=P0.31` (`GPIO_ACTIVE_HIGH`) — единственный надёжный признак
  подключённой магнитной зарядки. Пока он активен: красный 1 с / пауза 1 с;
  зелёный heartbeat, непрочитанные сообщения и LoRa TX подавлены. После снятия
  зарядки красный гаснет сразу. `P1.0/CHRG` не использовать для определения
  питания: на этой ревизии может залипнуть и держать красный бесконечно.
  Полный заряд: неактивный CHRG 20 с либо fallback при USB и ADC >=4130 mV;
  ADC опрашивается раз в 30 с, после чего горит постоянный зелёный. Эта
  финальная логика подтверждена пользователем как рабочая.
- Кнопка `P0.12`, active-low. Один tap — следующая страница, два — LEDs on/off,
  три — buzzer mute, четыре — GPS on/off, пять — flood advert; long press 1 s.
- Buzzer: PWM `P0.23`, enable `P1.04`. Питания: `P0.16` PWR_EN, `P0.17`
  BAT_POWER, `P0.07` EEPROM_POWER — все должны быть HIGH через gpio-hog.
- M3 использует bootloader SoftDevice `S140 v6`, `CONFIG_ZEPHCORE_SD_FWID=0x00B6`.
- Последняя прошивка: `firmware/thinknode_m3-companion-2026-08-21-charge-4130-immediate.uf2`
  (UF2), содержит питание AHT10, ADC-конфигурацию Meshtastic и подтверждённую
  логику зарядки с fallback по 4130 мВ.

## Fall alarm: ThinkNode M3 и T1000-E

- Пользователь подтвердил 2026-08-21: обнаружение падения работает на обеих
  платах. Сценарий: свободное падение → удар → неподвижность; ложные повторные
  срабатывания блокируются на 60 с.
- При падении в `#sos` сразу уходит `Fall detected! Waiting GPS fix...`, далее
  раз в 30 с до свежего GPS-фикса; финальное сообщение —
  `Fall detected! I may need help.` с телеметрией и координатами.
- Мелодия SOS повторяется раз в 30 с до первого физического нажатия кнопки.
  Оно отменяет Fall полностью, отправляет `Fall canceled...`, прекращает
  дальнейшие сообщения и возвращает исходные GPS duty/state.
- SOS и Fall взаимно заменяют активный сценарий; Fall не показывает на экране
  статусы SOS. Обычный SOS сохраняет прежнее поведение при выключенном buzzer.
- Уровень Fall сохраняется в prefs и меняется через `get/set fall.sens 1..5`.
  По умолчанию `3`: исходный профиль M3 (50 Гц, ≤650 mg → ≥2.8 g за 600 мс,
  затем 1 с пауза и 8 с покоя с минимум 85% стабильных выборок). `1` строже,
  `5` чувствительнее. На T1000-E критерий покоя относительный из-за смещения
  QMA6100P, но фазы и профиль уровня 3 те же, что у M3.
- Только Fall принудительно включает Morse SOS каждые 30 с даже при `buzz off`.
  Обычный SOS и UI-звуки продолжают уважать `buzz off`.
- Последние подтверждённые образы:
  `firmware/thinknode_m3-companion-2026-08-21-fall-sos-switch.uf2` и
  `firmware/t1000_e-companion-2026-08-21-fall-sensitivity.uf2`.

## Pico W Room Server

- Собирать только `firmware/picow-room-server.uf2`. Образ
  `waveshare_rp2040_lora-room-server.uf2` не создавать и не включать в
  результаты без отдельного запроса пользователя.
- Для Pico W полностью отключены `WIFI_AIROC`, `WIFI`, `NETWORKING` и
  CYW43 GPIO. Wi-Fi и штатный зелёный LED не используются: LED подключён к
  `WL_GPIO0` CYW43439 и без его драйвера недоступен. Не добавлять их снова без
  явного запроса пользователя.
- Последняя сборка без Wi-Fi: `firmware/picow-room-server.uf2`, Flash 12.39%
  (194860 / 1572608 B), RAM 31.82% (85692 / 263 KB), UF2 390656 B.

## Repeater Bridge: M6 ↔ Heltec V3 433 BLE

- Пользователь подтвердил 2026-08-25: BLE bridge между ThinkNode M6 (868) и
  Heltec V3 (433) работает после исправления реконнекта: статус `connected`,
  `bridge ping` — 97 ms.
- Роли определяются детерминированно по identity MAC: Heltec
  `76:79:3A:43:CA:48 public` — central/scan; M6
  `9E:E8:62:0B:0D:D5 random` — peripheral/advertising.
- `set bridge.peer` нужен на обеих платах и указывается с типом адреса
  (`public` или `random`); значение — identity MAC второй платы. Одинаковый
  `bridge.key` (32 hex) также обязателен на обеих. MAC не используется как
  пароль: он выбирает адресата и роль; пакеты bridge аутентифицирует key,
  GATT-транспорт дополнительно шифрует SMP.
- Соединение инициирует только central (в этой паре Heltec): он сканирует
  peer identity, подключается, поднимает L2 encryption, обнаруживает GATT
  service/characteristic/CCC и включает Notify. Peripheral (M6) рекламируется,
  принимает ACL и после шифрования ждёт HELLO. HELLO/READY/ACK завершают
  логический bridge; только после этого статус `connected` и `bridge ping`
  действительно означают готовый обмен пакетами.
- Полезная диагностика `bridge`: `p` — текущая фаза (`scan`, `adv`, `secure`,
  `discover`, `hello`, `ack`, `connected`); `r` — роль (`C` central, `P`
  peripheral); `t` — тип peer MAC (`P` public, `R` random); `f` — причина
  последнего сбоя; `s` — ошибка SMP, `d` — HCI reason разрыва, `c` — ошибка
  connect, `n` — число попыток реконнекта. При рабочем линке: `p=connected`,
  `f=none`, а `bridge ping` даёт pong.
- Ранее зависания возникали на Legacy SMP: в режиме SC-only nRF и ESP32-S3
  не согласовывали pairing (`s=4`/`s=9`), а старый LTK оставался одновременно
  в NVS и RAM Zephyr. Поэтому новый ключ после разрыва считался небезопасной
  заменой и обе стороны возвращались в `security`/`scan`/`adv`.
- Для bridge на обеих платформах обязательно
  `CONFIG_BT_SMP_SC_PAIR_ONLY=n`: ESP32-S3 и nRF могут не договориться в
  SC-only режиме (`s=4` на M6, `s=9` на Heltec). L2 encryption, bonding и
  `bridge.key` при этом остаются включены. Настройка должна быть в обоих
  файлах: `boards/common/repeater_ble_bridge.conf` (nRF) и
  `boards/common/repeater_espnow_bridge.conf` (ESP bridge, даже при
  `bridge.type ble`).
- На обеих конфигурациях требуются `CONFIG_BT_BONDABLE=y`,
  `CONFIG_BT_SETTINGS=y`, `CONFIG_BT_MAX_PAIRED=1`. ESP32-S3 отвергает
  non-bonding Legacy SMP, поэтому "постоянно выключить bonding" — неверное
  решение. `CONFIG_BT_BONDING_REQUIRED=n` оставлен, чтобы обычное соединение
  не требовало старого bond до установления шифрования.
- Доработан реконнект: при разрыве именно активного bridge ACL вызывается
  `bt_unpair(BT_ID_DEFAULT, nullptr)`, очищающий сохранённый bond и RAM LTK,
  затем запускается retry с прежней ролью. Отвергнутые посторонние
  подключения ключи peer не трогают. Ошибка security единожды делает ту же
  очистку и принудительно переподключает link. `bridge unpair`, смена peer и
  смена key также сбрасывают LTK до нового pairing. Health ping/watchdog
  отслеживает отвал уже установленного link и переводит его в этот же путь.
- После прошивки обеих плат надо один раз задать peer/key и включить
  `bridge on`; для ручного сброса старых ключей на обоих узлах выполнить
  `bridge unpair`, дождаться нового pairing и проверить `bridge ping`.
- Рабочие образы: `firmware/thinknode_m6-repeater-bridge-2026-08-24-ble-reconnect-bond-reset.uf2`
  и `firmware/heltec-v3-repeater-bridge-433-2026-08-24-ble-reconnect-bond-reset-merged.bin`
  (Heltec merged, адрес 0x0). В этих сборках pairing bondable; LTK и bond
  удаляются после реального разрыва bridge ACL, поэтому реконнект создаёт
  свежие ключи и не зависает в `security`.
