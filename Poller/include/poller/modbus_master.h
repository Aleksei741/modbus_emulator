#pragma once

#include "emulator/modbus_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace poller {

struct PduResult {
    bool ok = false;
    bool exception = false;
    std::uint8_t exceptionCode = 0;
    std::vector<std::uint8_t> payload;
    std::string message;
};

std::vector<std::uint8_t> BuildReadRequest(
    std::uint8_t slave,
    std::uint8_t function,
    std::uint16_t address,
    std::uint16_t quantity);

std::vector<std::uint8_t> BuildWriteCoilRequest(std::uint8_t slave, std::uint16_t address, bool value);
std::vector<std::uint8_t> BuildWriteRegisterRequest(std::uint8_t slave, std::uint16_t address, std::uint16_t value);

PduResult ParseResponse(
    const std::uint8_t* frame,
    std::size_t length,
    std::uint8_t expectedSlave,
    std::uint8_t expectedFunction);

bool UnpackBits(const std::vector<std::uint8_t>& payload, std::uint16_t quantity, std::vector<bool>& out);
bool UnpackWords(const std::vector<std::uint8_t>& payload, std::uint16_t quantity, std::vector<std::uint16_t>& out);

std::string HexDump(const std::uint8_t* data, std::size_t length);

std::uint8_t ReadFunction(emulator::TableKind kind);
std::uint16_t MaxQuantity(emulator::TableKind kind);

}  // namespace poller
