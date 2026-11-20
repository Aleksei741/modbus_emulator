# Эмулятор Modbus RTU

Десктопное приложение на wxWidgets: slave-эмулятор Modbus RTU поверх COM-порта (Boost.Asio). Карта регистров и настройки порта сохраняются в JSON (nlohmann/json).

## Возможности

- COM-порт, скорость, чётность, stop-биты, кнопки **Подключить / Отключить**
- Вкладки: Coils, Discrete Inputs, Input Registers, Holding Registers
- До 65536 строк на вкладку: галочка «вкл», адрес устройства (1–247), адрес регистра (0–65535), значение
- Coils / Discrete Inputs — бит (`0/1`); Input / Holding Registers — `uint16` и hex
- Выключенный регистр не отвечает по шине (исключение Illegal Data Address, если устройство в целом есть)
- Функции: `01 02 03 04 05 06 15 16`, CRC-16, кадрирование по паузе T3.5
- Сохранение/загрузка JSON, автосохранение рядом с exe при выходе

## Сборка Windows x64

По аналогии с остальным репозиторием используется MSYS2 UCRT64.

1. Task **Toolchains: Windows x64 Setup MSYS2 UCRT64**
2. Task **Toolchains: Windows x64 Install UCRT64 Libraries**\
   Ставит `gcc`, `cmake`, `ninja`, Boost, wxWidgets 3.2, nlohmann-json.
3. Task **Project: Windows x64 Debug Build**\
   Результат: `${config:emulator.windowsX64.localBuildDir.debug}/modbus_rtu_emulator/`
4. Запуск: **Emulator: Run Windows**
5. Портативный пакет: **Project: Windows x64 Release Package**\
   `${config:emulator.windowsX64.packageDir}/modbus_rtu_emulator/`

В свежем клоне перед сборкой нужно подготовить архив MSYS2 и сгенерировать иконку: см. [инструкцию по сборке Windows x64](../toolchains/README.md#windows-x64).

## JSON

Файл по умолчанию: `modbus_rtu_emulator.json` рядом с exe.

```json
{
  "version": 1,
  "serial": { "port": "COM3", "baud": 9600, "parity": "none", "stop_bits": 1 },
  "coils": [{ "enabled": true, "device": 1, "address": 0, "value": false }],
  "discrete_inputs": [],
  "input_registers": [],
  "holding_registers": [{ "enabled": true, "device": 1, "address": 0, "value": 0 }]
}
```
