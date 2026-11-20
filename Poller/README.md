# Опросчик Modbus RTU

Мастер: циклически читает Coils, Discrete Inputs, Input Registers и Holding Registers по COM-порту. Интерфейс как у эмулятора.

- COM, скорость, чётность, stop-биты, период опроса
- **Подключить** запускает опрос включённых строк
- Последовательные адреса одного устройства группируются в один запрос
- Изменение значения Coil / Holding Register при подключении сразу пишется на шину (FC05 / FC06)
- Сессия: `%APPDATA%\modbus_rtu_poller\session.json`

Запуск: VS Code task **Poller: Run Windows**.
