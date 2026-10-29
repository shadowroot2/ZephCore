## Repeater

Команды работы с приёмником GPS отображаются в help только при наличии GNSS в конфигурации платы. `gps advert` и ручные координаты доступны без GNSS.

### Remote CLI

```text
help
├─ id
│  ├─ get role
│  ├─ get/set name
│  ├─ get/set owner.info
│  ├─ get public.key
│  └─ set prv.key
├─ access
│  ├─ password <value>
│  ├─ get/set guest.password
│  └─ setperm <permissions> <pubkey>
├─ rf
│  ├─ get freq
│  ├─ get freqerr (LR2021)
│  ├─ get/set radio
│  ├─ get/set tx
│  ├─ get/set af
│  ├─ get/set path.hash.mode
│  ├─ get/set dutycycle
│  ├─ get/set int.thresh
│  ├─ get/set radio.rxgain (кроме SX127x)
│  ├─ get/set extra.sf (LR2021)
│  ├─ get/set radio.fem.rxgain (при lna-bypass-gpios)
│  ├─ get/set rxduty (кроме SX127x)
│  └─ tempradio <freq> <bw> <sf> <cr> <minutes>
├─ region
│  ├─ region
│  ├─ region get|put|remove|list
│  ├─ region def|load|save
│  └─ region allowf|denyf|home|default
├─ cad (кроме SX127x)
│  ├─ get cad
│  ├─ get cad.stats
│  ├─ get txdelay
│  ├─ set cad.reset
│  ├─ get cad.auto
│  ├─ get cad.offset
│  ├─ get probe.interval
│  ├─ get cad.busycap
│  └─ set cad.auto|offset|busycap|probe.interval
├─ mesh
│  ├─ advert
│  ├─ advert.zerohop
│  ├─ get/set repeat
│  ├─ get/set advert.interval
│  ├─ get/set loop.detect
│  ├─ get/set meshtimesync
│  ├─ get/set multi.acks
│  ├─ get/set flood.advert.interval
│  ├─ get/set flood.max
│  ├─ get/set flood.max.advert
│  ├─ get/set flood.max.unscoped
│  └─ get/set backoff.multiplier
├─ find
│  ├─ discover.neighbors
│  ├─ neighbors
│  └─ neighbor.remove <pubkey>
├─ gps
│  ├─ gps (при GNSS)
│  ├─ gps on|off (при GNSS)
│  ├─ gps sync|setloc|advert (при GNSS)
│  ├─ gps advert (без GNSS)
│  ├─ get gps diag (при GNSS)
│  ├─ get/set gps duty (при GNSS)
│  ├─ get/set gps standby (при GNSS)
│  ├─ get/set lat
│  └─ get/set lon
├─ ui (при светодиодах или дисплее)
│  ├─ get/set leds (при светодиодах)
│  ├─ get/set leds.brightness (при светодиодах)
│  ├─ get/set leds.hb (при heartbeat LED)
│  ├─ get/set leds.radio (при radio LED)
│  ├─ get/set display.rotate (при поддержке поворота панели)
│  ├─ get/set input.rotate (при дисплее)
│  └─ get/set tz.offset (при дисплее)
├─ power
│  ├─ power
│  ├─ powersaving on|off (при поддержке)
│  ├─ get pwrmgt.support
│  ├─ get pwrmgt.source (при поддержке)
│  ├─ get pwrmgt.bootreason
│  └─ get pwrmgt.bootmv (при поддержке)
├─ batt
│  ├─ battery
│  ├─ get/set adc.multiplier (при battery ADC)
│  ├─ get/set battery.alert
│  ├─ get/set battery.group
│  ├─ get/set battery.interval
│  └─ get/set battery.threshold
├─ sensor (при GNSS)
│  ├─ sensor get <key>
│  ├─ sensor set <key> <value>
│  └─ sensor list <start>
├─ bridge (только repeater-bridge)
│  ├─ bridge [on|off]
│  ├─ bridge ping|logs|unpair|bonds
│  ├─ bridge logs N|clear
│  ├─ get bridge.delay
│  ├─ get bridge.mac
│  ├─ set bridge.key <32-hex>
│  ├─ get/set bridge.peer
│  ├─ get/set bridge.priority
│  └─ get/set bridge.type
├─ uplink (только Wi-Fi/MQTT uplink)
│  ├─ get uplink.status
│  ├─ get/set uplink.enable
│  ├─ set uplink.wifi.psk
│  ├─ get/set uplink.mqtt.host|port|tls|user|iata
│  ├─ set uplink.mqtt.password
│  └─ get/set uplink.wifi.ssid
├─ stat
│  ├─ stats-packets
│  ├─ stats-radio
│  ├─ stats-core
│  ├─ clear stats
│  ├─ get pm (при ESP32 light sleep)
│  ├─ get mem [<n>]
│  ├─ get pwr [<n>]
│  └─ get loop.wakes
├─ log
│  ├─ log start
│  └─ log stop
└─ sys
   ├─ ver
   ├─ board
   ├─ uptime
   ├─ clock
   ├─ clock sync
   ├─ time <epoch>
   ├─ get dc.restarts (кроме SX127x)
   ├─ get bootloader.ver
   ├─ start dfu (при поддержке)
   ├─ reboot
   └─ shutdown y (при CONFIG_POWEROFF)
```

Команды батареи вынесены в отдельную группу `help batt`. При ESP-NOW `help bridge`
показывает только `bridge [on|off]`, `bridge ping`, чтение и настройку моста;
`bridge logs`, `bridge bonds` и `bridge unpair` доступны только при BLE.
Группа `log` показывается только при `CONFIG_LOG`; `log erase` не указан,
поскольку файл журнала в репиттере не реализован.
Группа `cad` и настройка `rxduty` скрыты на SX127x, где драйвер не имеет CAD
и приёмного duty cycle.
Группа `sensor` скрыта без GNSS: единственная настройка этого интерфейса — `gps`.

`get prv.key`, `set freq`, `get acl` и `erase` остаются только в полном локальном USB help.

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
- `start dfu` — переводит устройство в режим обновления загрузчика (DFU); способ подключения зависит от платы.
- `reboot` — перезагружает устройство.
- `shutdown y` — выключает устройство при поддержке платой; y подтверждает действие.
- `erase` — стирает файловое хранилище и перезагружает устройство. Опасная команда: удаляет сохранённые данные; доступна локально.
- `help` — выводит список доступных команд.

### Вид `help mesh` на устройстве

```text
advert[.zerohop]
get/set:
repeat
advert.interval
loop.detect
meshtimesync
multi.acks
flood.{advert.interval,max,max.advert,max.unscoped}
backoff.multiplier
```

`get/set:` относится ко всем следующим настройкам. Скобки после `flood.`
перечисляют суффиксы: например, `get flood.max` или `set flood.max 3`.
Ответ занимает 156 байт с NUL при пределе 158; имена не разрываются между строками.

### help rf — максимальная конфигурация

```text
get freq
get freqerr
get/set:
radio
tx
af
path.hash.mode
dutycycle
int.thresh
radio.rxgain
extra.sf
radio.fem.rxgain
rxduty
tempradio f bw sf cr min
```

Размер с NUL: 149/158 байт.

### help bridge — BLE

```text
bridge [on|off]
bridge ping|unpair|bonds
bridge logs [N|clear]
get bridge.delay|mac
set bridge.key <32-hex>
get/set:
bridge.peer
bridge.priority
bridge.type
```

Размер с NUL: 157/158 байт.

### help bridge — ESP-NOW

```text
bridge [on|off]
bridge ping
get bridge.delay
get bridge.mac
set bridge.key <32-hex>
get/set:
bridge.peer
bridge.priority
bridge.type
```

Размер с NUL: 133/158 байт.
