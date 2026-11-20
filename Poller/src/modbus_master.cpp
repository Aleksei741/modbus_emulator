#include "poller/modbus_master.h"

#include "emulator/modbus_crc.h"

#include <cstdio>
#include <string>

namespace poller {
namespace {

void PutU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

std::uint16_t GetU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

}  // namespace

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

std::uint8_t ReadFunction(emulator::TableKind kind) {
    switch (kind) {
    case emulator::TableKind::Coils:
        return emulator::kFcReadCoils;
    case emulator::TableKind::DiscreteInputs:
        return emulator::kFcReadDiscreteInputs;
    case emulator::TableKind::InputRegisters:
        return emulator::kFcReadInputRegisters;
    case emulator::TableKind::HoldingRegisters:
        return emulator::kFcReadHoldingRegisters;
    }
    return emulator::kFcReadHoldingRegisters;
}

std::uint16_t MaxQuantity(emulator::TableKind kind) {
    return emulator::IsBitTable(kind) ? 2000 : 125;
}

std::vector<std::uint8_t> BuildReadRequest(
    std::uint8_t slave,
    std::uint8_t function,
    std::uint16_t address,
    std::uint16_t quantity) {
    std::vector<std::uint8_t> frame;
    frame.reserve(8);
    frame.push_back(slave);
    frame.push_back(function);
    PutU16(frame, address);
    PutU16(frame, quantity);
    frame.resize(8);
    emulator::AppendCrc(frame.data(), 6);
    return frame;
}

std::vector<std::uint8_t> BuildWriteCoilRequest(std::uint8_t slave, std::uint16_t address, bool value) {
    std::vector<std::uint8_t> frame;
    frame.reserve(8);
    frame.push_back(slave);
    frame.push_back(emulator::kFcWriteSingleCoil);
    PutU16(frame, address);
    PutU16(frame, value ? emulator::kCoilOn : emulator::kCoilOff);
    frame.resize(8);
    emulator::AppendCrc(frame.data(), 6);
    return frame;
}

std::vector<std::uint8_t> BuildWriteRegisterRequest(std::uint8_t slave, std::uint16_t address, std::uint16_t value) {
    std::vector<std::uint8_t> frame;
    frame.reserve(8);
    frame.push_back(slave);
    frame.push_back(emulator::kFcWriteSingleRegister);
    PutU16(frame, address);
    PutU16(frame, value);
    frame.resize(8);
    emulator::AppendCrc(frame.data(), 6);
    return frame;
}

PduResult ParseResponse(
    const std::uint8_t* frame,
    std::size_t length,
    std::uint8_t expectedSlave,
    std::uint8_t expectedFunction) {
    PduResult result;
    if (frame == nullptr || length < 5) {
        result.message = "short frame";
        return result;
    }
    if (frame[0] != expectedSlave) {
        result.message = "unexpected slave";
        return result;
    }
    const std::uint8_t function = frame[1];
    if ((function & 0x80U) != 0) {
        result.exception = true;
        result.exceptionCode = length >= 3 ? frame[2] : 0;
        result.message = "exception " + std::to_string(result.exceptionCode);
        return result;
    }
    if (function != expectedFunction) {
        result.message = "unexpected function";
        return result;
    }
    result.ok = true;
    result.payload.assign(frame + 1, frame + length - 2);
    return result;
}

bool UnpackBits(const std::vector<std::uint8_t>& payload, std::uint16_t quantity, std::vector<bool>& out) {
    if (payload.size() < 2) {
        return false;
    }
    const std::uint8_t byteCount = payload[1];
    if (byteCount != static_cast<std::uint8_t>((quantity + 7) / 8) || payload.size() < static_cast<std::size_t>(2 + byteCount)) {
        return false;
    }
    out.assign(quantity, false);
    for (std::uint16_t i = 0; i < quantity; ++i) {
        out[i] = (payload[2 + (i / 8)] & (1U << (i % 8))) != 0;
    }
    return true;
}

bool UnpackWords(const std::vector<std::uint8_t>& payload, std::uint16_t quantity, std::vector<std::uint16_t>& out) {
    if (payload.size() < 2) {
        return false;
    }
    const std::uint8_t byteCount = payload[1];
    if (byteCount != static_cast<std::uint8_t>(quantity * 2) || payload.size() < static_cast<std::size_t>(2 + byteCount)) {
        return false;
    }
    out.resize(quantity);
    for (std::uint16_t i = 0; i < quantity; ++i) {
        out[i] = GetU16(payload.data() + 2 + i * 2);
    }
    return true;
}

}  // namespace poller
