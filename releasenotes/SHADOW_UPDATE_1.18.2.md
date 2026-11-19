# ShadoW: переход на ZephCore 1.18.2

Ветка `codex/update-1.18.2`. База — авторский тег `1.18.2-zephcore`.
В `dev` и публичные релизы пока не перенесено.

## Из основной прошивки

- **BLE OTA для ESP32-companion'ов** (ADR 0011, MCUmgr SMP over BLE).
  `start ota` открывает окно на 600 с, загрузка `<board>-update.bin` через
  nRF Connect Device Manager (iOS/Android), окно закрывается через `stop ota`.
  Доступно на Heltec V4/V4.3/V4 R8/Tracker V2, LilyGo T3S3, Meshnology W12,
  Station G2, XIAO ESP32-S3/C3. Heltec V3, Tracker, T-Lora C6, XIAO C6 —
  без BLE OTA (не хватает DRAM), `start ota` отвечает `Err: no OTA in this build`.
- **Пакеты без CRC не повторяются.** LoRa-заголовок может заявить отсутствие
  CRC — радио тогда пропускает проверку. MeshCore всегда шлёт CRC, такой пакет
  — шум или ошибочное чтение заголовка; теперь отбрасывается как битый CRC на
  всех типах радио (примерно 1 из 48000 приёмов на загруженном репиттере).
- **Пустые слоты контактов игнорируются.** Очищенный временный контакт мог
  совпасть с отправителем без ключа, и составное сообщение принималось как
  пришедшее от него (из upstream MeshCore).
- **Счётчики дублей repeater/room server** в статистике приложения
  останавливаются на 65535 вместо переполнения (из upstream MeshCore).
- **Zephyr обновлён** примерно на 1100 коммитов main, включая исправления
  Bluetooth host.
- `SECURITY.md` с политикой уязвимостей.

## Адаптация ShadoW

- **`-update.bin` возвращён в пакетку** (решение пользователя 2026-11-19):
  для ESP32-S3/C-series выпускаются оба образа — `…-merged.bin` (прошивка
  с `0x0`) и `…-update.bin` (signed app, payload для BLE/WiFi OTA).
  Правило «только merged» от 2026-11-06 отменено ради BLE OTA.
- Пункт меню обновления: nRF companion с экраном и CLI_DFU — `Enter DFU Mode`
  (как `start dfu`); ESP32 companion с BLE OTA — `BLE OTA` (открывает окно,
  узел продолжает работать); ESP32 без BLE OTA — пункта нет; headless — нет.
  Наш gate `UI_UPDATE_AVAILABLE` расширен на `CONFIG_ZEPHCORE_BLE_OTA`; авторский
  `CONFIG_SOC_SERIES_NRF52` в UI-слое заменён на capability-макрос, чтобы nRF54L
  не проваливался в «No OTA».
- Экран DFU показывает `Window open` пока окно BLE OTA открыто; подтверждение
  действием остаётся для nRF-пути. Мелодия входа в DFU играется только на
  nRF-пути (перезагрузка в загрузчик).
- `clkreboot` автором оставлен, у нас отсутствует по ADR 0010 — не возвращён.
- Страницы, порядок companion (… OFFGRID → Power off → Enter DFU), заставка
  `Tuned by ShadoW`, SOS/Fall/Tracking/NAV/геозона, bridge, настройки и профили
  плат сохранены. M3-MAMA остаётся персональным профилем.
- `start ota`/`stop ota` в remote CLI остаются запрещены (denylist).
- **BLE OTA паркует наши фичи.** Пока окно OTA открыто: живой SOS отменяется
  (с сообщением в `#sos`), Fall выключается, Tracking и Nav останавливаются,
  GPS гасится. Auto-shutdown в это время не работает (загрузка ест ток и не
  должна резать узел посреди апдейта). Закрытие окна возвращает Fall и GPS;
  Tracking/Nav не перезапускаются сами. Ждёт проверки на железе.

## Что осталось

- Host tests, `board_manifest check/docs`, полный diff review.
- `west update` под новый pin Zephyr, затем сборки.
- Сборка зоопарка и проверка на железе — отдельным шагом по подтверждению.
- Аппаратная приёмка (§15 SHADOW_UPDATE_GUIDE.md) не проводилась.
