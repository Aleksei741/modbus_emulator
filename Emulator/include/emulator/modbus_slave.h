#pragma once

#include "emulator/modbus_types.h"
#include "emulator/register_store.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace emulator {

struct ProcessResult {
    std::optional<std::vector<std::uint8_t>> response;
    std::string log;
    bool storeChanged = false;
    TableKind changedTable = TableKind::HoldingRegisters;
};

class ModbusSlave {
public:
    explicit ModbusSlave(RegisterStore& store);

    ProcessResult process(const std::uint8_t* data, std::size_t length);

private:
    ProcessResult handle(std::uint8_t slave, std::uint8_t function, const std::uint8_t* pdu, std::size_t pduLen);
    ProcessResult readBits(std::uint8_t slave, std::uint8_t function, TableKind kind, const std::uint8_t* pdu, std::size_t pduLen);
    ProcessResult readWords(std::uint8_t slave, std::uint8_t function, TableKind kind, const std::uint8_t* pdu, std::size_t pduLen);
    ProcessResult writeSingleCoil(std::uint8_t slave, const std::uint8_t* pdu, std::size_t pduLen);
    ProcessResult writeSingleRegister(std::uint8_t slave, const std::uint8_t* pdu, std::size_t pduLen);
    ProcessResult writeMultipleCoils(std::uint8_t slave, const std::uint8_t* pdu, std::size_t pduLen);
    ProcessResult writeMultipleRegisters(std::uint8_t slave, const std::uint8_t* pdu, std::size_t pduLen);

    ProcessResult exception(std::uint8_t slave, std::uint8_t function, std::uint8_t code, std::string log) const;
    ProcessResult echo(std::uint8_t slave, const std::uint8_t* pdu, std::size_t pduLen, std::string log, bool changed, TableKind table) const;
    ProcessResult reply(std::uint8_t slave, std::vector<std::uint8_t> pdu, std::string log, bool changed, TableKind table) const;

    static std::uint16_t readU16(const std::uint8_t* p);

    RegisterStore& store_;
};

}  // namespace emulator
