## 9. Сборки, проверки и исключённые эксперименты

- Имена артефактов имеют постоянный формат `<board>-<role>-<commit>.<ext>`,
  например `t1000_e-companion-a4733b1e6.uf2`; дата, версия и произвольные
  суффиксы не добавляются.
- Для ESP32-S3/C-series выдаются полный `-merged.bin` с MCUboot и подписанным
  приложением для прошивки с `0x0`, а также `-update.bin`. Обычный неподписанный
  `.bin` и HEX не публикуются.
- Для поддерживаемых nRF-плат выдаётся UF2; HEX создаётся только когда он
  действительно является форматом конкретной платы.
- На 26 октября 2026 года после правки беззвучного remote SOS/Tracking успешно
  собраны companion для M1, M3, T-1000E и Heltec V3; host tests — 126 PASS.
  Успешная сборка не равна аппаратной проверке всех сценариев.
- Love Edition для M1/M5 (персональные надписи, страница-посвящение и мелодия по нотам) была отдельным экспериментом и удалена по запросу пользователя. В стандартную прошивку не входит.
- Нельзя считать историческое подтверждение старого образа подтверждением любой новой сборки. Особенно это относится к длительной стабильности BLE bridge, новым платам и эвристике Fall.

## 10. Где находится реализация

| Область | Основные файлы |
| --- | --- |
| SOS, Fall-сценарий, Tracking, companion CLI | `zephcore/src/main_companion.cpp`, `zephcore/helpers/CompanionCLIAudio.h` |
| Companion BLE/app и remote help | `zephcore/helpers/CompanionCLIHelp.h` |
| vContact notifications-only | `zephcore/app/VContact.cpp` |
| Алгоритм падения | `zephcore/adapters/sensors/FallDetector.cpp` |
| Bridge, фильтрация, задержки и CLI | `zephcore/app/RepeaterBridge.cpp` |
| BLE / ESP-NOW | `zephcore/app/BLERepeaterBridge.cpp`, `zephcore/app/ESPNowBridge.cpp` |
| Батарейные уведомления репиттера | `zephcore/app/RepeaterMesh.cpp`, `zephcore/helpers/BatteryAlertDefaults.h` |
| LED и зарядная индикация | `zephcore/helpers/ui/ui_common.c` |
| Button UI: страницы и действия | `zephcore/helpers/ui-button/ui_pages.c`, `zephcore/helpers/ui-button/ui_task.c` |
| Общий CLI и порядок help | `zephcore/helpers/CommonCLI.cpp`, `zephcore/helpers/LocalCLIHelp.h`, `zephcore/helpers/RepeaterCLIHelp.h` |
| Параметры и миграция | `zephcore/helpers/NodePrefs.h`, адаптеры datastore |
| Платы, пины, кривые батареи | `zephcore/boards/` |
