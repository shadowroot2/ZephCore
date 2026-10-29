# Команды CLI

Загружать только документ нужной роли. Списки учитывают USB, BLE и remote
LoRa CLI, capability оборудования и ограничения опасных команд.

| Роль | Документ |
|---|---|
| Companion | [cli/companion.md](cli/companion.md) |
| Repeater | [cli/repeater.md](cli/repeater.md) |
| Repeater-bridge | [cli/bridge.md](cli/bridge.md) |
| Room server | [cli/room-server.md](cli/room-server.md) |
| Observer | [cli/observer.md](cli/observer.md) |
| Источники и границы | [cli/sources.md](cli/sources.md) |

После любой правки CLI обновлять обработчик, help всех затронутых транспортов
и соответствующий документ роли. BLE help должен помещаться в 172 байта
текста, tagged remote reply — в 158 байт вместе с NUL.

## Удалённый CLI репиттера

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

Показано полное дерево всех вариантов сборки. На устройстве остаются только
поддерживаемые строки:

- `bridge` — только роль repeater-bridge. При ESP-NOW скрыты `bridge logs`,
  `bridge logs N|clear`, `bridge unpair` и `bridge bonds`.
- `uplink` — только при включённых repeater uplink и MQTT.
- GNSS-команды и группа `sensor` — при наличии GNSS в devicetree.
  Без GNSS в `gps` остаются `gps advert`, `get/set lat` и `get/set lon`.
- `adc.multiplier` — при батарейном ADC; `radio.fem.rxgain` — при
  `lna-bypass-gpios` у радио.
- SX127x не показывает `cad`, `radio.rxgain`, `rxduty` и `dc.restarts`.
- LED-команды — при соответствующих светодиодах; `ui` скрыта без LED и дисплея.
  `display.rotate` требует поддержки поворота панели.
- DFU, powersaving и запросы источника питания/напряжения при загрузке —
  по Kconfig-возможностям платформы. `shutdown y` — при `CONFIG_POWEROFF`.
- `get pm` — при ESP32 light sleep; `get mem`, `get pwr`, `get loop.wakes` —
  при соответствующих диагностических опциях.
- `log` — при `CONFIG_LOG`. Файла журнала у репиттера нет, поэтому `log erase`
  и выгрузка `log` в help отсутствуют.

Максимальный размер группы: 158 байт с NUL. `rf` LR2021 — 149/158,
`mesh` — 156/158, `bridge` BLE — 157/158, ESP-NOW — 133/158.

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
