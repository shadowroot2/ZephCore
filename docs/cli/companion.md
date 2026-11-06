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
│  ├─ gps [on|off|setloc|advert]
│  ├─ get/set gps duty
│  ├─ get/set lat
│  └─ get/set lon
├─ nav                         (при GPS)
│  ├─ nav [start|stop]
│  ├─ nav telemetry [on|off]
│  ├─ wp [list|n|clear]
│  ├─ wp save [n [name]]
│  ├─ wp name [n] <name>
│  ├─ get/set nav.interval
│  └─ get/set nav.telemetry.interval
├─ geo                         (при GPS)
│  ├─ geo on|off
│  ├─ geo save
│  ├─ get/set geo.point
│  └─ get/set geo.radius
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
│  ├─ tracking [on|off]
│  ├─ get/set tracking.group
│  ├─ get/set tracking.interval
│  ├─ fall on|off
│  ├─ get/set fall.sens (1-5)
│  ├─ get/set v.contact
│  └─ offgrid on|off
├─ ui
│  ├─ buzz [on|off]
│  ├─ leds [on|off]
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
│  ├─ gps [on|off|setloc|advert]
│  └─ get/set gps duty | lat | lon
├─ nav                         (при GPS)
│  └─ nav | wp list
├─ wifi
│  ├─ get wifi.status | wifi.ip | wifi.enabled
│  └─ set wifi.enabled 0|1
├─ extra
│  ├─ findme | sos | tracking [on|off]
│  ├─ get/set tracking.group | tracking.interval
│  ├─ fall on|off | get/set fall.sens (1-5)
│  └─ get/set v.contact
├─ ui
│  ├─ buzz [on|off] | leds [on|off]
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

`wifi`, Fall и display/input rotation показываются только при наличии соответствующей возможности сборки. Удалённо не выполняются `get/set prv.key`, `get/set wifi.pwd`, `get/set wifi.ssid`, `set wifi.clear`, `set pin`, `set freq`, `set radio`, `set tx`, `set adc.multiplier`, `set autoshutdown`, `offgrid on|off`, `clear stats`, `shutdown`, `poweroff`, OTA/DFU и `erase`.

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
- `sos` — запускает SOS; при ожидании GPS отправляет предварительные сообщения, затем основное. При buzzer off SOS беззвучный. Запуск через удалённый LoRa CLI всегда беззвучный, включая подтверждение, промежуточные и финальную отправки. На T-1000E и M3 одно короткое нажатие во время ожидания SOS отменяет сценарий, воспроизводит сигнал отмены Fall, восстанавливает состояние GPS и отправляет в `#sos` сообщение `❌ SOS отменён`.
- `gps [on|off|setloc|advert]` — без аргумента выводит статус GPS: `on, active|standby, fix|no fix, N sats`, `off` или `Can't find GPS`; on|off включает/выключает GPS; setloc сохраняет текущие координаты; advert none|share|prefs выбирает координаты в объявлении (без координат / текущие / сохранённые).
- `get wifi.status` — выводит состояние подключения WiFi: `connected` или `disconnected`.
- `get wifi.ip` — выводит текущий IPv4-адрес или сообщает, что подключения нет.
- `get wifi.ssid` — выводит сохранённый SSID.
- `set wifi.ssid <name>` — сохраняет SSID; применяется после перезагрузки.
- `get wifi.pwd` — выводит сохранённый пароль WiFi; не публикуйте этот ответ.
- `set wifi.pwd <password>` — сохраняет пароль WiFi; применяется после перезагрузки.
- `get wifi.enabled` — выводит сохранённое состояние WiFi: `0` или `1`.
- `set wifi.enabled 0|1` — сохраняет выключенное/включённое состояние WiFi; применяется после перезагрузки.
- `set wifi.clear` — удаляет сохранённые SSID и пароль; применяется после перезагрузки.
- `wp list` — список точек с выбранной (`*1:GPS HOME`); единственная wp-команда, доступная удалённо.
- `wp clear` — сбрасывает точки 2–10 на базовые: имя `FREE`, координаты по умолчанию. Точку 1 (`GPS HOME`) не трогает никогда: её имя неизменяемо, координаты не сбрасываются. Доступно только локально (USB/BLE).
- `wp [n]` — без аргумента список из 10 точек с выбранной (`*1:GPS HOME`); `wp <n>` выбирает точку для экрана GPS HOME и отвечает с её координатами (`OK - wp 2 FREE  43.24112,76.92730`). Точка 1 всегда `GPS HOME`, по умолчанию выбрана она; после перезагрузки возврат к точке 1.
- `wp save [n [name]]` — сохраняет текущий GPS-фикс в выбранную точку или в точку `n` (и выбирает её). Необязательное имя (до 10 символов, пробелы считаются) применяется сразу, координаты пишутся по свежему фиксу как на экране (`Waiting GPS FIX...`, тайм-аут 5 минут); без имени `FREE` становится `SAVED` при сохранении, чужие имена не трогаются. Имя в `wp save 1 ...` отклоняется — точка 1 всегда `GPS HOME`. Локально (USB/BLE); при активной навигации запрещено.
- `wp name [n] <name>` — меняет имя выбранной точки или точки `n`, не трогая координаты; имя применяется сразу, те же ограничения (10 символов, точка 1 не переименовывается). Локально (USB/BLE).
- `nav [start|stop]` — без аргумента отдаёт состояние навигации с выбранной точкой; при `start` — ещё расстояние и направление (`nav: on  1-GPS HOME  2.34 km  NE`, без свежего фикса `nav: on  1-GPS HOME  ?? km  ?`), при `stop` — `nav: off  1-GPS HOME`. `nav start` отвечает `OK - Navigation to 1-GPS HOME  43.24112,76.92730` (только локально, удалённо доступен лишь статус): GPS и `gps duty 0`, обновление расстояния и направления раз в 5 секунд. Выключение возвращает GPS и duty в состояние на момент включения. Тик навигации работает на всех GNSS-платах, включая headless: неверный маршрут (рост расстояния на 30/60/90 м без подхода) шлёт в vContact `Вы движетесь в неверном направлении!` со строкой точки и `📏 2.34 км  СВ  ↗️` и пищит, прибытие ближе 15 м — `🏁 Вы прибыли в точку назначения!` с точкой, временем в пути, пройденным расстоянием и ссылкой на Google Maps, мелодия «Ta-Da!» и авто-STOP. Ещё в vContact идёт отчёт `📍 1-GPS HOME` + `📏 2.34 км  СВ  ↗️` сразу после первого фикса и затем по интервалу.
- `get/set nav.interval <min>` — интервал этих vContact-отчётов в минутах (`nav.interval: 1 min`), по умолчанию 1, можно 0–1440. `0` (`nav.interval: off`) выключает все уведомления о навигации в vContact — отчёт, неверный маршрут и прибытие; писк и авто-STOP при прибытии остаются. Локально (USB/BLE).
- `nav telemetry [on|off]` — LoRa-телеметрия навигации в группу tracking (`tracking_group`, по умолчанию `#tracks`), по умолчанию выключена. При активной навигации шлёт в группу отчёт `📍 1-GPS HOME` + `📏 2.34 км  СВ  ↗️` со ссылкой на текущую позицию по первому фиксу и далее раз в `nav.telemetry.interval`, и сообщение о прибытии (`🏁 Прибыл в точку назначения!` с точкой, временем в пути, пройденным расстоянием и ссылкой на Google Maps) при подходе ближе 15 м. Отчёт уходит, только когда расстояние до точки изменилось на 50 м (правило движения tracking); при потере фикса — одно сообщение `🛰️ Связь со спутниками потеряна...`, отправка возобновляется по его восстановлении. Неверный маршрут и сохранение точки по LoRa не отправляются. Локально (USB/BLE).
- `get/set nav.telemetry.interval <min>` — интервал LoRa-телеметрии в минутах (`nav.telemetry.interval: 10 min`), по умолчанию 10, минимум 5 — как у `tracking.interval`. Локально (USB/BLE).
- `geo on|off` — геозона вокруг одной точки (тумблер, доступен во всех CLI). `on` запоминает текущие GPS и duty, держит GPS включённым с duty 300 с и проверяет расстояние до точки по каждому свежему фиксу; `off` возвращает всё как было. Тумблер переживает перезагрузку: при `on` в prefs после ребута duty снова пинится, а первый фикс один раз сообщает состояние зоны — `✅ Вернулся в геозону` внутри или `🚪 Вышел из геозоны` за радиусом. Запрещён при навигации, сохранении точки и ожидающем SOS; с `tracking on` взаимоисключающ (оба владеют duty). При уходе за радиус (с первого фикса) и возврате (ниже радиуса − 50 м) в группу tracking уходит по LoRa и скрытно (не в исходящих, без vContact): `🚪 Вышел из геозоны` / `✅ Вернулся в геозону` с `📐 Радиус: ...`, `📏 Расстояние: ...` и ссылкой на Google Maps. Локального сигнала нет.
- `geo save` — сохраняет текущий GPS-фикс как точку геозоны (тот же конвейер, что у `wp save`: ожидание фикса, тайм-аут 5 минут). Локально (USB/BLE); запрещено при навигации, сохранении и активном tracking.
- `get/set geo.point <lat> <lon>` — читает/пишет точку геозоны вручную (`geo.point: 43.24112,76.92730 (saved)` или `(default)`). Пример: `set geo.point 43.241117 76.927301`. Единственная правка координат, доступная из удалённого CLI.
- `get/set geo.radius <m>` — радиус зоны **в метрах**, одно глобальное значение (`geo.radius: 3000 m (50..50000)`), по умолчанию 3000, диапазон 50–50000. Смена радиуса пере-армит проверку. Локально (USB/BLE).
- `tracking [on|off]` — без аргумента отдаёт текущее состояние (`tracking: on (5 min)`); on|off включает/выключает периодическую отправку координат. При включении через удалённый LoRa CLI запуск, остановка и все периодические отправки выполняются без звука. При выключении в группу tracking отправляется итог маршрута (путь, время в движении и на стоянках, число остановок, средняя и максимальная скорость, точка финиша).
- `fall on|off` — включает/выключает детекцию падения и мониторинг акселерометра (M3, T-1000E). Падение звучит даже при buzzer off.
- `leds [on|off]` — без аргумента выводит текущий статус индикации; on|off включает/выключает программную LED-индикацию; зарядная индикация зависит от платы.
- `buzz [on|off]` — без аргумента выводит текущий режим буззера; on|off включает/выключает обычные звуковые уведомления; исключение — тревога падения.
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
- `get/set tz.offset (hours)` — читает/задаёт часовой пояс экрана: часы −12..+14 с шагом 0.25 (например 5.5 или 5.75); не меняет UTC и сетевые метки.
- `get/set v.batteryalert` — читает/задаёт порог уведомления о низком заряде в vContact, в мВ; 0 отключает, default возвращает штатный порог.
- `get/set v.contact` — читает/задаёт доступность виртуального контакта уведомлений; обработчика команд в этом чате нет.
- `start ota` — запускает поддерживаемый платой режим OTA-обновления.
- `start dfu` — переводит устройство в режим обновления загрузчика (DFU); способ подключения зависит от платы.
- `stop ota` — останавливает OTA, если это поддерживается сборкой.
- `reboot` — перезагружает устройство.
- `shutdown y` — выключает устройство при поддержке платой; y подтверждает действие.
- `erase` — стирает файловое хранилище и перезагружает устройство. Опасная команда: удаляет сохранённые данные; доступна локально.
- `help` — выводит список доступных команд; ? — синоним в общем CLI.
