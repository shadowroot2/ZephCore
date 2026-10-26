# Команды CLI

Список Companion ниже соответствует локальному USB CLI; BLE CLI показывает команды по категориям. На WiFi companion USB `help` содержит строку `wifi (help wifi)`, а `help wifi` выводит ту же отдельную группу WiFi, что BLE CLI. Для каждой роли приведён самостоятельный список, включая общие команды.

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
- `sos` — запускает SOS; при ожидании GPS отправляет предварительные сообщения, затем основное. При buzzer off SOS беззвучный. Запуск через удалённый LoRa CLI всегда беззвучный, включая подтверждение, промежуточные и финальную отправки.
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

## Repeater

`gps on|off|setloc|advert` и `get gps diag` отображаются в help только при наличии включённого GNSS-устройства в конфигурации платы.

### Remote CLI

```text
help
├─ id
│  ├─ get role
│  ├─ get/set name
│  ├─ get/set owner.info
│  ├─ set prv.key
│  ├─ get public.key
│  ├─ password <value>
│  └─ get/set guest.password
├─ rf
│  ├─ get freq
│  ├─ get/set radio
│  ├─ get/set tx
│  ├─ get/set af
│  ├─ get/set path.hash.mode
│  ├─ get/set dutycycle
│  ├─ get/set int.thresh
│  ├─ get/set radio.rxgain
│  ├─ get/set extra.sf
│  ├─ get freqerr
│  ├─ get/set radio.fem.rxgain
│  ├─ get/set rxduty
│  └─ tempradio <freq> <bw> <sf> <cr> <minutes>
├─ cad
│  ├─ get cad
│  ├─ get cad.stats
│  ├─ get cad.auto
│  ├─ get cad.offset
│  ├─ get probe.interval
│  ├─ get cad.busycap
│  ├─ get txdelay
│  └─ set cad.auto|offset|busycap|reset|probe.interval
├─ mesh
│  ├─ advert
│  ├─ advert.zerohop
│  ├─ get/set advert.interval
│  ├─ get/set repeat
│  ├─ get/set loop.detect
│  ├─ get/set meshtimesync
│  ├─ get/set multi.acks
│  ├─ get/set flood.advert.interval
│  ├─ get/set flood.max
│  ├─ get/set flood.max.advert
│  ├─ get/set flood.max.unscoped
│  └─ get/set backoff.multiplier
├─ find
│  ├─ neighbors
│  ├─ neighbor.remove <pubkey>
│  └─ discover.neighbors
├─ region
│  ├─ region
│  ├─ region get|put|remove|list
│  ├─ region def|load|save
│  └─ region allowf|denyf|home|default
├─ access
│  └─ setperm <permissions> <pubkey>
├─ gps
│  ├─ gps
│  ├─ gps on|off|sync|setloc|advert
│  ├─ get gps
│  ├─ get gps diag
│  ├─ get/set gps duty
│  ├─ get/set gps standby
│  ├─ get/set lat
│  └─ get/set lon
├─ ui
│  ├─ get/set leds
│  ├─ get/set leds.brightness
│  ├─ get/set leds.hb
│  ├─ get/set leds.radio
│  ├─ get/set display.rotate
│  ├─ get/set input.rotate
│  └─ get/set tz.offset
├─ power
│  ├─ power
│  ├─ powersaving on|off
│  ├─ get pwrmgt.support
│  ├─ get pwrmgt.source
│  ├─ get pwrmgt.bootreason
│  ├─ get pwrmgt.bootmv
│  ├─ battery
│  ├─ get/set adc.multiplier
│  ├─ get/set battery.alert
│  ├─ get/set battery.group
│  ├─ get/set battery.interval
│  └─ get/set battery.threshold
├─ sensor
│  ├─ sensor get <key>
│  ├─ sensor set <key> <value>
│  └─ sensor list <start>
├─ stat
│  ├─ stats-packets
│  ├─ stats-radio
│  ├─ stats-core
│  ├─ clear stats
│  ├─ get pm
│  ├─ get mem [<n>]
│  ├─ get pwr [<n>]
│  └─ get loop.wakes
├─ log
│  ├─ log start
│  ├─ log stop
│  └─ log erase
├─ sys
│  ├─ ver
│  ├─ board
│  ├─ uptime
│  ├─ clock
│  ├─ clock sync
│  ├─ time <epoch>
│  ├─ get dc.restarts
│  ├─ get bootloader.ver
│  ├─ start ota
│  ├─ start dfu
│  ├─ stop ota
│  ├─ reboot
│  ├─ clkreboot
│  └─ shutdown y
├─ bridge (только repeater-bridge)
│  ├─ bridge
│  ├─ bridge on|off|ping|logs
│  ├─ bridge unpair
│  ├─ bridge bonds
│  ├─ bridge logs N|clear
│  ├─ get bridge.delay
│  └─ get bridge.mac
├─ bridge.cfg (только repeater-bridge)
│  ├─ set bridge.key <32-hex>
│  ├─ get/set bridge.peer
│  ├─ get/set bridge.priority
│  └─ get/set bridge.type
└─ uplink (только Wi-Fi/MQTT uplink)
   ├─ get uplink.status
   ├─ get/set uplink.enable
   ├─ get/set uplink.mqtt.host|port|tls|user|iata
   ├─ set uplink.mqtt.password
   ├─ get/set uplink.wifi.ssid
   └─ set uplink.wifi.psk
```

`get prv.key`, `set freq`, `get acl`, `log` и `erase` остаются только в полном локальном USB help.

- `ver` — выводит версию прошивки.
- `board` — выводит модель платы.
- `battery (mV, %, uptime, alert settings)` — выводит напряжение, процент заряда, uptime и настройки уведомлений; вводить battery.
- `uptime` — выводит время работы после загрузки.
- `advert` — отправляет объявление узла с распространением по сети.
- `advert.zerohop` — отправляет объявление только ближайшим узлам, без ретрансляции.
- `clock sync` — синхронизирует часы по метке времени отправителя команды; перевод назад запрещён. Для локального задания времени используйте time.
- `time <epoch>` — задаёт время Unix в секундах (UTC); обработчик не разрешает перевод назад.
- `gps on|off|setloc|advert` — включает/выключает GPS; setloc сохраняет текущие координаты; advert none|share|prefs выбирает координаты в объявлении (без координат / текущие / сохранённые).
- `password <value>` — задаёт пароль административного доступа.
- `neighbors` — выводит список соседних узлов.
- `neighbor.remove <pubkey>` — удаляет соседа по открытому ключу.
- `discover.neighbors` — запускает поиск соседей.
- `region def|get|put|remove|list|load|save` — управляет регионами: определение, чтение, изменение, удаление, список, загрузка и сохранение.
- `region allowf|denyf|home|default` — управляет разрешением flood-пересылки и выбором домашнего/основного региона.
- `tempradio <freq> <bw> <sf> <cr> <minutes>` — временно меняет радиопараметры на заданное число минут.
- `setperm <permissions> <pubkey>` — задаёт права доступа узлу по открытому ключу.
- `stats-packets` — выводит статистику пакетов.
- `stats-radio` — выводит статистику радио.
- `stats-core` — выводит системную статистику.
- `clear stats` — сбрасывает статистику.
- `get role` — выводит фактическую роль устройства.
- `get public.key` — выводит открытый ключ.
- `get gps diag` — выводит диагностику GPS и результат настройки модуля.
- `get dc.restarts` — выводит число перезапусков при тайм-ауте RX duty cycle.
- `get cad` — выводит параметры и статистику определения занятости эфира (CAD).
- `get bootloader.ver` — выводит версию загрузчика, если поддерживается платой.
- `get uplink.status` — выводит состояние Wi-Fi/MQTT uplink; только сборки с uplink.
- `set cad.auto` — включает/выключает автоматическую настройку CAD: on|off|default.
- `set cad.offset` — задаёт смещение порога CAD: -8..12 или default.
- `set probe.interval` — задаёт интервал измерений радио в секундах: 10..255; 0 отключает периодические пробы.
- `set cad.busycap` — задаёт порог доли проб с обнаруженным слабым сигналом: 10..90%; 0 отключает этот предел.
- `set cad.reset` — сбрасывает статистику проб и возвращает смещение CAD к значению по умолчанию.
- `set uplink.mqtt.password <password>` — задаёт пароль MQTT uplink.
- `set uplink.wifi.psk <password>` — задаёт пароль Wi-Fi uplink.
- `get/set adc.multiplier` — читает/задаёт коэффициент измерения батареи; 0 выбирает штатную калибровку платы.
- `get/set advert.interval` — читает/задаёт интервал локальных объявлений в минутах (шаг хранения — 2 минуты).
- `get/set af` — читает/задаёт коэффициент паузы после передачи; связан с dutycycle формулой 100/(af+1).
- `get/set backoff.multiplier` — читает/задаёт множитель задержки при дубликатах; 0 отключает механизм.
- `get/set battery.alert on|off (default on)` — включает/выключает уведомления о низком заряде; по умолчанию on.
- `get/set battery.group <name> (default #zephcore)` — задаёт группу уведомлений; по умолчанию #zephcore.
- `get/set battery.interval <hours> (1..168, default 12)` — задаёт интервал повторных уведомлений в часах; по умолчанию 12.
- `get/set battery.threshold <mV> (0..5000, default: board; 0=no alerts)` — задаёт порог низкого заряда в мВ; по умолчанию 3250 на nRF52, 3500 на остальных платформах. 0 означает 0 мВ и фактически отключает уведомления.
- `get/set display.rotate` — читает/задаёт разворот дисплея на 180°; только поддерживаемые дисплеи.
- `get/set dutycycle` — читает/задаёт ограничение доли времени передачи: 1..100%.
- `get/set extra.sf` — читает/задаёт дополнительные SF для одновременного приёма на LR2021; не для SX1262.
- `get/set flood.advert.interval` — читает/задаёт интервал сетевых объявлений в часах.
- `get/set flood.max` — читает/задаёт предел числа переходов для flood-пакетов.
- `get/set flood.max.advert` — читает/задаёт предел переходов для объявлений.
- `get/set flood.max.unscoped` — читает/задаёт предел переходов для flood-пакетов без региона.
- `get/set freq` — читает/задаёт частоту LoRa.
- `get/set gps duty` — читает/задаёт интервал GPS в секундах; 0 — постоянная работа.
- `get/set guest.password` — читает/задаёт пароль гостевого доступа; пустой пароль допускает открытый гостевой вход.
- `get/set input.rotate` — читает/задаёт инверсию направлений джойстика/кнопок навигации.
- `get/set int.thresh` — читает/задаёт порог обработки помех; set не поддерживается на companion, хотя присутствует в его help.
- `get/set lat` — читает/задаёт сохранённую широту в десятичных градусах.
- `get/set leds.hb` — читает/задаёт режим heartbeat: all|hb|unread|off.
- `get/set leds.radio` — читает/задаёт режим индикатора радио: tx|rx|all|off.
- `get/set lon` — читает/задаёт сохранённую долготу в десятичных градусах.
- `get/set loop.detect` — читает/задаёт проверку петель: 0 — off, 1 — minimal, 2 — moderate, 3 — strict.
- `get/set meshtimesync` — читает состояние/включает или выключает синхронизацию времени по mesh.
- `get/set multi.acks` — читает/задаёт параметр повторных ACK.
- `get/set name` — читает/задаёт имя узла.
- `get/set output.power 22|28|default (nominal dBm)` — выбирает номинальную выходную мощность Heltec V4.3: 22 → SX1262 10 dBm, 28 → SX1262 22 dBm. Это не измеренная мощность; default возвращает штатный TX.
- `get/set owner.info` — читает/задаёт текстовую информацию о владельце.
- `get/set path.hash.mode` — читает/задаёт формат хешей узлов в маршруте: 0..2.
- `get/set prv.key` — читает/задаёт секретный ключ identity. Не публикуйте ответ; изменение меняет идентичность узла.
- `get/set radio` — читает/задаёт параметры LoRa: частоту, полосу, SF и CR.
- `get/set radio.fem.rxgain` — читает/задаёт усиление внешнего приёмного усилителя; нужен поддерживаемый FEM.
- `get/set radio.rxgain` — читает/задаёт усиленный приём: 0|1.
- `get/set repeat` — читает/включает или выключает ретрансляцию пакетов: on|off.
- `get/set rxduty` — читает/задаёт экономичный циклический приём: 0|1; требуется перезагрузка.
- `get/set tx` — читает/задаёт мощность самого радиочипа в dBm, в пределах платы.
- `get/set tz.offset (hours)` — читает/задаёт часовой пояс экрана: целые часы -12..14; не меняет UTC и сетевые метки.
- `get/set uplink.enable` — читает/включает или выключает uplink (при наличии в сборке).
- `get/set uplink.mqtt.host` — читает/задаёт адрес MQTT-брокера uplink.
- `get/set uplink.mqtt.iata` — читает/задаёт код местоположения uplink.
- `get/set uplink.mqtt.port` — читает/задаёт порт MQTT uplink.
- `get/set uplink.mqtt.tls` — читает/включает или выключает TLS для MQTT uplink.
- `get/set uplink.mqtt.user` — читает/задаёт имя пользователя MQTT uplink.
- `get/set uplink.wifi.ssid` — читает/задаёт SSID uplink.
- `start ota` — запускает поддерживаемый платой режим OTA.
- `start dfu` — переводит устройство в режим обновления загрузчика (DFU); способ подключения зависит от платы.
- `stop ota` — останавливает Wi-Fi OTA; только сборки с Wi-Fi OTA.
- `reboot` — перезагружает устройство.
- `clkreboot` — устанавливает часы на дату/время сборки прошивки и перезагружает устройство.
- `shutdown y` — выключает устройство при поддержке платой; y подтверждает действие.
- `erase` — стирает файловое хранилище и перезагружает устройство. Опасная команда: удаляет сохранённые данные; доступна локально.
- `help` — выводит список доступных команд.

## Repeater-bridge

`gps on|off|setloc|advert` и `get gps diag` отображаются в help только при наличии включённого GNSS-устройства в конфигурации платы.

Полный список, включая команды основного репиттера. Варианты `bridge.type` зависят от платформы.

- `ver` — выводит версию прошивки.
- `board` — выводит модель платы.
- `battery (mV, %, uptime, alert settings)` — выводит напряжение, процент заряда, uptime и настройки уведомлений; вводить battery.
- `uptime` — выводит время работы после загрузки.
- `advert` — отправляет объявление узла с распространением по сети.
- `advert.zerohop` — отправляет объявление только ближайшим узлам, без ретрансляции.
- `clock sync` — синхронизирует часы по метке времени отправителя команды; перевод назад запрещён. Для локального задания времени используйте time.
- `time <epoch>` — задаёт время Unix в секундах (UTC); обработчик не разрешает перевод назад.
- `gps on|off|setloc|advert` — включает/выключает GPS; setloc сохраняет текущие координаты; advert none|share|prefs выбирает координаты в объявлении (без координат / текущие / сохранённые).
- `password <value>` — задаёт пароль административного доступа.
- `bridge on|off|ping|logs|keygen|unpair (keygen: local)` — bridge без аргументов показывает состояние и диагностику; on/off включает/выключает мост; ping проверяет связь; logs выводит последние события BLE-подключения; keygen создаёт и сохраняет ключ (локально); unpair запрашивает удаление BLE bond партнёра и переподключение. Если ACL ещё открыт, bond удаляется после завершения disconnect; ответ команды подтверждает запрос, а результат виден в `bridge bonds` и `bridge logs`.
- `bridge bonds` — показывает число сохранённых BLE bond, наличие записи для настроенного peer и адрес первой записи (`type=P` public, `R` random). Не изменяет ключи.
- `bridge logs N|clear` — листает журнал по две записи на страницу (`N` от 1 до 32) или очищает его. Время — UTC, при несинхронизированных часах — uptime; журнал хранится в RAM до перезагрузки.
  Ответ начинается с названия платы без `board=`, затем `bridge=<connected|waiting|offline>`. При установленном мосте обе стороны измеряют RSSI активного BLE-соединения (`-96 dBm` либо `n/a dBm`, если измерение не удалось); до соединения поле dBm отсутствует. `up=` — длительность текущего или последнего соединения (`4d7h30s`, `na` до первого подключения). `phase=` — стадия BLE (`scan`, `adv`, `retry`, `secure`, `connected` и др.), `fault=` — последняя ошибка, `sec=` — ошибка безопасности, `disc=` — причина разрыва, `conn=` — ошибка подключения (только ненулевая), `retry=` — число повторов, `unpair=` — число автоматических очисток bond, `queue=занятые слоты/активная передача`. MAC-адреса доступны отдельно через `get bridge.mac` и `get bridge.peer`; приоритет — через `get bridge.priority`. `phase=scan`/`phase=adv` подтверждают ответ BLE API, но не работу радиоэфира. Коды ошибок могут относиться к разным попыткам.
- `neighbors` — выводит список соседних узлов.
- `neighbor.remove <pubkey>` — удаляет соседа по открытому ключу.
- `discover.neighbors` — запускает поиск соседей.
- `region def|get|put|remove|list|load|save` — управляет регионами: определение, чтение, изменение, удаление, список, загрузка и сохранение.
- `region allowf|denyf|home|default` — управляет разрешением flood-пересылки и выбором домашнего/основного региона.
- `tempradio <freq> <bw> <sf> <cr> <minutes>` — временно меняет радиопараметры на заданное число минут.
- `setperm <permissions> <pubkey>` — задаёт права доступа узлу по открытому ключу.
- `stats-packets` — выводит статистику пакетов.
- `stats-radio` — выводит статистику радио.
- `stats-core` — выводит системную статистику.
- `clear stats` — сбрасывает статистику.
- `get role` — выводит фактическую роль устройства.
- `get public.key` — выводит открытый ключ.
- `get gps diag` — выводит диагностику GPS и результат настройки модуля.
- `get dc.restarts` — выводит число перезапусков при тайм-ауте RX duty cycle.
- `get cad` — выводит параметры и статистику определения занятости эфира (CAD).
- `get bootloader.ver` — выводит версию загрузчика, если поддерживается платой.
- `get bridge.delay` — выводит диапазон задержки пересылки и приоритет моста.
- `get bridge.mac` — выводит локальный и партнёрский BLE MAC-адреса с типом (public/random), состояние шифрования и счётчики передачи.
- `get uplink.status` — выводит состояние Wi-Fi/MQTT uplink; только сборки с uplink.
- `set cad.auto` — включает/выключает автоматическую настройку CAD: on|off|default.
- `set cad.offset` — задаёт смещение порога CAD: -8..12 или default.
- `set probe.interval` — задаёт интервал измерений радио в секундах: 10..255; 0 отключает периодические пробы.
- `set cad.busycap` — задаёт порог доли проб с обнаруженным слабым сигналом: 10..90%; 0 отключает этот предел.
- `set cad.reset` — сбрасывает статистику проб и возвращает смещение CAD к значению по умолчанию.
- `set bridge.key <32-hex>` — сохраняет 16-байтовый ключ (32 hex-символа); одинаковый на двух концах моста.
- `set uplink.mqtt.password <password>` — задаёт пароль MQTT uplink.
- `set uplink.wifi.psk <password>` — задаёт пароль Wi-Fi uplink.
- `get/set adc.multiplier` — читает/задаёт коэффициент измерения батареи; 0 выбирает штатную калибровку платы.
- `get/set advert.interval` — читает/задаёт интервал локальных объявлений в минутах (шаг хранения — 2 минуты).
- `get/set af` — читает/задаёт коэффициент паузы после передачи; связан с dutycycle формулой 100/(af+1).
- `get/set backoff.multiplier` — читает/задаёт множитель задержки при дубликатах; 0 отключает механизм.
- `get/set battery.alert on|off (default on)` — включает/выключает уведомления о низком заряде; по умолчанию on.
- `get/set battery.group <name> (default #zephcore)` — задаёт группу уведомлений; по умолчанию #zephcore.
- `get/set battery.interval <hours> (1..168, default 12)` — задаёт интервал повторных уведомлений в часах; по умолчанию 12.
- `get/set battery.threshold <mV> (0..5000, default: board; 0=no alerts)` — задаёт порог низкого заряда в мВ; по умолчанию 3250 на nRF52, 3500 на остальных платформах. 0 означает 0 мВ и фактически отключает уведомления.
- `get/set bridge.peer <MAC> public|random` — читает/задаёт адрес партнёра и тип BLE-адреса; для BLE используйте адрес транспорта, не Wi-Fi MAC.
- `get/set bridge.priority (0..7)` — читает/задаёт приоритет пересылки: 0 — высший, 7 — низший.
- `get/set bridge.type ble|esp-now` — читает/задаёт транспорт моста на ESP32: ble|esp-now.
- `get/set bridge.type ble` — читает/задаёт транспорт моста на nRF: только ble.
- `get/set display.rotate` — читает/задаёт разворот дисплея на 180°; только поддерживаемые дисплеи.
- `get/set dutycycle` — читает/задаёт ограничение доли времени передачи: 1..100%.
- `get/set extra.sf` — читает/задаёт дополнительные SF для одновременного приёма на LR2021; не для SX1262.
- `get/set flood.advert.interval` — читает/задаёт интервал сетевых объявлений в часах.
- `get/set flood.max` — читает/задаёт предел числа переходов для flood-пакетов.
- `get/set flood.max.advert` — читает/задаёт предел переходов для объявлений.
- `get/set flood.max.unscoped` — читает/задаёт предел переходов для flood-пакетов без региона.
- `get/set freq` — читает/задаёт частоту LoRa.
- `get/set gps duty` — читает/задаёт интервал GPS в секундах; 0 — постоянная работа.
- `get/set guest.password` — читает/задаёт пароль гостевого доступа; пустой пароль допускает открытый гостевой вход.
- `get/set input.rotate` — читает/задаёт инверсию направлений джойстика/кнопок навигации.
- `get/set int.thresh` — читает/задаёт порог обработки помех; set не поддерживается на companion, хотя присутствует в его help.
- `get/set lat` — читает/задаёт сохранённую широту в десятичных градусах.
- `get/set leds.hb` — читает/задаёт режим heartbeat: all|hb|unread|off.
- `get/set leds.radio` — читает/задаёт режим индикатора радио: tx|rx|all|off.
- `get/set lon` — читает/задаёт сохранённую долготу в десятичных градусах.
- `get/set loop.detect` — читает/задаёт проверку петель: 0 — off, 1 — minimal, 2 — moderate, 3 — strict.
- `get/set meshtimesync` — читает состояние/включает или выключает синхронизацию времени по mesh.
- `get/set multi.acks` — читает/задаёт параметр повторных ACK.
- `get/set name` — читает/задаёт имя узла.
- `get/set output.power 22|28|default (nominal dBm)` — выбирает номинальную выходную мощность Heltec V4.3: 22 → SX1262 10 dBm, 28 → SX1262 22 dBm. Это не измеренная мощность; default возвращает штатный TX.
- `get/set owner.info` — читает/задаёт текстовую информацию о владельце.
- `get/set path.hash.mode` — читает/задаёт формат хешей узлов в маршруте: 0..2.
- `get/set prv.key` — читает/задаёт секретный ключ identity. Не публикуйте ответ; изменение меняет идентичность узла.
- `get/set radio` — читает/задаёт параметры LoRa: частоту, полосу, SF и CR.
- `get/set radio.fem.rxgain` — читает/задаёт усиление внешнего приёмного усилителя; нужен поддерживаемый FEM.
- `get/set radio.rxgain` — читает/задаёт усиленный приём: 0|1.
- `get/set repeat` — читает/включает или выключает ретрансляцию пакетов: on|off.
- `get/set rxduty` — читает/задаёт экономичный циклический приём: 0|1; требуется перезагрузка.
- `get/set tx` — читает/задаёт мощность самого радиочипа в dBm, в пределах платы.
- `get/set tz.offset (hours)` — читает/задаёт часовой пояс экрана: целые часы -12..14; не меняет UTC и сетевые метки.
- `get/set uplink.enable` — читает/включает или выключает uplink (при наличии в сборке).
- `get/set uplink.mqtt.host` — читает/задаёт адрес MQTT-брокера uplink.
- `get/set uplink.mqtt.iata` — читает/задаёт код местоположения uplink.
- `get/set uplink.mqtt.port` — читает/задаёт порт MQTT uplink.
- `get/set uplink.mqtt.tls` — читает/включает или выключает TLS для MQTT uplink.
- `get/set uplink.mqtt.user` — читает/задаёт имя пользователя MQTT uplink.
- `get/set uplink.wifi.ssid` — читает/задаёт SSID uplink.
- `start dfu` — переводит устройство в режим обновления загрузчика (DFU); способ подключения зависит от платы.
- `reboot` — перезагружает устройство.
- `clkreboot` — устанавливает часы на дату/время сборки прошивки и перезагружает устройство.
- `shutdown y` — выключает устройство при поддержке платой; y подтверждает действие.
- `erase` — стирает файловое хранилище и перезагружает устройство. Опасная команда: удаляет сохранённые данные; доступна локально.
- `help` — выводит список доступных команд.

## Room server

- `setperm <permissions> <pubkey>` — задаёт права доступа узлу по открытому ключу.
- `region def|get|put|remove|list|load|save` — управляет регионами: определение, чтение, изменение, удаление, список, загрузка и сохранение.
- `region allowf|denyf|home|default` — управляет разрешением flood-пересылки и выбором домашнего/основного региона.
- `ver` — выводит версию прошивки.
- `board` — выводит модель платы.
- `advert` — отправляет объявление узла с распространением по сети.
- `advert.zerohop` — отправляет объявление только ближайшим узлам, без ретрансляции.
- `clock sync` — синхронизирует часы по метке времени отправителя команды; перевод назад запрещён. Для локального задания времени используйте time.
- `time <epoch>` — задаёт время Unix в секундах (UTC); обработчик не разрешает перевод назад.
- `gps on|off|setloc|advert` — включает/выключает GPS; setloc сохраняет текущие координаты; advert none|share|prefs выбирает координаты в объявлении (без координат / текущие / сохранённые).
- `get gps diag` — выводит диагностику GPS и результат настройки модуля.
- `neighbors` — выводит список соседних узлов.
- `neighbor.remove <pubkey>` — удаляет соседа по открытому ключу.
- `tempradio <freq> <bw> <sf> <cr> <minutes>` — временно меняет радиопараметры на заданное число минут.
- `password <value>` — задаёт пароль административного доступа.
- `stats-packets` — выводит статистику пакетов.
- `stats-radio` — выводит статистику радио.
- `stats-core` — выводит системную статистику.
- `clear stats` — сбрасывает статистику.
- `get acl` — выводит список прав доступа (ACL).
- `get public.key` — выводит открытый ключ узла.
- `get role` — выводит роль устройства.
- `get dc.restarts` — выводит число перезапусков при тайм-ауте RX duty cycle.
- `get cad` — выводит параметры и статистику определения занятости эфира (CAD).
- `get/set leds.radio` — читает/задаёт режим индикатора радио: tx|rx|all|off.
- `get/set leds.hb` — читает/задаёт режим heartbeat: all|hb|unread|off.
- `get/set display.rotate` — читает/задаёт разворот дисплея на 180°; только поддерживаемые дисплеи.
- `get/set input.rotate` — читает/задаёт инверсию направлений джойстика/кнопок навигации.
- `get/set tz.offset (hours)` — читает/задаёт часовой пояс экрана: целые часы -12..14; не меняет UTC и сетевые метки.
- `get/set radio.fem.rxgain` — читает/задаёт усиление внешнего приёмного усилителя; нужен поддерживаемый FEM.
- `get/set extra.sf` — читает/задаёт дополнительные SF для одновременного приёма на LR2021; не для SX1262.
- `get/set dutycycle` — читает/задаёт ограничение доли времени передачи: 1..100%.
- `get/set af` — читает/задаёт коэффициент паузы после передачи; связан с dutycycle формулой 100/(af+1).
- `get/set int.thresh` — читает/задаёт порог обработки помех; set не поддерживается на companion, хотя присутствует в его help.
- `get/set multi.acks` — читает/задаёт параметр повторных ACK.
- `get/set allow.read.only` — читает/задаёт разрешение доступа только для чтения.
- `get/set flood.advert.interval` — читает/задаёт интервал сетевых объявлений в часах.
- `get/set advert.interval` — читает/задаёт интервал локальных объявлений в минутах (шаг хранения — 2 минуты).
- `get/set guest.password` — читает/задаёт пароль гостевого доступа; пустой пароль допускает открытый гостевой вход.
- `get/set prv.key` — читает/задаёт секретный ключ identity. Не публикуйте ответ; изменение меняет идентичность узла.
- `get/set name` — читает/задаёт имя узла.
- `get/set repeat` — читает/включает или выключает ретрансляцию пакетов: on|off.
- `get/set lat` — читает/задаёт сохранённую широту в десятичных градусах.
- `get/set lon` — читает/задаёт сохранённую долготу в десятичных градусах.
- `get/set radio` — читает/задаёт параметры LoRa: частоту, полосу, SF и CR.
- `get/set radio.rxgain` — читает/задаёт усиленный приём: 0|1.
- `get/set flood.max.advert` — читает/задаёт предел переходов для объявлений.
- `get/set flood.max.unscoped` — читает/задаёт предел переходов для flood-пакетов без региона.
- `get/set flood.max` — читает/задаёт предел числа переходов для flood-пакетов.
- `get/set backoff.multiplier` — читает/задаёт множитель задержки при дубликатах; 0 отключает механизм.
- `get/set owner.info` — читает/задаёт текстовую информацию о владельце.
- `get/set path.hash.mode` — читает/задаёт формат хешей узлов в маршруте: 0..2.
- `get/set loop.detect` — читает/задаёт проверку петель: 0 — off, 1 — minimal, 2 — moderate, 3 — strict.
- `get/set tx` — читает/задаёт мощность самого радиочипа в dBm, в пределах платы.
- `get/set freq` — читает/задаёт частоту LoRa.
- `get/set adc.multiplier` — читает/задаёт коэффициент измерения батареи; 0 выбирает штатную калибровку платы.
- `get/set rxduty` — читает/задаёт экономичный циклический приём: 0|1; требуется перезагрузка.
- `get/set gps duty` — читает/задаёт интервал GPS в секундах; 0 — постоянная работа.
- `get/set meshtimesync` — читает состояние/включает или выключает синхронизацию времени по mesh.
- `set cad.auto` — включает/выключает автоматическую настройку CAD: on|off|default.
- `set cad.offset` — задаёт смещение порога CAD: -8..12 или default.
- `set probe.interval` — задаёт интервал измерений радио в секундах: 10..255; 0 отключает периодические пробы.
- `set cad.busycap` — задаёт порог доли проб с обнаруженным слабым сигналом: 10..90%; 0 отключает этот предел.
- `set cad.reset` — сбрасывает статистику проб и возвращает смещение CAD к значению по умолчанию.
- `sensor get <key>` — читает настройку датчика по ключу.
- `sensor set <key> <value>` — меняет настройку датчика.
- `sensor list start` — начинает вывод списка настроек датчиков.
- `start dfu` — переводит устройство в режим обновления загрузчика (DFU); способ подключения зависит от платы.
- `start ota` — запускает поддерживаемый платой режим OTA-обновления.
- `stop ota` — останавливает OTA, если это поддерживается сборкой.
- `reboot` — перезагружает устройство.
- `clkreboot` — устанавливает часы на дату/время сборки прошивки и перезагружает устройство.
- `erase` — стирает файловое хранилище и перезагружает устройство. Опасная команда: удаляет сохранённые данные; доступна локально.
- `help` — выводит список доступных команд; ? — синоним в общем CLI.

## Observer

- `set wifi.ssid <name>` — задаёт имя сети Wi-Fi.
- `set wifi.psk <password>` — задаёт пароль Wi-Fi; пустой пароль — открытая сеть.
- `set mqtt.host <hostname>` — задаёт адрес MQTT-брокера.
- `set mqtt.port <port>` — задаёт порт MQTT; по умолчанию 8883.
- `set mqtt.tls 0|1` — выключает/включает TLS; по умолчанию включён.
- `set mqtt.user <username>` — задаёт пользователя MQTT.
- `set mqtt.password <pass>` — задаёт пароль MQTT.
- `set mqtt.iata <code>` — задаёт код местоположения.
- `set name <name>` — задаёт имя узла.
- `set lat <-90..90>` — задаёт широту.
- `set lon <-180..180>` — задаёт долготу; координаты и нестандартное имя позволяют объявлять узел.
- `set meshtimesync on|off` — включает/выключает коррекцию времени по mesh.
- `set freq <MHz|Hz>` — задаёт частоту 300–1000 МГц; принимает МГц или Гц.
- `set sf <7-12>` — задаёт spreading factor.
- `set bw <idx|kHz>` — задаёт полосу в кГц или индекс: 0=125, 1=250, 2=500, 3=62.5, 4=41.7, 5=31.25.
- `set cr <5-8>` — задаёт coding rate.
- `get name` — выводит имя.
- `get role` — выводит роль observer.
- `get board` — выводит плату.
- `get version` — выводит версию и дату сборки.
- `get public.key` — выводит открытый ключ.
- `get radio` — выводит параметры LoRa.
- `get lat / get lon` — выводит координаты; это две отдельные команды.
- `get wifi.ssid` — выводит SSID.
- `get wifi.status` — выводит состояние Wi-Fi.
- `get mqtt.status` — выводит состояние MQTT.
- `get mqtt.host` — выводит адрес брокера.
- `get mqtt.port` — выводит порт брокера.
- `get mqtt.tls` — выводит настройку TLS.
- `get mqtt.user` — выводит пользователя MQTT.
- `get mqtt.iata` — выводит код местоположения.
- `get meshtimesync` — выводит состояние согласования времени.
- `help` — выводит список доступных команд.

## Источники и границы списка

- `zephcore/helpers/LocalCLIHelp.h` — порядок companion, repeater, repeater-bridge и room server.
- `zephcore/helpers/CommonCLI.cpp` — общие обработчики.
- `zephcore/src/main_companion.cpp` — SOS, Fall, tracking и настройки companion.
- `zephcore/app/main_observer.cpp` — порядок help observer.

Это описание команд, перечисленных в help, а не каталог всех внутренних и совместимых алиасов обработчиков.
