<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="img/kite-network-logo-thick-bright.svg">
    <img src="img/kite-network-logo-thick-dark.svg" alt="Логотип ZephCore" width="140">
  </picture>
</p>

<h1 align="center">ZephCore</h1>

<p align="center"><b>MeshCore на Zephyr RTOS с доработками ShadoW</b></p>

<p align="center">
  <a href="https://github.com/liquidraver/ZephCore/actions/workflows/build.yml"><img src="https://github.com/liquidraver/ZephCore/actions/workflows/build.yml/badge.svg" alt="сборка"></a>
  <a href="https://github.com/liquidraver/ZephCore/actions/workflows/tests.yml"><img src="https://github.com/liquidraver/ZephCore/actions/workflows/tests.yml/badge.svg" alt="тесты"></a>
  <a href="https://github.com/liquidraver/ZephCore/releases/latest"><img src="https://img.shields.io/github/v/release/liquidraver/ZephCore" alt="последний релиз"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT"></a>
</p>

<p align="center">
  <a href="https://zephcore.meshcore.dev">Прошить</a> ·
  <a href="docs/supported_boards.md">Платы</a> ·
  <a href="docs/COMMANDS.MD">Команды</a> ·
  <a href="docs/SHADOW_FEATURES.md">Доработки ShadoW</a> ·
  <a href="releasenotes/RELEASE_NOTES_1.18.0-zephcore.md">Изменения 1.18.0</a>
</p>

---

ZephCore — прошивка для LoRa mesh-узлов, совместимая с протоколом и приложениями
[MeshCore](https://github.com/meshcore-dev/MeshCore/). Вместо Arduino она использует
[Zephyr RTOS](https://zephyrproject.org/), но остаётся совместимой с обычными узлами MeshCore
по радиоэфиру и companion-протоколу.

## Авторство и основа этой ветки

Автор ZephCore и основной код прошивки — **liquidraver**.

- **Обязательная ссылка на авторскую прошивку:**
  [github.com/liquidraver/ZephCore](https://github.com/liquidraver/ZephCore)
- Исходный проект и протокол:
  [github.com/meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore/)
- Рабочая ветка ShadoW:
  [github.com/shadowroot2/ZephCore](https://github.com/shadowroot2/ZephCore)
- Основа этой ветки: авторская **ZephCore 1.18.0** с сохранёнными и дополненными
  доработками ShadoW для экранов, CLI, SOS, Fall, Tracking, индикации,
  питания, GPS, телеметрии и конкретных плат.

Это не официальная прошивка MeshCore и не попытка присвоить авторскую работу.
Полный список отличий нашей ветки хранится в
[SHADOW_FEATURES.md](docs/SHADOW_FEATURES.md).

> [!NOTE]
> Обычное обновление внутри нашей ветки сохраняет identity, настройки, контакты и BLE-пару.
> При переходе между stock MeshCore и ZephCore настройки могут потребовать повторной настройки.

## Установка

1. **Web Flasher:** откройте [zephcore.meshcore.dev](https://zephcore.meshcore.dev) в Chrome или Edge,
   выберите плату и роль, подключите устройство и прошейте его.
2. **Mesh America Device Configurator:** [apps.meshamerica.com](https://apps.meshamerica.com).
3. **Вручную:** загрузите наш образ из
   [Releases ветки ShadoW](https://github.com/shadowroot2/ZephCore/releases).
   Для nRF52 используется `.uf2`; для ESP32 — полный `-merged.bin` с offset `0x0`.

Авторские сборки без наших дополнений находятся в
[Releases liquidraver/ZephCore](https://github.com/liquidraver/ZephCore/releases).

После прошивки подключитесь через приложение MeshCore. Стандартный BLE PIN — `123456`.
Точный список плат и build-target находится в [docs/supported_boards.md](docs/supported_boards.md).

## Роли устройства

| Роль | Назначение | Управление |
|---|---|---|
| **Companion** | Личный узел: контакты, каналы, offline-очередь сообщений | MeshCore по BLE, USB, а на части ESP32 также по WiFi |
| **Repeater** | Ретрансляция пакетов сети | USB CLI или удалённая CLI по LoRa |
| **Room server** | Общая доска сообщений с хранением пропущенных публикаций | CLI репитера и вход пользователей из приложения |
| **Observer** | Только приём эфира и публикация в MQTT через WiFi | Отдельная USB CLI |
| **KISS modem** | Сырой LoRa-модем для программы на компьютере | KISS-протокол по USB или UART |
| **Repeater bridge** | Репитер с межчастотным мостом между двумя узлами | USB/LoRa CLI, ESP-NOW или BLE bridge |

Для одного образа выбирается одна роль.

## Что даёт авторская ZephCore поверх MeshCore

- **Zephyr RTOS и событийная архитектура:** основной цикл спит между событиями, драйверы используют
  подсистемы Zephyr, devicetree и Kconfig.
- **Самонастройка задержек ретрансляции:** репитер учитывает загрузку соседней сети вместо постоянного
  `txdelay` ([ADR 0007](docs/adr/0007-adaptive-contention-window.md)).
- **Адаптивный CAD/LBT:** порог занятости канала подстраивается под локальный шум
  ([ADAPTIVE_CAD.md](docs/ADAPTIVE_CAD.md)).
- **RX duty cycle:** поддерживаемые радиочипы снижают потребление между проверками преамбулы.
- **Mesh Time Sync:** узлы без GPS могут согласовать время; функция выключена по умолчанию
  ([MESHTIMESYNC.md](docs/MESHTIMESYNC.md)).
- **Observer и MQTT uplink:** приём LoRa с публикацией в MQTT, а также MQTT-подключение репитера.
- **Управление LED:** глобальное отключение, режимы TX/RX и heartbeat.
- **Off-grid:** companion может временно ретранслировать handheld-узлы на отдельной off-grid частоте
  ([OFFGRID.md](docs/OFFGRID.md)).
- **LR2021 2.4 ГГц**, Linux Native на Raspberry Pi/Femtofox и KISS modem.

## Доработки ShadoW в 1.18.0

### CLI и vContact

- USB CLI выводит полный список команд. BLE и удалённая LoRa CLI используют категории
  `id`, `rf`, `cad`, `mesh`, `find`, `gps`, `wifi`, `extra`, `ui`, `power`, `stat`, `log`, `sys`.
- Ответы удалённого `help <group>` ограничены размером BLE/LoRa frame; секреты, BLE PIN,
  RF-настройки, erase, OTA/DFU и выключение остаются локальными.
- Добавлены короткие команды `gps`, `buzz`, `leds`, `tz`, команды Fall, Tracking, SOS,
  WiFi, питания, индикации и диагностики. Полное дерево: [COMMANDS.MD](docs/COMMANDS.MD).
- vContact используется только для уведомлений. На входящую команду он отвечает:
  `Please use the Command Line in Settings. This chat is for notifications only.`
- В vContact приходят изменения GPS, buzzer, LEDs, BLE, Offgrid, Tracking, Wake on message,
  поворота экрана/ввода, ручного выключения и причины перезапуска.

### SOS, Fall и Tracking

- SOS ожидает свежий GPS fix, повторяет промежуточное сообщение и завершает отправку с координатами
  либо по тайм-ауту. Состояние и duty interval GPS после сценария восстанавливаются.
- На T-1000E и ThinkNode M3 короткое нажатие отменяет ожидающий ручной SOS, включает мелодию
  отмены и отправляет `❌ SOS Canceled` в `#sos`.
- Удалённые `sos` и `tracking on|off` полностью беззвучны, включая промежуточные и финальные отправки.
- Для T-1000E и M3 добавлен Fall detector с настройкой чувствительности, повторной тревогой и
  физическим подтверждением отмены.
- Tracking периодически отправляет координаты в выбранную группу; доступны интервал, группа,
  экранная страница и кнопочный жест.

### Экраны и кнопки

- Восстановлены авторские страницы и наша заставка с версией, датой и подписью
  `Tuned by ShadoW` для button UI и joystick UI.
- Добавлены и сохранены страницы Tracking, SOS, WiFi, GPS, Advert, Buzzer, LEDs, Offgrid,
  DFU и Shutdown с учётом возможностей платы.
- WiFi companion показывает SSID и адрес одной строкой `IPv4:port`; WiFi можно включить или
  выключить с экрана, после чего устройство перезагружается.
- Heltec V3 получает USB companion/CLI через CP2102/UART0 без конфликта с console.
- На M1/M5 физический переключатель GPS определяет стартовое состояние. При OFF экран показывает
  `GPS switch: off`, а не ошибочное `GPS: not detected`.
- При выключении отображается `Power OFF`; E-Ink сохраняет последний кадр.

### GPS, питание и индикация

- GPS принимает только подтверждённый спутниками fix и не переводит часы назад по старым данным
  после пробуждения.
- Неустановленный опциональный GPS определяется по отсутствию NMEA и больше не будит устройство
  на каждом цикле телеметрии.
- M1/M5/M6 показывают видимые спутники во время поиска; для T-1000E сохраняется настоящая
  телеметрия освещённости.
- Исправлены питание, UART и запуск GPS на M1/M5/M6, а также board-specific схемы батареи и зарядки.
- T1000-E, M3 и XIAO используют процентные зарядные серии. XIAO при зарядке сохраняет один обычный
  heartbeat pulse вместо трёх low-battery импульсов.
- На T-Echo low-battery красный LED больше не остаётся включённым после первого импульса.
- Companion перед автовыключением может отправить сообщение о низком заряде в `#zephcore`;
  репитер имеет отдельные настраиваемые battery alerts.

### Repeater bridge и сеть

- Repeater bridge связывает две LoRa-сети через ESP-NOW или BLE, фильтрует типы пакетов и защищается
  от циклов и повторов.
- Добавлены диагностические страницы, CLI, RSSI, журнал BLE bridge, управление bond и восстановление
  соединения без блокировки основного LoRa-цикла.
- Off-grid включается только на выделенной частоте 433.000, 869.495 или 918.000 МГц и возвращает
  прежнюю частоту после выключения.

Детальное поведение, аппаратные ограничения и статус проверок:
[SHADOW_FEATURES.md](docs/SHADOW_FEATURES.md).

## Изменения основной MeshCore 1.18.0

- ACK на direct-маршрутах ретранслируются после небольшой случайной задержки; при `multi.acks 1`
  две копии отправляются с разносом 300 мс.
- Flood-пакеты с transport code `0` считаются unscoped. В нашей ветке к ним дополнительно применяются
  `flood.max.unscoped` и `region denyf *`.
- Room server больше не включает repeater через устаревшую команду `set repeat on`.

Полное описание релиза: [RELEASE_NOTES_1.18.0-zephcore.md](releasenotes/RELEASE_NOTES_1.18.0-zephcore.md).

## Поддерживаемые платы

<!-- Generated by `python zephcore/scripts/board_manifest.py docs`; edit the board manifests, not this table. -->
<!-- boards:summary -->
| Платформа | Платы | С опубликованной прошивкой |
|---|---:|---:|
| nRF52840 | 22 | 21 |
| Семейство ESP32 | 16 | 14 |
| nRF54L | 4 | 3 |
| EFR32MG24 | 1 | 1 |
| STM32WL | 1 | 1 |
| Native Linux (пресеты) | 3 | 3 |
<!-- /boards -->

Поддерживаются RAK4631, SenseCAP T1000-E, ThinkNode M1/M3/M5/M6, T-Echo,
Wio Tracker L1, Heltec T114 и V3/V4, XIAO ESP32-S3/nRF52840/nRF54L15,
Station G2, MG24, Seeed LoRa-E5, Femtofox, Raspberry Pi с RAK6421 HAT и другие.

Полный список с точными target: [docs/supported_boards.md](docs/supported_boards.md).
Добавление новой платы: [Board Porting Guide](zephcore/boards/example_board/README.md).

## Самостоятельная сборка

Проще всего использовать [dev container](.devcontainer/README.md). При ручной установке нужны
[Zephyr SDK и west](https://docs.zephyrproject.org/latest/develop/getting_started/index.html):

```bash
# один раз
west init -l zephcore
west update

# companion и repeater
west build -b wio_tracker_l1 zephcore --pristine
west build -b rak4631 zephcore --pristine -- \
  -DEXTRA_CONF_FILE="boards/common/repeater.conf"

west flash
```

| Fragment | Роль/возможность |
|---|---|
| без fragment | Companion |
| `repeater.conf` | Repeater |
| `room_server.conf` | Room server |
| `observer.conf` | Observer для ESP32 |
| `kiss_modem.conf` | KISS modem |
| `debug.conf` | Логи и assertions |
| `packet_logging.conf` | Отдельные RAW/RX/TX строки пакетов |

ESP32 sysbuild/MCUboot, дополнительные варианты и параметры описаны в
[docs/BUILDING.md](docs/BUILDING.md).

## Техническая документация

| Документ | Содержание |
|---|---|
| [SHADOW_FEATURES.md](docs/SHADOW_FEATURES.md) | Полный список наших доработок и аппаратных проверок |
| [COMMANDS.MD](docs/COMMANDS.MD) | Фактическое дерево USB/BLE/LoRa CLI |
| [docs/BUILD_MATRIX.md](docs/BUILD_MATRIX.md) | Проверенные профили и команды сборки |
| [docs/DESIGN.md](docs/DESIGN.md) | Инварианты, слои и правила проекта |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Архитектура компонентов |
| [docs/CLI_commands.md](docs/CLI_commands.md) | Описание CLI-команд |
| [docs/BUILDING.md](docs/BUILDING.md) | Все варианты сборки |
| [docs/OFFGRID.md](docs/OFFGRID.md) | Off-grid режим и частоты |
| [docs/supported_boards.md](docs/supported_boards.md) | Платы и build-target |
| [docs/LINUX_NATIVE.md](docs/LINUX_NATIVE.md) | Запуск на Raspberry Pi/Femtofox |
| [releasenotes/](releasenotes/) | История релизов |

## Об использовании AI

Большая часть кода авторской ZephCore и этой ветки создавалась с помощью AI. Решения о включении
изменений принимает человек; критичные функции проверяются host-тестами и, где указано,
на реальном оборудовании. Успешная сборка не считается аппаратной проверкой.

[![TWONKS: AI Design Logic](img/twonks-ai-design.jpg)](https://www.twonks.co.uk/)

*Комикс [TWONKS](https://www.twonks.co.uk/).*

## Лицензия

MIT, как у авторской ZephCore и MeshCore. См. [LICENSE](LICENSE), где также перечислены
встроенные компоненты и их лицензии. Логотип: [recrof](https://github.com/recrof),
[WTFPL](https://en.wikipedia.org/wiki/WTFPL).
