# Shadow ZephCore v20260716.2 — hotfix ThinkNode M5

Дата: 2026-07-16

Этот hotfix заменяет только образ ThinkNode M5 из `shadow-v20260716.1`.

## Причина

В предыдущем merged-образе подписанное приложение было помещено по адресу `0x10000`. У платы ThinkNode M5 из этой ветки диапазон `0x10000..0x1ffff` занят системным разделом, а MCUboot ищет slot0 по адресу `0x20000`. Поэтому bootloader запускался, но сообщал:

```text
E: Bad image magic 0x0; Image=0
E: Unable to find bootable image
```

## Исправление

Новый полный образ сохраняет MCUboot по адресу `0x0` и помещает подписанное приложение в фактический slot0 по адресу `0x20000`. Таблица разделов и пользовательское хранилище не менялись.

Файл для прошивки по USB/serial с offset `0x0`:

- `thinknode_m5-esp32s3-procpu-companion-3ed0b86-merged.bin`

Не используйте M5-файл из `shadow-v20260716.1`; он отозван из релиза.

## Проверка

- Сборка ThinkNode M5 companion успешно завершена.
- Сгенерированные DTS для приложения и MCUboot содержат `slot0_partition` с `reg = <0x20000 ...>`.
- В итоговом merged-образе область `0x10000` пустая, а заголовок MCUboot image находится на `0x20000`.

## SHA-256

```text
8b19694a5c61e0f15a3428812729a39d1d99f4b95576bb95566ce48049191e32  thinknode_m5-esp32s3-procpu-companion-3ed0b86-merged.bin
```
