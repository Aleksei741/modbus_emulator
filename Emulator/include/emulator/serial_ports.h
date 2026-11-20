#pragma once

#include <string>
#include <vector>

namespace emulator {

std::vector<std::string> ListSerialPorts();
std::string NormalizeComPort(const std::string& text);
std::string WindowsDevicePath(const std::string& port);

}  // namespace emulator
