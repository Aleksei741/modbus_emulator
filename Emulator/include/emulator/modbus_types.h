#pragma once

#include <cstdint>
#include <string_view>

namespace emulator {

enum class TableKind {
    Coils,
    DiscreteInputs,
    InputRegisters,
    HoldingRegisters
};

inline constexpr std::uint16_t kMaxRegisterAddress = 65535;
inline constexpr std::uint8_t kMinDeviceAddress = 1;
inline constexpr std::uint8_t kMaxDeviceAddress = 247;
inline constexpr std::size_t kMaxRowsPerTable = 65536;
inline constexpr std::size_t kMaxAduSize = 256;

inline constexpr std::uint8_t kFcReadCoils = 0x01;
inline constexpr std::uint8_t kFcReadDiscreteInputs = 0x02;
inline constexpr std::uint8_t kFcReadHoldingRegisters = 0x03;
inline constexpr std::uint8_t kFcReadInputRegisters = 0x04;
inline constexpr std::uint8_t kFcWriteSingleCoil = 0x05;
inline constexpr std::uint8_t kFcWriteSingleRegister = 0x06;
inline constexpr std::uint8_t kFcWriteMultipleCoils = 0x0F;
inline constexpr std::uint8_t kFcWriteMultipleRegisters = 0x10;

inline constexpr std::uint8_t kExcIllegalFunction = 0x01;
inline constexpr std::uint8_t kExcIllegalDataAddress = 0x02;
inline constexpr std::uint8_t kExcIllegalDataValue = 0x03;

inline constexpr std::uint16_t kCoilOn = 0xFF00;
inline constexpr std::uint16_t kCoilOff = 0x0000;

inline bool IsBitTable(TableKind kind) {
    return kind == TableKind::Coils || kind == TableKind::DiscreteInputs;
}

inline bool IsWritableTable(TableKind kind) {
    return kind == TableKind::Coils || kind == TableKind::HoldingRegisters;
}

inline std::string_view TableTitle(TableKind kind) {
    switch (kind) {
    case TableKind::Coils:
        return "Coils";
    case TableKind::DiscreteInputs:
        return "Discrete Inputs";
    case TableKind::InputRegisters:
        return "Input Registers";
    case TableKind::HoldingRegisters:
        return "Holding Registers";
    }
    return "";
}

inline const char* TableJsonKey(TableKind kind) {
    switch (kind) {
    case TableKind::Coils:
        return "coils";
    case TableKind::DiscreteInputs:
        return "discrete_inputs";
    case TableKind::InputRegisters:
        return "input_registers";
    case TableKind::HoldingRegisters:
        return "holding_registers";
    }
    return "unknown";
}

struct RegisterCell {
    std::uint64_t id = 0;
    bool enabled = true;
    std::uint8_t device = 1;
    std::uint16_t address = 0;
    std::uint16_t value = 0;
};

inline constexpr TableKind kAllTables[] = {
    TableKind::Coils,
    TableKind::DiscreteInputs,
    TableKind::InputRegisters,
    TableKind::HoldingRegisters
};

}  // namespace emulator
