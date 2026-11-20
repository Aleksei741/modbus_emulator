#include "emulator/modbus_crc.h"
#include "emulator/modbus_types.h"

namespace emulator {

std::uint16_t ModbusCrc16(const std::uint8_t* data, std::size_t length) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001U) != 0) {
                crc = static_cast<std::uint16_t>((crc >> 1) ^ 0xA001U);
            } else {
                crc = static_cast<std::uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

bool FrameCrcIsValid(const std::uint8_t* data, std::size_t length) {
    if (data == nullptr || length < 4) {
        return false;
    }
    const std::uint16_t expected = ModbusCrc16(data, length - 2);
    const std::uint16_t actual = static_cast<std::uint16_t>(data[length - 2] | (static_cast<std::uint16_t>(data[length - 1]) << 8));
    return expected == actual;
}

void AppendCrc(std::uint8_t* data, std::size_t lengthWithoutCrc) {
    const std::uint16_t crc = ModbusCrc16(data, lengthWithoutCrc);
    data[lengthWithoutCrc] = static_cast<std::uint8_t>(crc & 0xFF);
    data[lengthWithoutCrc + 1] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
}

std::size_t ExpectedRtuAduLength(const std::uint8_t* data, std::size_t length) {
    if (data == nullptr || length < 2) {
        return 0;
    }
    const std::uint8_t rawFunction = data[1];
    if ((rawFunction & 0x80U) != 0) {
        return 5;
    }
    switch (rawFunction) {
    case 0x05:
    case 0x06:
        return 8;
    case 0x0F:
    case 0x10:
        if (length < 7) {
            return 0;
        }
        return 9U + data[6];
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04: {
        if (length >= 8 && FrameCrcIsValid(data, 8)) {
            return 8;
        }
        if (length >= 3) {
            const std::uint8_t byteCount = data[2];
            if (byteCount >= 1 && byteCount <= 250) {
                const std::size_t asResponse = 5U + byteCount;
                if (asResponse <= kMaxAduSize) {
                    if (length >= asResponse && FrameCrcIsValid(data, asResponse)) {
                        return asResponse;
                    }
                    if (length < 8 || !FrameCrcIsValid(data, 8)) {
                        return asResponse;
                    }
                }
            }
        }
        return 8;
    }
    default:
        return 0;
    }
}

bool LooksLikeReadResponse(const std::uint8_t* data, std::size_t length) {
    if (data == nullptr || length < 5) {
        return false;
    }
    const std::uint8_t function = data[1];
    if (function != 0x01 && function != 0x02 && function != 0x03 && function != 0x04) {
        return false;
    }
    const std::uint8_t byteCount = data[2];
    if (byteCount < 1 || byteCount > 250) {
        return false;
    }
    if (length != 5U + byteCount) {
        return false;
    }
    if ((function == 0x03 || function == 0x04) && (byteCount % 2U) != 0) {
        return false;
    }
    return FrameCrcIsValid(data, length);
}

}  // namespace emulator
