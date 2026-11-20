#pragma once

#include "emulator/modbus_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace emulator {

inline const char* FunctionTitleRu(std::uint8_t function) {
    switch (function) {
    case kFcReadCoils:
        return "Чтение Coils";
    case kFcReadDiscreteInputs:
        return "Чтение Discrete Inputs";
    case kFcReadHoldingRegisters:
        return "Чтение Holding Registers";
    case kFcReadInputRegisters:
        return "Чтение Input Registers";
    case kFcWriteSingleCoil:
        return "Запись Coil";
    case kFcWriteSingleRegister:
        return "Запись Holding Register";
    case kFcWriteMultipleCoils:
        return "Запись нескольких Coils";
    case kFcWriteMultipleRegisters:
        return "Запись нескольких Holding Registers";
    default:
        return "Неизвестная функция";
    }
}

inline const char* ExceptionTitleRu(std::uint8_t code) {
    switch (code) {
    case kExcIllegalFunction:
        return "недопустимая функция";
    case kExcIllegalDataAddress:
        return "несуществующий адрес";
    case kExcIllegalDataValue:
        return "недопустимое значение";
    default:
        return "исключение";
    }
}

inline std::string AddressSpan(std::uint16_t start, std::uint16_t quantity) {
    if (quantity <= 1) {
        return std::to_string(start);
    }
    return std::to_string(start) + "–" + std::to_string(static_cast<unsigned>(start) + quantity - 1);
}

inline std::string CompressAddresses(const std::vector<std::uint16_t>& addresses) {
    if (addresses.empty()) {
        return {};
    }
    std::string out;
    std::size_t i = 0;
    std::size_t ranges = 0;
    while (i < addresses.size()) {
        const std::uint16_t begin = addresses[i];
        std::size_t j = i;
        while (j + 1 < addresses.size() && addresses[j + 1] == static_cast<std::uint16_t>(addresses[j] + 1)) {
            ++j;
        }
        if (ranges > 0) {
            out += ", ";
        }
        if (ranges >= 8) {
            out += "…";
            break;
        }
        if (begin == addresses[j]) {
            out += std::to_string(begin);
        } else {
            out += std::to_string(begin) + "–" + std::to_string(addresses[j]);
        }
        ++ranges;
        i = j + 1;
    }
    return out;
}

}  // namespace emulator
