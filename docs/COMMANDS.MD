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
