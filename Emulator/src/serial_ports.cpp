#include "emulator/serial_ports.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace emulator {
namespace {

int ComNumber(const std::string& port) {
    int number = 0;
    const char* p = port.c_str();
    if ((p[0] == 'C' || p[0] == 'c') && (p[1] == 'O' || p[1] == 'o') && (p[2] == 'M' || p[2] == 'm')) {
        p += 3;
    }
    while (*p >= '0' && *p <= '9') {
        number = number * 10 + (*p - '0');
        ++p;
    }
    return number;
}

}  // namespace

std::vector<std::string> ListSerialPorts() {
    std::vector<std::string> ports;
#ifdef _WIN32
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return ports;
    }

    for (DWORD index = 0;; ++index) {
        char name[256]{};
        char data[256]{};
        DWORD nameLen = sizeof(name);
        DWORD dataLen = sizeof(data);
        DWORD type = 0;
        const LONG status = RegEnumValueA(
            key,
            index,
            name,
            &nameLen,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(data),
            &dataLen);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status == ERROR_SUCCESS && type == REG_SZ && data[0] != '\0') {
            ports.emplace_back(data);
        }
    }
    RegCloseKey(key);
#endif

    std::sort(ports.begin(), ports.end(), [](const std::string& a, const std::string& b) {
        const int na = ComNumber(a);
        const int nb = ComNumber(b);
        if (na != nb) {
            return na < nb;
        }
        return a < b;
    });
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

std::string NormalizeComPort(const std::string& text) {
    std::string trimmed;
    trimmed.reserve(text.size());
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            trimmed.push_back(c);
        }
    }
    if (trimmed.empty()) {
        return {};
    }

    bool digitsOnly = true;
    for (char c : trimmed) {
        if (c < '0' || c > '9') {
            digitsOnly = false;
            break;
        }
    }
    if (digitsOnly) {
        return "COM" + trimmed;
    }

    if (trimmed.size() >= 3) {
        trimmed[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(trimmed[0])));
        trimmed[1] = static_cast<char>(std::toupper(static_cast<unsigned char>(trimmed[1])));
        trimmed[2] = static_cast<char>(std::toupper(static_cast<unsigned char>(trimmed[2])));
    }
    return trimmed;
}

std::string WindowsDevicePath(const std::string& port) {
    const std::string normalized = NormalizeComPort(port);
    if (normalized.empty()) {
        return {};
    }
    const int number = ComNumber(normalized);
    if (number >= 10) {
        return std::string("\\\\.\\") + normalized;
    }
    return normalized;
}

}  // namespace emulator
