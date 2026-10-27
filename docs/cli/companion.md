# Команды CLI

Раздел Companion ниже соответствует локальному USB CLI; BLE CLI показывает команды по категориям. На WiFi companion USB `help` содержит строку `wifi (help wifi)`, а `help wifi` выводит ту же отдельную группу WiFi, что BLE CLI. Для каждой роли приведён самостоятельный список, включая общие команды.

`get/set name` означает две команды: `get name` — прочитать, `set name <значение>` — изменить. Символ `|` разделяет варианты; угловые скобки обозначают аргумент и не вводятся. Например: `set fall.sens 3`.

Списки объединяют варианты плат: GPS, буззер, дисплей, Fall, OTA, DFU и uplink доступны только при поддержке оборудования и сборки. В Companion командная строка доступна через USB и BLE; vContact служит только для уведомлений. Удалённый LoRa CLI компаньена доступен контакту с разрешением remote CLI, поддерживает категории `help` и рабочие команды ZephCore. Секретный ключ, OTA/DFU и erase остаются локальными. По USB репиттер выводит полный список команд одной командой `help`; удалённый CLI репиттера выводит категории по `help` и их содержимое по `help <group>`. Доступность команд дополнительно ограничивается правами и возможностями сборки. Секретные ключи и пароли не публикуйте.

Текст, отправленный в vContact, не выполняется как команда. Устройство отвечает
`Please use the Command Line in Settings. This chat is for notifications only.`;
повтор сообщения с тем же timestamp не ставит этот ответ в очередь второй раз.
`get/set v.contact` управляет наличием контакта уведомлений, а не возвращает в
него командный обработчик.

На XIAO nRF52840 `shutdown y` переводит плату в System OFF и не оставляет пробуждение кнопкой D0; запуск происходит при новом подключении USB VBUS (или аппаратном RESET). Если USB уже подключён, его нужно отключить и подключить снова. Падение и последующий рост напряжения солнечной панели электрически равнозначны переподключению USB.

У репитера `get prv.key` и `set freq` выполняются только локально; поэтому удалённый help их не предлагает. `stats-packets`, `stats-radio` и `stats-core` доступны и удалённому администратору.

## Companion

### BLE CLI: Companion

На сборке без WiFi `help` выводит `Groups: id | rf | cad | mesh | gps | extra | ui | power | stat | sys`. На WiFi companion между `gps` и `extra` добавляется `wifi`; команды группы выводятся по `help <group>`:

```text
help
├─ id
│  ├─ get role
│  ├─ get/set name
│  ├─ get/set owner.info
│  ├─ get/set prv.key
│  └─ get public.key
├─ rf
│  ├─ get/set freq
│  ├─ get/set radio
│  ├─ get/set tx
│  ├─ get/set af
│  ├─ get/set path.hash.mode
│  ├─ get/set dutycycle
│  ├─ get/set extra.sf
│  ├─ get/set int.thresh
│  ├─ get/set radio.rxgain
│  └─ get/set radio.fem.rxgain
├─ cad
│  ├─ get cad
│  ├─ set cad.auto
│  ├─ set cad.offset
│  ├─ set probe.interval
│  ├─ set cad.busycap
│  └─ set cad.reset
├─ mesh
│  ├─ advert
│  ├─ advert.zerohop
│  ├─ get/set advert.interval
│  ├─ get/set meshtimesync
│  ├─ get/set flood.advert.interval
│  ├─ get/set flood.max
│  ├─ get/set flood.max.advert
│  └─ get/set flood.max.unscoped
├─ gps
│  ├─ gps on|off|setloc|advert
│  ├─ get/set gps duty
│  ├─ get/set lat
│  └─ get/set lon
├─ wifi                         (только WiFi companion)
│  ├─ get wifi.status
│  ├─ get wifi.ip
│  ├─ get wifi.ssid
│  ├─ set wifi.ssid <name>
│  ├─ get wifi.pwd
│  ├─ set wifi.pwd <password>
│  ├─ get wifi.enabled
│  ├─ set wifi.enabled 0|1
│  └─ set wifi.clear
├─ extra
│  ├─ findme
│  ├─ sos
│  ├─ tracking on|off
│  ├─ get/set tracking.group
│  ├─ get/set tracking.interval
│  ├─ fall on|off
│  ├─ get/set fall.sens (1-5)
│  ├─ get/set v.contact
│  └─ offgrid on|off
├─ ui
│  ├─ buzz on|off
│  ├─ leds on|off
│  ├─ get/set leds.hb
│  └─ get/set leds.radio
├─ power
│  ├─ get/set adc.multiplier
│  ├─ get/set autoshutdown
│  ├─ get/set autoshutdown.emergency
│  └─ get/set v.batteryalert
├─ stat
│  ├─ stats-packets
│  ├─ stats-radio
│  ├─ stats-core
│  └─ clear stats
└─ sys
   ├─ ver
   ├─ board
   ├─ uptime
   ├─ tz
   ├─ clock sync
   ├─ time <epoch>
   ├─ get dc.restarts
   ├─ get bootloader.ver
   ├─ get/set tz.offset
   ├─ start ota
   ├─ start dfu
   ├─ reboot
   ├─ shutdown y
   └─ erase
```

Команды Fall в BLE help показываются только на T1000-E и ThinkNode M3; `display.rotate` и `input.rotate` добавляются в `ui` на сборках с дисплеем.

### Remote LoRa CLI: Companion

Ответы `help <group>` помещаются в один remote CLI frame. `sos` и
`tracking on|off` выполняются без звука: для SOS это относится ко всему сценарию
до финальной передачи, для Tracking — ко всему сеансу и удалённому выключению.
Категорийное дерево удалённого CLI:

```text
help
├─ id
│  ├─ get role
│  ├─ get/set name
│  ├─ get/set owner.info
│  └─ get public.key
├─ rf
│  ├─ get freq | radio | tx
│  ├─ get/set af | path.hash.mode | dutycycle
│  └─ get/set extra.sf | int.thresh | radio.rxgain | radio.fem.rxgain
├─ cad
│  ├─ get cad
│  └─ set cad.auto | cad.offset | probe.interval | cad.busycap | cad.reset
├─ mesh
│  ├─ advert | advert.zerohop
│  ├─ get/set advert.interval | meshtimesync
│  └─ get/set flood.advert.interval | flood.max | flood.max.advert | flood.max.unscoped
├─ gps
│  ├─ gps on|off|setloc|advert
│  └─ get/set gps duty | lat | lon
├─ wifi
│  ├─ get wifi.status | wifi.ip | wifi.enabled
│  └─ set wifi.enabled 0|1
├─ extra
│  ├─ findme | sos | tracking on|off
│  ├─ get/set tracking.group | tracking.interval
│  ├─ fall on|off | get/set fall.sens (1-5)
│  └─ get/set v.contact
├─ ui
│  ├─ buzz on|off | leds on|off
│  ├─ get/set display.rotate | input.rotate
│  └─ get/set leds.hb | leds.radio
├─ power
│  ├─ get adc.multiplier | autoshutdown
│  └─ get/set autoshutdown.emergency | v.batteryalert
├─ stat
│  └─ stats-packets | stats-radio | stats-core
└─ sys
   ├─ ver | board | uptime | tz | clock sync | time <epoch>
   ├─ get dc.restarts | bootloader.ver | get/set tz.offset
   └─ reboot
```

`wifi`, Fall и display/input rotation показываются только при наличии соответствующей возможности сборки. Удалённо не выполняются `get/set prv.key`, `get/set wifi.pwd`, `get/set wifi.ssid`, `set wifi.clear`, `set pin`, `set freq`, `set radio`, `set tx`, `set adc.multiplier`, `set autoshutdown`, `offgrid on|off`, `clear stats`, `shutdown`, `poweroff`, OTA/DFU, `erase` и `clkreboot`.

### Команды и описание

- `ver` — выводит версию прошивки.
- `board` — выводит модель платы.
- `uptime` — выводит время работы после загрузки.
- `tz` — выводит текущий часовой пояс экрана в формате `GMT+5`, `GMT+5:30`, `GMT-3:30` или `GMT+0`; доступна через USB CLI, BLE/app CLI и удалённый LoRa CLI.
- `advert` — отправляет объявление узла с распространением по сети.
- `advert.zerohop` — отправляет объявление только ближайшим узлам, без ретрансляции.
- `clock sync` — синхронизирует часы по метке времени отправителя команды; перевод назад запрещён. Для локального задания времени используйте time.
- `time <epoch>` — задаёт время Unix в секундах (UTC); обработчик не разрешает перевод назад.
- `findme` — запускает звуковой поиск устройства; нужен поддерживаемый буззер.
- `sos` — запускает SOS; при ожидании GPS отправляет предварительные сообщения, затем основное. При buzzer off SOS беззвучный. Запуск через удалённый LoRa CLI всегда беззвучный, включая подтверждение, промежуточные и финальную отправки. На T-1000E и M3 одно короткое нажатие во время ожидания SOS отменяет сценарий, воспроизводит сигнал отмены Fall, восстанавливает состояние GPS и отправляет в `#sos` сообщение `❌ SOS Canceled`.
- `gps on|off|setloc|advert` — включает/выключает GPS; setloc сохраняет текущие координаты; advert none|share|prefs выбирает координаты в объявлении (без координат / текущие / сохранённые).
- `get wifi.status` — выводит состояние подключения WiFi: `connected` или `disconnected`.
- `get wifi.ip` — выводит текущий IPv4-адрес или сообщает, что подключения нет.
- `get wifi.ssid` — выводит сохранённый SSID.
- `set wifi.ssid <name>` — сохраняет SSID; применяется после перезагрузки.
- `get wifi.pwd` — выводит сохранённый пароль WiFi; не публикуйте этот ответ.
- `set wifi.pwd <password>` — сохраняет пароль WiFi; применяется после перезагрузки.
- `get wifi.enabled` — выводит сохранённое состояние WiFi: `0` или `1`.
- `set wifi.enabled 0|1` — сохраняет выключенное/включённое состояние WiFi; применяется после перезагрузки.
- `set wifi.clear` — удаляет сохранённые SSID и пароль; применяется после перезагрузки.
- `tracking on|off` — включает/выключает периодическую отправку координат. При включении через удалённый LoRa CLI запуск, остановка и все периодические отправки выполняются без звука.
- `fall on|off` — включает/выключает детекцию падения и мониторинг акселерометра (M3, T-1000E). Падение звучит даже при buzzer off.
- `leds on|off` — включает/выключает программную LED-индикацию; зарядная индикация зависит от платы.
- `buzz on|off` — включает/выключает обычные звуковые уведомления; исключение — тревога падения.
- `offgrid on|off` — включает/выключает пересылку пакетов компаньеном.
- `stats-packets` — выводит статистику пакетов.
- `stats-radio` — выводит статистику радио.
- `stats-core` — выводит системную статистику.
- `clear stats` — сбрасывает статистику.
- `get role` — выводит роль устройства.
- `get public.key` — выводит открытый ключ узла.
- `get dc.restarts` — выводит число перезапусков при тайм-ауте RX duty cycle.
- `get cad` — выводит параметры и статистику определения занятости эфира (CAD).
- `get txdelay` — выводит состояние адаптивной задержки передачи и текущую оценку занятости.
- `get pwr [<n>]` — выводит строку диагностики питания; только сборки с `CONFIG_ZEPHCORE_POWER_DIAG`.
- `get bootloader.ver` — выводит версию загрузчика, если поддерживается платой.
- `set cad.auto` — включает/выключает автоматическую настройку CAD: on|off|default.
- `set cad.offset` — задаёт смещение порога CAD: -8..12 или default.
- `set probe.interval` — задаёт интервал измерений радио в секундах: 10..255; 0 отключает периодические пробы.
- `set cad.busycap` — задаёт порог доли проб с обнаруженным слабым сигналом: 10..90%; 0 отключает этот предел.
- `set cad.reset` — сбрасывает статистику проб и возвращает смещение CAD к значению по умолчанию.
- `get/set adc.multiplier` — читает/задаёт коэффициент измерения батареи; 0 выбирает штатную калибровку платы.
- `set adc.multiplier target <mV>` — калибрует коэффициент по измеренному мультиметром напряжению 3000–4400 мВ.
- `set adc.multiplier full` — калибрует коэффициент по верхней точке батарейной кривой; применять только при полном заряде.
- `get/set advert.interval` — читает/задаёт интервал локальных объявлений в минутах (шаг хранения — 2 минуты).
- `get/set af` — читает/задаёт коэффициент паузы после передачи; связан с dutycycle формулой 100/(af+1).
- `get/set autoshutdown` — читает/задаёт порог автоматического выключения в мВ; 0 отключает. Только сборки с поддержкой автовыключения.
- `get/set autoshutdown.emergency` — читает/задаёт on|off для аварийного сообщения перед автоматическим выключением.
- `get/set buzzer off|on|vibrate|sound` — читает/задаёт режим уведомлений: выключено, звук и вибрация, только вибрация, только звук; зависит от оборудования.
- `get/set display.rotate` — читает/задаёт разворот дисплея на 180°; только поддерживаемые дисплеи.
- `get/set dutycycle` — читает/задаёт ограничение доли времени передачи: 1..100%.
- `get/set extra.sf` — читает/задаёт дополнительные SF для одновременного приёма на LR2021; не для SX1262.
- `get/set fall.sens (1-5)` — читает/задаёт чувствительность падения: 1 — максимальная, 3 — обычная, 5 — минимальная (M3, T-1000E).
- `get/set flood.advert.interval` — читает/задаёт интервал сетевых объявлений в часах.
- `get/set flood.max` — читает/задаёт предел числа переходов для flood-пакетов.
- `get/set flood.max.advert` — читает/задаёт предел переходов для объявлений.
- `get/set flood.max.unscoped` — читает/задаёт предел переходов для flood-пакетов без региона.
- `get/set freq` — читает/задаёт частоту LoRa.
- `get/set gps duty` — читает/задаёт интервал GPS в секундах; 0 — постоянная работа.
- `get/set input.rotate` — читает/задаёт инверсию направлений джойстика/кнопок навигации; только устройства с дисплеем.
- `get/set int.thresh` — читает/задаёт порог обработки помех; set не поддерживается на companion, хотя присутствует в его help.
- `get/set lat` — читает/задаёт сохранённую широту в десятичных градусах.
- `get/set leds.hb` — читает/задаёт режим heartbeat: all|hb|unread|off.
- `get/set leds.radio` — читает/задаёт режим индикатора радио: tx|rx|all|off.
- `get/set lon` — читает/задаёт сохранённую долготу в десятичных градусах.
- `get/set meshtimesync` — читает состояние/включает или выключает синхронизацию времени по mesh.
- `get/set name` — читает/задаёт имя узла.
- `get/set output.power 22|28|default (nominal dBm)` — выбирает номинальную выходную мощность Heltec V4.3: 22 → SX1262 10 dBm, 28 → SX1262 22 dBm. Это не измеренная мощность; default возвращает штатный TX.
- `get/set owner.info` — читает/задаёт текстовую информацию о владельце.
- `get/set path.hash.mode` — читает/задаёт формат хешей узлов в маршруте: 0..2.
- `get/set prv.key` — читает/задаёт секретный ключ identity. Не публикуйте ответ; изменение меняет идентичность узла.
- `get/set radio` — читает/задаёт параметры LoRa: частоту, полосу, SF и CR.
- `get/set radio.fem.rxgain` — читает/задаёт усиление внешнего приёмного усилителя; нужен поддерживаемый FEM.
- `get/set radio.rxgain` — читает/задаёт усиленный приём: 0|1.
- `get/set tracking.group` — читает/задаёт группу для отправки координат.
- `get/set tracking.interval` — читает/задаёт интервал отправки координат в минутах.
- `get/set tx` — читает/задаёт мощность самого радиочипа в dBm, в пределах платы.
- `get/set tz.offset (hours)` — читает/задаёт часовой пояс экрана: целые часы -12..14; не меняет UTC и сетевые метки.
- `get/set v.batteryalert` — читает/задаёт порог уведомления о низком заряде в vContact, в мВ; 0 отключает, default возвращает штатный порог.
- `get/set v.contact` — читает/задаёт доступность виртуального контакта уведомлений; обработчика команд в этом чате нет.
- `start ota` — запускает поддерживаемый платой режим OTA-обновления.
- `start dfu` — переводит устройство в режим обновления загрузчика (DFU); способ подключения зависит от платы.
- `stop ota` — останавливает OTA, если это поддерживается сборкой.
- `reboot` — перезагружает устройство.
- `shutdown y` — выключает устройство при поддержке платой; y подтверждает действие.
- `erase` — стирает файловое хранилище и перезагружает устройство. Опасная команда: удаляет сохранённые данные; доступна локально.
- `help` — выводит список доступных команд; ? — синоним в общем CLI.
