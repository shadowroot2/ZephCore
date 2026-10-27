# Обновление ZephCore с сохранением доработок ShadoW

Порядок переноса текущего форка на новый релиз автора. Обновление не считается
завершённым, пока пройдены все пункты этого файла, `docs/SHADOW_FEATURES.md` сверён
построчно, а результаты записаны в `SHADOW_CONTEXT.md`.

Источники истины:

- `docs/SHADOW_FEATURES.md` — полный перечень нашего текущего поведения;
- `SHADOW_CONTEXT.md` — аппаратные проверки, принятые решения и ограничения;
- `docs/COMMANDS.MD` — фактическое дерево и описание CLI;
- `docs/DESIGN.md`, `docs/ARCHITECTURE.md`, `docs/adr/` — инварианты проекта;
- `docs/AUTHOR_FIRST_OVERLAY.md` и `author-first-overlay.json` — опыт предыдущего
  author-first переноса.

## 1. Подготовка

1. Обновить `dev`, сохранить SHA текущего рабочего состояния и список
   незакоммиченных файлов. Чужие изменения не удалять и не перезаписывать.
2. Зафиксировать новый авторский tag/SHA, release notes, изменения Zephyr/SDK,
   board manifests, Kconfig, devicetree и форматов хранения.
3. Создать ветку `codex/update-<version>` от `dev`. Цель — `dev`; переносить
   историю rebase, не merge.
4. Сохранить доступ к предыдущей рабочей версии: ветка/tag и последние
   аппаратно проверенные образы. Не использовать старые бинарники как
   доказательство работы нового исходного дерева.
5. Снять три diff:
   - старый автор → новый автор;
   - старый автор → текущий `dev`;
   - новый автор → рабочая ветка обновления.
6. Составить per-file checklist. Для каждого нашего файла определить: автор
   уже реализовал функцию, нужен перенос, нужна адаптация или код можно удалить.
7. Прочитать затрагиваемые разделы `docs/DESIGN.md`, `docs/ARCHITECTURE.md` и
   соответствующие ADR до изменения кода.

## 2. Правила интеграции

1. Основа всегда новая версия автора. Не копировать старое дерево целиком.
2. Сначала принять авторские изменения, затем накладывать минимальный ShadoW
   overlay по функциям из этого документа.
3. Если автор уже исправил проблему, использовать его реализацию. Наш код
   оставлять только при сохранении поведения или большей точности без потери
   совместимости.
4. Не возвращать старые radio drivers, SDK headers, Zephyr patches и широкие
   platform workarounds поверх новой авторской версии.
5. Файлы, портированные из MeshCore и перечисленные в `.editorconfig`, менять
   только внутри блоков `// ZEPHCORE:`; авторский текст не форматировать.
6. В `zephcore/patches/zephyr` один patch владеет одним Zephyr-файлом.
7. Поведение общее для всех плат переносить в общий слой. Условия строить по
   DT/Kconfig capability, не по имени платы или `CONFIG_SOC_*`.
8. После rebase перечитать весь diff относительно `dev`: конфликтов может не
   быть, но старая ветка способна молча отменить новый код вокруг неё.

## 3. Сначала сохранить форматы и инварианты

Эти пункты выполняются до UI и новых функций: ошибка здесь может стереть
identity, pairing или настройки пользователя.

- [ ] Совместимость companion protocol и эфирного формата с Arduino MeshCore.
- [ ] `prefs.json` остаётся единственным развиваемым форматом настроек.
- [ ] Не менять размер/offset legacy `PrefsCodec`; pinned tests не править под
      новый результат.
- [ ] Сохранить авторский префикс prefs и версионированный хвост `SHPF` в той
      же атомарной записи.
- [ ] Сохранить чтение ранее выпущенных custom companion/repeater layouts.
- [ ] Bridge и repeater battery prefs остаются отдельными совместимыми файлами.
- [ ] Обычное обновление той же роли сохраняет identity, настройки, контакты и
      BLE pairing.
- [ ] Авторское стирание данных при смене роли сохранить. Перед
      companion ↔ repeater требуется экспорт identity и важных настроек.
- [ ] Стенные часы никогда не переводятся назад.
- [ ] Mesh, GPS и UI меняются только в main thread; циклы остаются event-driven.
- [ ] Не добавлять heap в packet path.
- [ ] Не обращаться к периферии до её успешного определения.
- [ ] Не возвращать не-криптографический RNG fallback для RP2040.

## 4. BLE, transport и безопасность

- [ ] Companion: не отправлять proactive SMP Security Request.
- [ ] Сохранить `BT_LE_ADV_OPT_USE_IDENTITY` и штатные connection parameters.
- [ ] nRF/MG24 и ESP32 privacy/security оставить согласно действующим ADR.
- [ ] Unpaired BLE connection освобождает единственный слот через 15 секунд.
- [ ] Обычный companion не получает ослабленную bridge-политику безопасности.
- [ ] Legacy SMP/L2 encryption/bonding разрешены только repeater-bridge.
- [ ] BLE TX queue: 8 слотов, 2 control reserve, один in-flight, timeout 8 с,
      generation guard против старых callbacks.
- [ ] Heltec V3 companion использует CP2102/UART0 для companion transport/CLI;
      console/shell и USB-Serial/JTAG не должны перехватывать UART callbacks.
- [ ] USB, BLE и TCP transport корректно отражаются на экране; Bluetooth
      показывает `Connected` только для BLE.

## 5. Общие аппаратные слои

Переносить до функций, которые от них зависят.

- [ ] Battery curves, ADC scaling, external-power detection и charge state.
- [ ] GPS power/reset/UART, fresh-fix semantics и число видимых спутников.
- [ ] Environmental sensors и запрет ложного MCU-temperature fallback там, где
      заявлен внешний датчик.
- [ ] LED ownership: heartbeat, unread, LoRa TX и зарядка не перебивают друг
      друга.
- [ ] Провайдеры VBUS и процента батареи устанавливаются до `ui_init()`.
- [ ] Buzzer/haptic modes `off|on|vibrate|sound` и forced Fall alarm.
- [ ] Репиттеры по умолчанию используют GPS duty 43200 секунд.
- [ ] Off-grid переключает на допустимую частоту своего диапазона и возвращает
      прежнюю; forwarding на обычной mesh-частоте запрещён.
- [ ] GPS time принимается только после подтверждённого satellite fix; старое
      время не двигает часы назад.

## 6. Companion: функциональный overlay

### SOS

- [ ] Канал `#sos`, сообщения ожидания раз в 30 секунд, timeout GPS 5 минут.
- [ ] Fresh fix не старше 30 секунд; без GPS финальное сообщение уходит сразу.
- [ ] Сохраняются и восстанавливаются исходные GPS enabled/duty state.
- [ ] Повторный SOS перезапускает активный сценарий.
- [ ] Waiting и финальное сообщения зеркалируются в локальную очередь группы.
- [ ] Локальный/UI SOS соблюдает пользовательский mute; финальная мелодия
      привязана к фактическому TX complete.
- [ ] Удалённый LoRa CLI `sos` полностью беззвучен: подтверждение, повторные
      waiting-этапы и финальная передача. Флаг remote origin хранится в
      асинхронном SOS state до завершения.
- [ ] На headless T-1000E/M3 один tap при ожидающем ручном SOS отменяет его,
      возвращает GPS state, принудительно воспроизводит сигнал отмены Fall,
      отправляет `❌ SOS Canceled` в `#sos` и не запускает Advert. Fall этим
      путём не отменяется.
- [ ] На устройствах с экраном ENTER на странице SOS запускает SOS, а во время
      ожидания GPS отменяет его; экран меняет действие на `Press to Cancel`.

### Fall — только capability соответствующего акселерометра

- [ ] M3/SC7A20H и T-1000E/QMA6100P сохраняют отдельные профили.
- [ ] Цепочка low-g → impact → pause → stillness; опрос 20 мс, cooldown 60 с.
- [ ] `fall on|off`, `get/set fall.sens 1..5`; 1 — наиболее чувствительный.
- [ ] `fall off` останавливает мониторинг, но не общий sensor rail.
- [ ] Fall использует общий GPS/SOS pipeline с отдельными текстами.
- [ ] Forced alarm повторяется раз в 30 секунд даже при mute и после финального
      сообщения, пока пользователь физически не подтвердит отмену.
- [ ] Первое физическое подтверждение останавливает alarm/messages, возвращает
      GPS state, отправляет `Fall canceled...` и даёт отдельный сигнал.
- [ ] Fall и ручной SOS заменяют друг друга, а не работают параллельно.
- [ ] Remote silent SOS не ослабляет forced Fall alarm.

### Tracking

- [ ] `tracking on|off`, `tracking.group`, `tracking.interval`, нормализация `#`.
- [ ] Координаты отправляются только при fresh fix и достаточном перемещении;
      исходящие сообщения зеркалируются локально.
- [ ] GPS state/duty восстанавливаются после выключения.
- [ ] Таймер 64-битный и обслуживается независимо от потока RX/BLE events.
- [ ] Tracking, включённый удалённым LoRa CLI, хранит silent flag весь сеанс:
      включение и все периодические отправки без звука. Remote `tracking off`
      также беззвучен.
- [ ] USB CLI, BLE/app CLI и UI сохраняют обычные звуки.

### vContact и питание

- [ ] vContact используется только для уведомлений; входящая команда отвечает:
      `Please use the Command Line in Settings. This chat is for notifications only.`
      Повтор одного timestamp не должен ставить этот ответ в очередь второй раз.
- [ ] Уведомления о GPS, buzzer, LEDs, BLE, Offgrid, Tracking, Wake on message,
      display/input rotation и ручном выключении сохраняются.
- [ ] SOS/Advert не создают статусное уведомление только из-за запуска.
- [ ] `autoshutdown`, `v.batteryalert`, `autoshutdown.emergency` сохраняются.
- [ ] Перед low-battery shutdown опционально отправляется сообщение в
      `#zephcore`; M1 держит экран предупреждения в течение grace period.
- [ ] Выключение кнопкой отправляет vContact notice при подключённом приложении.

## 7. Repeater и repeater-bridge

### Repeater battery alert

- [ ] Общая реализация для repeater и repeater-bridge.
- [ ] `battery`, alert/threshold/interval/group, три последовательных низких
      замера, внешний источник сбрасывает подтверждение.
- [ ] Текст содержит имя, процент и напряжение; GPS берётся из последнего fix
      или сохранённой позиции без принудительного включения GPS.
- [ ] Cooldown сохраняется; нехватка packet slot повторяет попытку через 30 с.
- [ ] Автоматическое выключение репиттера не добавляется.

### Bridge

- [ ] Роль `repeater-bridge` остаётся обычным repeater плюс ESP-NOW/BLE bridge.
- [ ] Через мост идут только flood group text/data и adverts. Direct, ACK,
      admin и remote CLI не пересылаются между диапазонами.
- [ ] Packet hash не зависит от изменяемого route; cache 128/10 минут, pending
      queue 4, priority 0..7 и отмена дубликата до отложенной пересылки.
- [ ] `Forwarded` и `Skipped` сохраняют текущую семантику.
- [ ] Central/peripheral выбираются детерминированно по identity address.
- [ ] Ready означает security + GATT discovery + CCC + HELLO/READY/ACK, не
      просто BLE ACL.
- [ ] CCC value handle, retry, phase timeouts, health ping, watchdog и reconnect
      переносятся вместе.
- [ ] Обычный RF disconnect не стирает bond; unpair только при подтверждённой
      auth/key ошибке, ручной команде или смене peer/key.
- [ ] `bridge`, `bridge ping`, `bridge logs`, `bridge bonds`, `bridge unpair`,
      MAC/peer/priority/type/key CLI и ограниченные remote replies сохранены.
- [ ] GATT TX остаётся вне main loop; потерянный callback не исчерпывает пул.
- [ ] Проверить ESP-NOW и BLE отдельно на двух физических платах и длительный
      reconnect, не ограничиваться успешным `bridge ping`.

### XIAO repeater

- [ ] Обычная repeater-сборка без BLE.
- [ ] RX watchdog реагирует на невозможность вернуться в RX, а не на тишину в
      эфире; сохраняются диагностика/recovery/reboot bounds.
- [ ] `shutdown y`: System OFF, пробуждение новым USB VBUS или RESET, не D0.
- [ ] HICHG 100 мА, правильная полярность `/CHG`, телеметрия показывает
      номинальный лимит, не измеренный ток.

## 8. CLI переносится как единая функция

При любой новой или изменённой команде одновременно обновлять:

1. обработчик;
2. локальный USB CLI;
3. BLE/app CLI;
4. remote CLI, если команда безопасна;
5. `docs/COMMANDS.MD` и `docs/CLI_commands.md`;
6. host tests;
7. release notes.

### Companion

- [ ] USB выводит полный локальный список; BLE/app и remote LoRa CLI имеют
      категории: `id | rf | cad | mesh | gps | wifi | extra | ui | power |
      stat | sys`. `wifi` условна capability.
- [ ] `id`: `role`, `name`, `owner.info`, private/public keys; remote private
      key не показывает.
- [ ] `rf`: freq/radio/tx первыми, затем af/path hash/duty/extra SF/int
      threshold/RX gains.
- [ ] Flood settings находятся в конце `mesh`.
- [ ] `extra`: findme, SOS, Tracking, conditional Fall, vContact; offgrid
      остаётся только локально.
- [ ] `ui`: короткие `buzz on|off` выше `leds on|off`; старые `set leds` и
      `set buzzer` пользователю не показываются. Rotate только при display.
- [ ] `stat` расположен перед `sys`; remote `clear stats` запрещён.
- [ ] `sys`: OTA/DFU выше reboot в локальном CLI; `clkreboot` у companion
      отсутствует.
- [ ] `tz` сразу после `uptime`, формат `GMT±H[:MM]`.
- [ ] Companion не показывает `multi.acks` и `gps diag`.
- [ ] Remote-only denylist сохраняет secrets, SSID/password, PIN, RF writes,
      ADC/autoshutdown writes, offgrid, stats clear, shutdown, OTA/DFU, erase.

### Repeater

- [ ] USB `help`/`?` печатает полный локальный список.
- [ ] Remote root order: `id | rf | cad | mesh | find | region | access | gps |
      ui | power | sensor | stat | log | sys`, затем conditional
      `bridge | bridge.cfg | uplink`.
- [ ] `password` и `guest.password` находятся в `id`; `access` содержит
      `setperm`.
- [ ] Radio commands находятся в конце `rf`; отдельной `radio` группы нет.
- [ ] Flood commands находятся в конце `mesh`; отдельной `flood` группы нет.
- [ ] Neighbor commands находятся в `find`; `help mesh.2` отсутствует.
- [ ] Battery commands находятся в `power`/`power.2`; отдельной `battery`
      группы нет.
- [ ] `log` после `stat`; `shutdown y` последний в `sys`.

### Ограничения frame

- [ ] После каждого изменения help показать пользователю фактический список
      или полное дерево так, как оно выглядит на устройстве.
- [ ] Проверить длину каждой категории для всех capability-вариантов.
- [ ] BLE `MAX_FRAME_SIZE` 176: один байт type и возможный трёхбайтовый CLI
      prefix; для help доступно 172 байта текста.
- [ ] Tagged remote reply: не более 158 байт вместе с NUL.
- [ ] Ответ сначала формируется в полном local scratch buffer, затем безопасно
      ограничивается размером remote frame.
- [ ] Не оставлять compiler warning о намеренном truncation: bounded copy
      должен выражать ограничение без предупреждения.

## 9. Экраны и действия UI

- [ ] Обе заставки показывают ZephCore, `MeshCore on Zephyr`, версию, дату и
      `Tuned by ShadoW`: проверить button UI и joystick UI отдельно.
- [ ] Не потерять авторские страницы при восстановлении наших.
- [ ] Companion: Tracking, SOS, GPS, WiFi, Advert, Buzzer, LEDs, Offgrid, DFU,
      Shutdown и board-specific pages.
- [ ] GPS off/on callback передаёт enabled и фактический
      `OFF`/`ACQUIRING`/`STANDBY`, затем вызывает общий redraw; проверить
      button, joystick и E-Paper UI.
- [ ] На платах с DT alias `gps-switch` его положение определяет старт GPS;
      при OFF страница пишет `GPS switch: off`, а не `GPS: not detected`.
- [ ] Heltec V3 без GPS не показывает бесполезную GPS page.
- [ ] Advert не переводит пользователя на соседнюю страницу.
- [ ] WiFi page позволяет on/off, сохраняет prefs и делает cold reboot.
- [ ] WiFi endpoint одной строкой `IPv4:port`; `Port 5000` отдельно отсутствует.
- [ ] `Power OFF` виден до отключения; E-Ink сохраняет последний кадр.
- [ ] M5: E-Ink, expander, backlight timeout и подтверждение power-off.
- [ ] Bridge button UI order: status → RADIO → BRIDGE → BRG INFO → BUZZER
      (если есть) → LEDS → shutdown.
- [ ] Обычный repeater не получает bridge pages.

## 10. Кнопки, звук и LED

- [ ] Headless T-1000E/M3: один tap открывает 3-секундное SOS window; hold ≥1 с
      запускает SOS, иначе flood advert.
- [ ] При уже активном ручном SOS тот же один tap отменяет SOS и потребляется до
      запуска Advert/SOS window.
- [ ] T-1000E/M3: 2 taps Tracking, 3 buzzer, 4 LEDs, 5 GPS.
- [ ] Для M3 используется та же карта, что для T-1000E.
- [ ] Число коротких подтверждений соответствует числу нажатий.
- [ ] Экранные платы используют свою DT map; headless map не переносить на них.
- [ ] `findme` играет Morse `ZEPHCORE` и принудительно обходит mute.
- [ ] Fall forced; обычный SOS/UI соблюдают mute; remote SOS/Tracking silent.
- [ ] M3/T-1000E/XIAO charging bands: <50% — 1, 50–74% — 2, ≥75% — 3,
      full — постоянный LED; 150/200/1500 мс.
- [ ] XIAO при зарядке сохраняет один обычный heartbeat pulse, не три low-batt
      pulse; зелёный charge LED независим от красного HB и синего TX.
- [ ] M3/T-1000E/T-ECHO charge indication имеет приоритет над activity/TX.
- [ ] T-ECHO не получает процентные charge-series из-за завышенного ADC при USB.

## 11. Обязательная проверка плат

### ThinkNode M3

- [ ] AHT10 reset/calibration/measurement, −5 °C correction, humidity.
- [ ] ADC AIN3/P0.05, gain 1/4, multiplier 4200, curve top 4130 мВ.
- [ ] External power P0.31; GPS и accelerometer rails; SC7A20H Fall.

### T-1000E

- [ ] Авторский temperature/light driver и sensor rail остаются.
- [ ] Luminosity — реальный свет, не спутники; без MCU temperature fallback.
- [ ] EXT_PWR_DETECT, full 4190 мВ, QMA6100P clock/filter после reset.

### M1/M5/M6

- [ ] Видимые satellites до 32; telemetry mapping не применять к T-1000E.
- [ ] M1/M5 curve top 4100 мВ.
- [ ] M1 heartbeat: phone-connected отключает синий HB, low-battery остаётся.
- [ ] M5 ESP32-S3 crystal temperature не называется ambient.
- [ ] M6 GPS startup как M1, charge polarity и nominal panel `6 W` semantics.

### Heltec

- [ ] V3 battery multiplier 5420, low-battery sleep, MCU temperature,
      UART0 companion CLI и WiFi page.
- [ ] V4.3 `output.power 22|28|default` только на плате с соответствующим PA;
      не добавлять V3/Tracker V2.
- [ ] Heltec ESP32-S3 release — MCUboot sysbuild, full merged image с 0x0 и
      signed update payload с app offset.

### T-ECHO, XIAO и прочие

- [ ] T-ECHO curve top 4100, RGB active-low pins, backlight sensor SENSE.
- [ ] XIAO nRF curve top 4170; companion L76K variant не ломает Wio SX1262
      MOSI/RESET и button/WAKEUP sharing.
- [ ] RAK4631, T114, M6, M1, M3, T-1000E и Heltec V3 сохраняют manifests,
      роли и capability-based Kconfig/DT.
- [ ] Pico/Pico W, ProMicro/E22, M5, M9, Heltec T1, V4.3 и Tracker V2 проверить,
      если общая правка затрагивает их слой.

## 12. Документация и тесты до сборки

1. Обновить `docs/ARCHITECTURE.md` для поведения/interface/limit.
2. Обновить `docs/CLI_commands.md` и `docs/COMMANDS.MD` для любой CLI-правки.
3. Обновить `docs/SHADOW_FEATURES.md`, этот guide и release notes.
4. ADR нужен при новом или отменённом архитектурном решении.
5. Любая логика без hardware получает host test и запись в
   `tests/catalog.json`; pinned значения не подгонять.
6. Выполнить:

   ```sh
   python3 tests/run.py run --profile quick
   python3 zephcore/scripts/board_manifest.py check
   python3 zephcore/scripts/board_manifest.py docs
   git diff --check
   ```

7. Сравнить полный diff с `dev`, отдельно проверить CMake/Kconfig/DTS, prefs,
   main loops, CLI tables, screens и board configs.

## 13. Сборка

Перед любой сборкой сначала показать пользователю точные платы, роли, команды
и назначение сборки; запускать только после подтверждения.

Локальное окружение:

```sh
ZEPHYR_SDK_INSTALL_DIR=/Users/shadow/Work/CodeX/ZephCore/zephyr-sdk-1.0.1
CCACHE_DIR=/private/tmp/zephcore-ccache
./.venv/bin/west build ...
```

- При смене платы/роли использовать `--pristine`.
- Все локальные build trees хранить в `builds/`; каждой команде west передавать
  `-d builds/<board>-<role>`. Build-каталоги в корне не создавать.
- ESP32-S3/C-series release собирать с `--sysbuild`.
- Собрать каждую затронутую плату и роль плюс одну незатронутую.
- Если менялся companion common path, минимальная проверенная матрица:
  M1, M3, T-1000E, Heltec V3 companion.
- Если менялся repeater common path: XIAO nRF, M6, RAK4631 и Heltec V3
  repeater; при bridge-изменении дополнительно M6 и Heltec V3 repeater-bridge.
- Если менялись экраны: минимум одна button UI, одна joystick UI и одна E-Ink
  плата; отдельно проверить headless M3/T-1000E.
- Записать FLASH/RAM delta относительно базы. Для Heltec V3 контролировать
  критический DRAM headroom.

## 14. Имена и упаковка артефактов

- Постоянный формат: `<board>-<role>-<commit>.<ext>`.
- Пример: `t1000_e-companion-a4733b1e6.uf2`.
- Не добавлять дату, версию, `firmware` или произвольные суффиксы.
- Сохранять штатный board id и подчёркивания.
- nRF release: UF2. HEX не создавать без необходимости платы.
- ESP32-S3/C-series:
  - `<board>-<role>-<commit>-merged.bin` — MCUboot + signed app, прошивка с `0x0`;
  - `<board>-<role>-<commit>-update.bin` — подписанное app-only обновление.
- Не публиковать signed app как обычный bootable `.bin`.
- Для каждого файла сохранить SHA-256.

## 15. Аппаратная приёмка

Успешная компиляция и host tests не заменяют проверку устройства.

- [ ] Обновление поверх предыдущей версии сохраняет identity, prefs, contacts и
      pairing; PIN не сбрасывается.
- [ ] BLE reconnect Android/iOS, USB CLI, WiFi companion transport.
- [ ] Полное дерево help совпадает с `docs/COMMANDS.MD`; remote replies не режутся.
- [ ] SOS: fresh/stale/no GPS, timeout, repeat, TX-complete melody и remote silent.
- [ ] Tracking: movement/no movement, GPS restore и remote silent весь сеанс.
- [ ] Fall: оба датчика, forced alarm, physical cancel и восстановление GPS.
- [ ] Все button gestures, vContact notices и manual shutdown.
- [ ] Все страницы, `Tuned by ShadoW`, GPS redraw, WiFi toggle/reboot,
      `IPv4:port`, final E-Ink frame.
- [ ] Charge LEDs на M3/T-1000E/XIAO/T-ECHO и отсутствие конфликтов HB/TX.
- [ ] Repeater battery alert/cooldown и XIAO RX watchdog/System OFF.
- [ ] ESP-NOW bridge, BLE bridge pairing/reconnect/unpair/logs и длительный run.
- [ ] Записать в `SHADOW_CONTEXT.md`, что именно проверено пользователем и на
      каком образе. Не переносить аппаратное подтверждение старой сборки на новую.

## 16. Перед выпуском

- [ ] `git status` содержит только относящиеся к обновлению файлы.
- [ ] Нет случайного удаления наших плат, страниц, CLI или prefs migration.
- [ ] Нет возврата исключённых экспериментов: Love Edition и старый
      некриптографический RP2040 RNG fallback.
- [ ] Release notes разделяют изменения автора и наши изменения.
- [ ] Указаны собранные платы, реально проверенное железо и непроверенные места.
- [ ] Финальный diff после последнего rebase повторно прочитан целиком.
