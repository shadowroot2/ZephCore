# ShadoW: переход на ZephCore 1.18.1

## Из основной прошивки

- `get/set tz.offset`: дробные часы с шагом 0.25, точный вывод `5.5`/`5.75`.
- Часовой пояс применяется к экрану репиттера и room server.
- TCP companion выбирает двухсекундный send timeout через Kconfig.
- Heartbeat в режимах `hb`/`off` не мигает на входящем сообщении.
- Zephyr обновлён до авторского pin из `west.yml`; исправления повторной
  инициализации GPS и внешней flash, актуализировано ESP32 PM описание.

## Адаптация ShadoW

- Старый `zc.tz_min` переносится в авторский float без обрезания половин
  и четвертей часа; явный UTC сохраняется. Новый JSON не дублирует timezone.
- Сохранены `tz`, GMT-подписи, button/joystick UI и E-Paper redraw.
  Двузначные дробные зоны на экране укладываются в 8 символов (`GMT+1245`).
- Входящие сообщения соблюдают `leds off` также на T-1000E; зарядная
  индикация и подтверждения действий остаются ShadoW.
- Остальной overlay, SOS/Fall/Tracking, bridge, настройки и профили плат
  сохранены относительно dev. M3-MAMA остаётся персональным профилем.

## Проверки

- Quick: **133 PASS** — 123 native cases и 10 Python harness tests.
- **15/15 pristine builds PASS**, ESP32-S3 с sysbuild:
  companion M1, M3, M5, T-1000E, Heltec V3, T114, Wio Tracker L1 и
  MeshTracker X1; repeater M6, RAK4631, XIAO nRF и Heltec V3;
  M6 bridge 868, Heltec V3 bridge 433; RAK4631 room server.
- Проверены 11 UF2 и 8 ESP32 BIN: формат, адреса, роль, версия, совпадение
  с build output; MCUboot integrity и размещение signed app в merged BIN.
- Все условные варианты CLI help в лимитах; 48 manifests без ошибок.
- У Heltec V3 companion DRAM 379784/380672 B (**99,77%**, свободно **888 B**),
  IRAM 93648 B, Flash 1030356 B. DRAM вырос на 248 B относительно 1.18.0.
- Существующие предупреждения Kconfig/DTS, unused и bounded remote reply
  truncation сохранены в логах. Новых предупреждений в сопоставимых
  релизных профилях не обнаружено.

Аппаратная проверка 1.18.1 ещё не проведена. Исходники и результаты сохранены
в `codex/update-1.18.1`; dev и публичный релиз не изменены.
