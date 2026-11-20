#pragma once

#include <cstddef>
#include <cstdint>

namespace emulator {

std::uint16_t ModbusCrc16(const std::uint8_t* data, std::size_t length);

bool FrameCrcIsValid(const std::uint8_t* data, std::size_t length);

void AppendCrc(std::uint8_t* data, std::size_t lengthWithoutCrc);

// 0 = ещё рано решать. Иначе ожидаемая длина ADU (запрос или ответ).
std::size_t ExpectedRtuAduLength(const std::uint8_t* data, std::size_t length);
bool LooksLikeReadResponse(const std::uint8_t* data, std::size_t length);

}  // namespace emulator
