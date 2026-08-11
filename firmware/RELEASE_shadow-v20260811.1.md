# Shadow ZephCore v20260811.1

Дата: 2026-08-11
База: `14d7d92` (`1.16.7-zephcore`)

Release с GPS tracking для companion и профилем XIAO nRF52840 + Wio-SX1262 repeater. Все указанные образы собраны из текущего дерева; дата прошивки генерируется во время сборки.

## Что изменилось

- **GPS Tracking для companion**: `tracking on|off`, минимум 5 минут, по умолчанию 10 минут. Координаты отправляются не чаще заданного интервала, только с GPS fix и только после перемещения не менее 50 м. Пока tracking включён, GPS остаётся активным; после выключения восстанавливаются прежние GPS и GPS duty настройки.
- **Группа Tracking**: `get/set tracking.group`. При `set tracking.group <name>` автоматически добавляется `#` и создаётся публичная группа. По умолчанию — `#tracks`. Сообщение имеет вид `🐾 <Google Maps link>`.
- **SOS**: используется группа `#sos`; GPS включается на время поиска fix (до 5 минут), а прежний GPS state и duty возвращаются после постановки сообщения в очередь.
- **Offgrid**: доступен в companion CLI и v.Contact (`offgrid on|off`), но намеренно сбрасывается в off при каждом перезапуске.
- **Интерфейс и звук**: tracking расположен после GPS в меню; экран показывает статус, интервал и действие. Подтверждение переключения — шесть сигналов, а после постановки tracking-сообщения играет короткая Morse-мелодия. На T1000-E tracking переключается шестью короткими нажатиями.
- **GPS UI**: при включении GPS вместо противоречивого `GPS off` показывается `Starting GPS...`.
- **Дата сборки**: CMake создаёт stamp при каждом запуске Ninja. Дата в firmware всегда соответствует локальной дате сборки; в пределах одного дня лишняя перекомпиляция не выполняется.
- **XIAO nRF52840 + Wio-SX1262 repeater**: сверена распиновка SX1262, добавлены заводские LoRa-настройки (867.935 MHz, 62.5 kHz, SF8, CR 4/8, duty cycle 50%), red heartbeat, blue TX и зелёная индикация зарядки. Зарядка — медленное зелёное мигание, полный заряд — постоянный зелёный. `~CHG` и выбор 100 mA взяты с BQ25101; в телеметрию передаётся номинальный ток зарядки.
- **Кривая XIAO**: 4.17 V = 100%. Для подстройки конкретной платы используйте `set adc.multiplier target <mV>` по показанию мультиметра.

## Образы прошивки

| Файл | Плата | Роль | Как прошивать |
|------|-------|------|---------------|
| `thinknode_m1-companion-2026-08-11-14d7d92.uf2` | ThinkNode M1 | Companion | UF2 через загрузчик платы |
| `thinknode_m5-companion-2026-08-11-14d7d92-merged.bin` | ThinkNode M5 | Companion | полный ESP32-S3 образ, записывать с `0x0` |
| `thinknode_m6-repeater-2026-08-11-14d7d92.uf2` | ThinkNode M6 | Repeater | UF2 через загрузчик платы |
| `t1000_e-companion-2026-08-11-14d7d92.uf2` | Seeed T1000-E | Companion | UF2 через загрузчик платы |
| `heltec_v3-companion-2026-08-11-14d7d92-merged.bin` | Heltec V3 | Companion | полный ESP32-S3 образ, записывать с `0x0` |
| `heltec_t114-companion-2026-08-11-14d7d92.uf2` | Heltec T114 | Companion | UF2 через загрузчик платы |
| `promicro_sx1262-companion-2026-08-11-14d7d92.uf2` | ProMicro SX1262 | Companion | UF2 через загрузчик платы |
| `xiao_nrf52840-wio_sx1262-repeater-2026-08-11-14d7d92.uf2` | XIAO nRF52840 + Wio-SX1262 | Repeater | UF2 через загрузчик платы |

## Использование

- Для ThinkNode M5 и Heltec V3 используйте только `-merged.bin`; он содержит MCUboot и signed app. Запись — с offset `0x0`.
- UF2-образы копируются на появившийся USB-накопитель загрузчика.
- Для XIAO при проверке напряжения калибруйте ADC по мультиметру: `set adc.multiplier target 4220` — пример для фактических 4.22 V.

## Проверка сборок

- ThinkNode M1 Companion: FLASH 394216 / 696320 (56.61%), RAM 174184 / 262144 (66.45%).
- ThinkNode M5 Companion: FLASH 585348 / 4194176 (13.96%), IRAM 63392 / 400640 (15.82%), DRAM 260056 / 384256 (67.68%).
- ThinkNode M6 Repeater: FLASH 220632 / 696320 (31.69%), RAM 83180 / 262144 (31.73%).
- T1000-E Companion: FLASH 354032 / 692224 (51.14%), RAM 170856 / 262144 (65.18%).
- XIAO nRF52840 Repeater: FLASH 205768 / 692224 (29.73%), RAM 81196 / 262144 (30.97%).
- Heltec V3 Companion: FLASH 582644 / 8388480 (6.95%), IRAM 63116 / 400640 (15.75%), DRAM 254768 / 384256 (66.30%).
- Heltec T114 Companion: FLASH 386252 / 696320 (55.47%), RAM 177384 / 262144 (67.67%).
- ProMicro SX1262 Companion: FLASH 383788 / 696320 (55.12%), RAM 169896 / 262144 (64.81%).

## SHA-256

```text
67b1631713042a1ee68c5d456569cdfb66338fddbeeed75618cb5e98eba81939  thinknode_m1-companion-2026-08-11-14d7d92.uf2
2e2cf6b8b12612790db8fb79472a1de974ab374f4f35c81b205014cca4e51754  thinknode_m5-companion-2026-08-11-14d7d92-merged.bin
741c0aa0067c3cc8b64450d16c40ae90420295503b540cbf70a01107032560eb  thinknode_m6-repeater-2026-08-11-14d7d92.uf2
40d0d7a87ff97e14312766356e851ea366ef01bec0e8ca615d2622812e9cf001  t1000_e-companion-2026-08-11-14d7d92.uf2
80272c944c4e06b92b858f4e02bd5cb8e93eac0993c180cf3acb32cbbc4bc6ad  heltec_v3-companion-2026-08-11-14d7d92-merged.bin
f28bd867e80d25ce5896964eb06755996600d7a41b22ba3d85872f72b2f96c89  heltec_t114-companion-2026-08-11-14d7d92.uf2
843b9718dbadff2a2f756b6bfd6da8b8a1f0eacdfe745ea7180b24d1f5827e06  promicro_sx1262-companion-2026-08-11-14d7d92.uf2
3243ed90c3c483dccfe5678b81ef77b5d8c4b29fc6db8e0357c9e21964aaa764  xiao_nrf52840-wio_sx1262-repeater-2026-08-11-14d7d92.uf2
```
