#include "emulator/modbus_slave.h"

#include "emulator/modbus_crc.h"
#include "emulator/modbus_log.h"

#include <cstdio>
#include <sstream>

namespace emulator {
namespace {

std::string HexDump(const std::uint8_t* data, std::size_t length) {
    std::string out;
    out.reserve(length * 3);
    char buf[4];
    for (std::size_t i = 0; i < length; ++i) {
        std::snprintf(buf, sizeof(buf), "%02X", static_cast<unsigned>(data[i]));
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += buf;
    }
    return out;
}

std::string DeviceLabel(std::uint8_t slave) {
    if (slave == 0) {
        return "широковещательно";
    }
    return "устройство " + std::to_string(slave);
}

std::string DescribeRange(std::uint8_t function, std::uint8_t slave, std::uint16_t start, std::uint16_t quantity) {
    std::ostringstream line;
    line << FunctionTitleRu(function) << ": " << DeviceLabel(slave);
    if (quantity <= 1) {
        line << ", адрес " << start;
    } else {
        line << ", адреса " << AddressSpan(start, quantity) << " (" << quantity << " шт.)";
    }
    return line.str();
}

std::string DescribeMissing(
    RegisterStore& store,
    TableKind kind,
    std::uint8_t function,
    std::uint8_t slave,
    std::uint16_t start,
    std::uint16_t quantity) {
    std::ostringstream line;
    line << DescribeRange(function, slave, start, quantity);
    const auto missing = store.missingAddresses(kind, slave, start, quantity);
    if (missing.empty()) {
        line << " — адреса выходят за 0–65535";
    } else if (missing.size() == quantity) {
        line << " — в эмуляторе нет этих " << std::string(TableTitle(kind)) << ": " << CompressAddresses(missing);
    } else {
        line << " — в эмуляторе нет " << std::string(TableTitle(kind)) << " " << CompressAddresses(missing);
    }
    line << " → исключение 02 (" << ExceptionTitleRu(kExcIllegalDataAddress) << ")";
    return line.str();
}

const char* FunctionHint(std::uint8_t function) {
    if ((function & 0x80U) != 0) {
        return "исключение (старший бит функции)";
    }
    switch (function) {
    case 0x01: case 0x02: case 0x03: case 0x04:
    case 0x05: case 0x06: case 0x0F: case 0x10:
        return FunctionTitleRu(function);
    default:
        return "не стандартная команда Modbus RTU";
    }
}

std::string DescribeBadCrcFrame(RegisterStore& store, const std::uint8_t* data, std::size_t length) {
    std::ostringstream line;
    const std::uint8_t slave = data[0];
    const std::uint8_t function = data[1];
    const std::size_t expected = ExpectedRtuAduLength(data, length);
    const bool ours = slave == 0 || store.deviceExists(slave);

    line << "Кадр отброшен: неверная CRC. Как запрос: устройство " << static_cast<int>(slave)
         << ", функция 0x";
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%02X", function);
    line << hex << " (" << FunctionHint(function) << ")";

    if (!ours) {
        line << " — это устройство эмулятор не обслуживает";
    }
    if (expected > length) {
        line << ". Неполный ADU: пришло " << length << " из " << expected << " байт";
    }

    // Typical desync: leftover address byte + someone else's FC03 response.
    if (length >= 5) {
        const std::uint8_t maybeSlave = data[1];
        const std::uint8_t maybeFunc = data[2];
        if (maybeFunc == kFcReadHoldingRegisters || maybeFunc == kFcReadInputRegisters
            || maybeFunc == kFcReadCoils || maybeFunc == kFcReadDiscreteInputs) {
            const std::uint8_t byteCount = data[3];
            if (byteCount >= 1 && byteCount <= 250) {
                line << ". Похоже на сдвиг кадра: без первого байта это ответ устройства "
                     << static_cast<int>(maybeSlave) << " (" << FunctionTitleRu(maybeFunc)
                     << ", " << static_cast<int>(byteCount) << " байт данных), не команда 0x" << hex;
            }
        }
    }
    return line.str();
}

}  // namespace

ModbusSlave::ModbusSlave(RegisterStore& store) : store_(store) {}

std::uint16_t ModbusSlave::readU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

ProcessResult ModbusSlave::exception(std::uint8_t slave, std::uint8_t function, std::uint8_t code, std::string log) const {
    ProcessResult result;
    if (slave == 0) {
        result.log = std::move(log);
        return result;
    }
    std::vector<std::uint8_t> frame(5);
    frame[0] = slave;
    frame[1] = static_cast<std::uint8_t>(function | 0x80U);
    frame[2] = code;
    AppendCrc(frame.data(), 3);
    result.response = std::move(frame);
    result.log = std::move(log);
    return result;
}

ProcessResult ModbusSlave::echo(
    std::uint8_t slave,
    const std::uint8_t* pdu,
    std::size_t pduLen,
    std::string log,
    bool changed,
    TableKind table) const {
    std::vector<std::uint8_t> body(pdu, pdu + pduLen);
    return reply(slave, std::move(body), std::move(log), changed, table);
}

ProcessResult ModbusSlave::reply(
    std::uint8_t slave,
    std::vector<std::uint8_t> pdu,
    std::string log,
    bool changed,
    TableKind table) const {
    ProcessResult result;
    result.log = std::move(log);
    result.storeChanged = changed;
    result.changedTable = table;
    if (slave == 0) {
        return result;
    }
    std::vector<std::uint8_t> frame;
    frame.reserve(pdu.size() + 3);
    frame.push_back(slave);
    frame.insert(frame.end(), pdu.begin(), pdu.end());
    frame.resize(frame.size() + 2);
    AppendCrc(frame.data(), frame.size() - 2);
    result.response = std::move(frame);
    return result;
}

ProcessResult ModbusSlave::process(const std::uint8_t* data, std::size_t length) {
    ProcessResult result;
    if (data == nullptr || length < 4 || length > kMaxAduSize) {
        result.log = "Кадр отброшен: слишком короткий или слишком длинный";
        if (data != nullptr && length > 0) {
            result.log += " | RX [" + HexDump(data, length) + "]";
        }
        return result;
    }
    const std::uint8_t slave = data[0];
    const std::uint8_t function = data[1];

    if (LooksLikeReadResponse(data, length)) {
        result.log = "Игнор: это ответ устройства " + std::to_string(slave)
            + " мастеру, не запрос к эмулятору | RX [" + HexDump(data, length) + "]";
        return result;
    }

    if (!FrameCrcIsValid(data, length)) {
        result.log = DescribeBadCrcFrame(store_, data, length)
            + " | RX [" + HexDump(data, length) + "]";
        return result;
    }

    const std::uint8_t* pdu = data + 1;
    const std::size_t pduLen = length - 3;

    if (slave != 0 && !store_.deviceExists(slave)) {
        result.log = std::string(FunctionTitleRu(function)) + ": " + DeviceLabel(slave)
            + " — в эмуляторе нет включённых регистров этого устройства, ответ не отправлен"
            + " | RX [" + HexDump(data, length) + "]";
        return result;
    }

    auto handled = handle(slave, function, pdu, pduLen);
    handled.log += " | RX [" + HexDump(data, length) + "]";
    if (handled.response && !handled.response->empty()) {
        handled.log += " | TX [" + HexDump(handled.response->data(), handled.response->size()) + "]";
    }
    return handled;
}

ProcessResult ModbusSlave::handle(
    std::uint8_t slave,
    std::uint8_t function,
    const std::uint8_t* pdu,
    std::size_t pduLen) {
    switch (function) {
    case kFcReadCoils:
        if (slave == 0) {
            ProcessResult ignored;
            ignored.log = "Чтение по широковещательному адресу 0 проигнорировано";
            return ignored;
        }
        return readBits(slave, function, TableKind::Coils, pdu, pduLen);
    case kFcReadDiscreteInputs:
        if (slave == 0) {
            ProcessResult ignored;
            ignored.log = "Чтение по широковещательному адресу 0 проигнорировано";
            return ignored;
        }
        return readBits(slave, function, TableKind::DiscreteInputs, pdu, pduLen);
    case kFcReadHoldingRegisters:
        if (slave == 0) {
            ProcessResult ignored;
            ignored.log = "Чтение по широковещательному адресу 0 проигнорировано";
            return ignored;
        }
        return readWords(slave, function, TableKind::HoldingRegisters, pdu, pduLen);
    case kFcReadInputRegisters:
        if (slave == 0) {
            ProcessResult ignored;
            ignored.log = "Чтение по широковещательному адресу 0 проигнорировано";
            return ignored;
        }
        return readWords(slave, function, TableKind::InputRegisters, pdu, pduLen);
    case kFcWriteSingleCoil:
        return writeSingleCoil(slave, pdu, pduLen);
    case kFcWriteSingleRegister:
        return writeSingleRegister(slave, pdu, pduLen);
    case kFcWriteMultipleCoils:
        return writeMultipleCoils(slave, pdu, pduLen);
    case kFcWriteMultipleRegisters:
        return writeMultipleRegisters(slave, pdu, pduLen);
    default:
        return exception(
            slave,
            function,
            kExcIllegalFunction,
            std::string(FunctionTitleRu(function)) + " (код " + std::to_string(function) + "): "
                + DeviceLabel(slave) + " → исключение 01 (" + ExceptionTitleRu(kExcIllegalFunction) + ")");
    }
}

ProcessResult ModbusSlave::readBits(
    std::uint8_t slave,
    std::uint8_t function,
    TableKind kind,
    const std::uint8_t* pdu,
    std::size_t pduLen) {
    if (pduLen < 5) {
        return exception(slave, function, kExcIllegalDataValue,
            std::string(FunctionTitleRu(function)) + ": " + DeviceLabel(slave)
                + " — слишком короткий запрос → исключение 03 (" + ExceptionTitleRu(kExcIllegalDataValue) + ")");
    }
    const std::uint16_t start = readU16(pdu + 1);
    const std::uint16_t quantity = readU16(pdu + 3);
    if (quantity < 1 || quantity > 2000) {
        return exception(slave, function, kExcIllegalDataValue,
            DescribeRange(function, slave, start, quantity)
                + " — недопустимое количество → исключение 03 (" + ExceptionTitleRu(kExcIllegalDataValue) + ")");
    }
    if (static_cast<std::uint32_t>(start) + quantity - 1 > kMaxRegisterAddress) {
        return exception(slave, function, kExcIllegalDataAddress,
            DescribeRange(function, slave, start, quantity)
                + " — адреса выходят за 0–65535 → исключение 02 (" + ExceptionTitleRu(kExcIllegalDataAddress) + ")");
    }
    if (!store_.rangePresent(kind, slave, start, quantity)) {
        return exception(slave, function, kExcIllegalDataAddress,
            DescribeMissing(store_, kind, function, slave, start, quantity));
    }

    const std::uint8_t byteCount = static_cast<std::uint8_t>((quantity + 7) / 8);
    std::vector<std::uint8_t> pduOut(2 + byteCount, 0);
    pduOut[0] = function;
    pduOut[1] = byteCount;
    for (std::uint16_t i = 0; i < quantity; ++i) {
        const auto value = store_.readBit(kind, slave, static_cast<std::uint16_t>(start + i));
        if (value.value_or(false)) {
            pduOut[2 + (i / 8)] |= static_cast<std::uint8_t>(1U << (i % 8));
        }
    }
    return reply(slave, std::move(pduOut),
        DescribeRange(function, slave, start, quantity) + " → ответ ок", false, kind);
}

ProcessResult ModbusSlave::readWords(
    std::uint8_t slave,
    std::uint8_t function,
    TableKind kind,
    const std::uint8_t* pdu,
    std::size_t pduLen) {
    if (pduLen < 5) {
        return exception(slave, function, kExcIllegalDataValue,
            std::string(FunctionTitleRu(function)) + ": " + DeviceLabel(slave)
                + " — слишком короткий запрос → исключение 03 (" + ExceptionTitleRu(kExcIllegalDataValue) + ")");
    }
    const std::uint16_t start = readU16(pdu + 1);
    const std::uint16_t quantity = readU16(pdu + 3);
    if (quantity < 1 || quantity > 125) {
        return exception(slave, function, kExcIllegalDataValue,
            DescribeRange(function, slave, start, quantity)
                + " — недопустимое количество → исключение 03 (" + ExceptionTitleRu(kExcIllegalDataValue) + ")");
    }
    if (static_cast<std::uint32_t>(start) + quantity - 1 > kMaxRegisterAddress) {
        return exception(slave, function, kExcIllegalDataAddress,
            DescribeRange(function, slave, start, quantity)
                + " — адреса выходят за 0–65535 → исключение 02 (" + ExceptionTitleRu(kExcIllegalDataAddress) + ")");
    }
    if (!store_.rangePresent(kind, slave, start, quantity)) {
        return exception(slave, function, kExcIllegalDataAddress,
            DescribeMissing(store_, kind, function, slave, start, quantity));
    }

    const std::uint8_t byteCount = static_cast<std::uint8_t>(quantity * 2);
    std::vector<std::uint8_t> pduOut(2 + byteCount);
    pduOut[0] = function;
    pduOut[1] = byteCount;
    for (std::uint16_t i = 0; i < quantity; ++i) {
        const auto value = store_.readWord(kind, slave, static_cast<std::uint16_t>(start + i)).value_or(0);
        pduOut[2 + i * 2] = static_cast<std::uint8_t>(value >> 8);
        pduOut[3 + i * 2] = static_cast<std::uint8_t>(value & 0xFF);
    }
    return reply(slave, std::move(pduOut),
        DescribeRange(function, slave, start, quantity) + " → ответ ок", false, kind);
}

ProcessResult ModbusSlave::writeSingleCoil(std::uint8_t slave, const std::uint8_t* pdu, std::size_t pduLen) {
    if (pduLen < 5) {
        return exception(slave, kFcWriteSingleCoil, kExcIllegalDataValue,
            std::string(FunctionTitleRu(kFcWriteSingleCoil)) + ": " + DeviceLabel(slave)
                + " — слишком короткий запрос → исключение 03");
    }
    const std::uint16_t address = readU16(pdu + 1);
    const std::uint16_t raw = readU16(pdu + 3);
    if (raw != kCoilOn && raw != kCoilOff) {
        return exception(slave, kFcWriteSingleCoil, kExcIllegalDataValue,
            DescribeRange(kFcWriteSingleCoil, slave, address, 1)
                + " — значение должно быть 0x0000 или 0xFF00, пришло "
                + std::to_string(raw) + " → исключение 03 (" + ExceptionTitleRu(kExcIllegalDataValue) + ")");
    }
    const bool value = raw == kCoilOn;

    if (slave == 0) {
        store_.writeBitBroadcast(TableKind::Coils, address, value);
        return echo(slave, pdu, 5,
            DescribeRange(kFcWriteSingleCoil, 0, address, 1) + ", значение " + (value ? "1" : "0") + " → выполнено без ответа",
            true, TableKind::Coils);
    }
    if (!store_.writeBit(TableKind::Coils, slave, address, value)) {
        return exception(slave, kFcWriteSingleCoil, kExcIllegalDataAddress,
            DescribeMissing(store_, TableKind::Coils, kFcWriteSingleCoil, slave, address, 1));
    }
    return echo(slave, pdu, 5,
        DescribeRange(kFcWriteSingleCoil, slave, address, 1) + ", значение " + (value ? "1" : "0") + " → ответ ок",
        true, TableKind::Coils);
}

ProcessResult ModbusSlave::writeSingleRegister(std::uint8_t slave, const std::uint8_t* pdu, std::size_t pduLen) {
    if (pduLen < 5) {
        return exception(slave, kFcWriteSingleRegister, kExcIllegalDataValue,
            std::string(FunctionTitleRu(kFcWriteSingleRegister)) + ": " + DeviceLabel(slave)
                + " — слишком короткий запрос → исключение 03");
    }
    const std::uint16_t address = readU16(pdu + 1);
    const std::uint16_t value = readU16(pdu + 3);

    if (slave == 0) {
        store_.writeWordBroadcast(TableKind::HoldingRegisters, address, value);
        return echo(slave, pdu, 5,
            DescribeRange(kFcWriteSingleRegister, 0, address, 1) + ", значение " + std::to_string(value)
                + " → выполнено без ответа",
            true, TableKind::HoldingRegisters);
    }
    if (!store_.writeWord(TableKind::HoldingRegisters, slave, address, value)) {
        return exception(slave, kFcWriteSingleRegister, kExcIllegalDataAddress,
            DescribeMissing(store_, TableKind::HoldingRegisters, kFcWriteSingleRegister, slave, address, 1));
    }
    return echo(slave, pdu, 5,
        DescribeRange(kFcWriteSingleRegister, slave, address, 1) + ", значение " + std::to_string(value) + " → ответ ок",
        true, TableKind::HoldingRegisters);
}

ProcessResult ModbusSlave::writeMultipleCoils(std::uint8_t slave, const std::uint8_t* pdu, std::size_t pduLen) {
    if (pduLen < 6) {
        return exception(slave, kFcWriteMultipleCoils, kExcIllegalDataValue,
            std::string(FunctionTitleRu(kFcWriteMultipleCoils)) + ": " + DeviceLabel(slave)
                + " — слишком короткий запрос → исключение 03");
    }
    const std::uint16_t start = readU16(pdu + 1);
    const std::uint16_t quantity = readU16(pdu + 3);
    const std::uint8_t byteCount = pdu[5];
    if (quantity < 1 || quantity > 1968) {
        return exception(slave, kFcWriteMultipleCoils, kExcIllegalDataValue,
            DescribeRange(kFcWriteMultipleCoils, slave, start, quantity)
                + " — недопустимое количество → исключение 03");
    }
    if (byteCount != static_cast<std::uint8_t>((quantity + 7) / 8) || pduLen < static_cast<std::size_t>(6 + byteCount)) {
        return exception(slave, kFcWriteMultipleCoils, kExcIllegalDataValue,
            DescribeRange(kFcWriteMultipleCoils, slave, start, quantity)
                + " — неверное число байт данных → исключение 03");
    }
    if (static_cast<std::uint32_t>(start) + quantity - 1 > kMaxRegisterAddress) {
        return exception(slave, kFcWriteMultipleCoils, kExcIllegalDataAddress,
            DescribeRange(kFcWriteMultipleCoils, slave, start, quantity)
                + " — адреса выходят за 0–65535 → исключение 02");
    }

    auto apply = [&](std::uint8_t device) {
        if (!store_.rangePresent(TableKind::Coils, device, start, quantity)) {
            return false;
        }
        for (std::uint16_t i = 0; i < quantity; ++i) {
            const bool value = (pdu[6 + (i / 8)] & (1U << (i % 8))) != 0;
            store_.writeBit(TableKind::Coils, device, static_cast<std::uint16_t>(start + i), value);
        }
        return true;
    };

    if (slave == 0) {
        for (std::uint8_t device = kMinDeviceAddress; device <= kMaxDeviceAddress; ++device) {
            if (store_.deviceExists(device)) {
                apply(device);
            }
        }
        std::vector<std::uint8_t> ack{kFcWriteMultipleCoils, pdu[1], pdu[2], pdu[3], pdu[4]};
        return reply(slave, std::move(ack),
            DescribeRange(kFcWriteMultipleCoils, 0, start, quantity) + " → выполнено без ответа",
            true, TableKind::Coils);
    }

    if (!apply(slave)) {
        return exception(slave, kFcWriteMultipleCoils, kExcIllegalDataAddress,
            DescribeMissing(store_, TableKind::Coils, kFcWriteMultipleCoils, slave, start, quantity));
    }
    std::vector<std::uint8_t> ack{kFcWriteMultipleCoils, pdu[1], pdu[2], pdu[3], pdu[4]};
    return reply(slave, std::move(ack),
        DescribeRange(kFcWriteMultipleCoils, slave, start, quantity) + " → ответ ок", true, TableKind::Coils);
}

ProcessResult ModbusSlave::writeMultipleRegisters(std::uint8_t slave, const std::uint8_t* pdu, std::size_t pduLen) {
    if (pduLen < 6) {
        return exception(slave, kFcWriteMultipleRegisters, kExcIllegalDataValue,
            std::string(FunctionTitleRu(kFcWriteMultipleRegisters)) + ": " + DeviceLabel(slave)
                + " — слишком короткий запрос → исключение 03");
    }
    const std::uint16_t start = readU16(pdu + 1);
    const std::uint16_t quantity = readU16(pdu + 3);
    const std::uint8_t byteCount = pdu[5];
    if (quantity < 1 || quantity > 123) {
        return exception(slave, kFcWriteMultipleRegisters, kExcIllegalDataValue,
            DescribeRange(kFcWriteMultipleRegisters, slave, start, quantity)
                + " — недопустимое количество → исключение 03");
    }
    if (byteCount != static_cast<std::uint8_t>(quantity * 2) || pduLen < static_cast<std::size_t>(6 + byteCount)) {
        return exception(slave, kFcWriteMultipleRegisters, kExcIllegalDataValue,
            DescribeRange(kFcWriteMultipleRegisters, slave, start, quantity)
                + " — неверное число байт данных → исключение 03");
    }
    if (static_cast<std::uint32_t>(start) + quantity - 1 > kMaxRegisterAddress) {
        return exception(slave, kFcWriteMultipleRegisters, kExcIllegalDataAddress,
            DescribeRange(kFcWriteMultipleRegisters, slave, start, quantity)
                + " — адреса выходят за 0–65535 → исключение 02");
    }

    auto apply = [&](std::uint8_t device) {
        if (!store_.rangePresent(TableKind::HoldingRegisters, device, start, quantity)) {
            return false;
        }
        for (std::uint16_t i = 0; i < quantity; ++i) {
            const std::uint16_t value = readU16(pdu + 6 + i * 2);
            store_.writeWord(TableKind::HoldingRegisters, device, static_cast<std::uint16_t>(start + i), value);
        }
        return true;
    };

    if (slave == 0) {
        for (std::uint8_t device = kMinDeviceAddress; device <= kMaxDeviceAddress; ++device) {
            if (store_.deviceExists(device)) {
                apply(device);
            }
        }
        std::vector<std::uint8_t> ack{kFcWriteMultipleRegisters, pdu[1], pdu[2], pdu[3], pdu[4]};
        return reply(slave, std::move(ack),
            DescribeRange(kFcWriteMultipleRegisters, 0, start, quantity) + " → выполнено без ответа",
            true, TableKind::HoldingRegisters);
    }

    if (!apply(slave)) {
        return exception(slave, kFcWriteMultipleRegisters, kExcIllegalDataAddress,
            DescribeMissing(store_, TableKind::HoldingRegisters, kFcWriteMultipleRegisters, slave, start, quantity));
    }
    std::vector<std::uint8_t> ack{kFcWriteMultipleRegisters, pdu[1], pdu[2], pdu[3], pdu[4]};
    return reply(slave, std::move(ack),
        DescribeRange(kFcWriteMultipleRegisters, slave, start, quantity) + " → ответ ок",
        true, TableKind::HoldingRegisters);
}

}  // namespace emulator
