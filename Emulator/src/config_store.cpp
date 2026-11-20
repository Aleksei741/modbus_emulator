#include "emulator/config_store.h"

#include "emulator/serial_ports.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <wchar.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace emulator {
namespace {

std::string gAppFolder = "modbus_rtu_emulator";
std::string gAppFile = "modbus_rtu_emulator.json";


using nlohmann::json;

const char* ParityName(ParityMode mode) {
    switch (mode) {
    case ParityMode::Even:
        return "even";
    case ParityMode::Odd:
        return "odd";
    case ParityMode::None:
    default:
        return "none";
    }
}

ParityMode ParseParity(const std::string& value) {
    if (value == "even") {
        return ParityMode::Even;
    }
    if (value == "odd") {
        return ParityMode::Odd;
    }
    return ParityMode::None;
}

json CellsToJson(const std::vector<RegisterCell>& cells, bool bits) {
    json array = json::array();
    for (const auto& cell : cells) {
        json item;
        item["enabled"] = cell.enabled;
        item["device"] = cell.device;
        item["address"] = cell.address;
        if (bits) {
            item["value"] = cell.value != 0;
        } else {
            item["value"] = cell.value;
        }
        array.push_back(std::move(item));
    }
    return array;
}

std::vector<RegisterCell> CellsFromJson(const json& array, bool bits) {
    std::vector<RegisterCell> cells;
    if (!array.is_array()) {
        return cells;
    }
    cells.reserve(array.size());
    for (const auto& item : array) {
        RegisterCell cell;
        cell.enabled = item.value("enabled", true);
        cell.device = static_cast<std::uint8_t>(item.value("device", 1));
        cell.address = static_cast<std::uint16_t>(item.value("address", 0));
        if (bits) {
            if (item.contains("value") && item["value"].is_boolean()) {
                cell.value = item["value"].get<bool>() ? 1 : 0;
            } else {
                cell.value = static_cast<std::uint16_t>(item.value("value", 0) != 0);
            }
        } else {
            cell.value = static_cast<std::uint16_t>(item.value("value", 0));
        }
        cells.push_back(cell);
    }
    return cells;
}

}  // namespace

void ConfigStore::SetAppIdentity(std::string folder, std::string fileName) {
    if (!folder.empty()) {
        gAppFolder = std::move(folder);
    }
    if (!fileName.empty()) {
        gAppFile = std::move(fileName);
    }
}

std::filesystem::path ConfigStore::NextToExecutable() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        return std::filesystem::path(buffer).parent_path() / gAppFile;
    }
#endif
    return std::filesystem::current_path() / gAppFile;
}

std::filesystem::path ConfigStore::SessionPath() {
#ifdef _WIN32
    if (const wchar_t* appData = _wgetenv(L"APPDATA")) {
        return std::filesystem::path(appData) / gAppFolder / "session.json";
    }
#endif
    if (const char* appData = std::getenv("APPDATA")) {
        return std::filesystem::path(appData) / gAppFolder / "session.json";
    }
    return NextToExecutable();
}

AppConfig ConfigStore::load(const std::filesystem::path& path, std::string& error) {
    AppConfig config;
    std::ifstream in(path);
    if (!in) {
        error = "Cannot open " + path.string();
        return config;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& ex) {
        error = ex.what();
        return config;
    }

    const auto serial = root.value("serial", json::object());
    config.serial.port = NormalizeComPort(serial.value("port", "COM1"));
    config.serial.baud = serial.value("baud", 9600U);
    config.serial.parity = ParseParity(serial.value("parity", "none"));
    config.serial.stopBits = serial.value("stop_bits", 1) == 2 ? StopBitsMode::Two : StopBitsMode::One;
    config.pollIntervalMs = root.value("poll_interval_ms", 200U);
    if (config.pollIntervalMs < 50) {
        config.pollIntervalMs = 50;
    }

    const auto ui = root.value("ui", json::object());
    if (!ui.empty()) {
        config.ui.hasGeometry = ui.contains("x") && ui.contains("y") && ui.contains("width") && ui.contains("height");
        config.ui.x = ui.value("x", 0);
        config.ui.y = ui.value("y", 0);
        config.ui.width = ui.value("width", 1180);
        config.ui.height = ui.value("height", 760);
        config.ui.maximized = ui.value("maximized", false);
        config.ui.tab = ui.value("tab", 0);
    }

    config.coils = CellsFromJson(root.value("coils", json::array()), true);
    config.discreteInputs = CellsFromJson(root.value("discrete_inputs", json::array()), true);
    config.inputRegisters = CellsFromJson(root.value("input_registers", json::array()), false);
    config.holdingRegisters = CellsFromJson(root.value("holding_registers", json::array()), false);
    error.clear();
    return config;
}

bool ConfigStore::save(const std::filesystem::path& path, const AppConfig& config, std::string& error) {
    json root;
    root["version"] = 1;
    root["serial"] = {
        {"port", config.serial.port},
        {"baud", config.serial.baud},
        {"parity", ParityName(config.serial.parity)},
        {"stop_bits", config.serial.stopBits == StopBitsMode::Two ? 2 : 1}
    };
    root["poll_interval_ms"] = config.pollIntervalMs;
    root["ui"] = {
        {"x", config.ui.x},
        {"y", config.ui.y},
        {"width", config.ui.width},
        {"height", config.ui.height},
        {"maximized", config.ui.maximized},
        {"tab", config.ui.tab}
    };
    root["coils"] = CellsToJson(config.coils, true);
    root["discrete_inputs"] = CellsToJson(config.discreteInputs, true);
    root["input_registers"] = CellsToJson(config.inputRegisters, false);
    root["holding_registers"] = CellsToJson(config.holdingRegisters, false);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "Cannot write " + path.string();
        return false;
    }
    out << root.dump(2) << '\n';
    out.flush();
    if (!out) {
        error = "Failed while writing " + path.string();
        return false;
    }
    error.clear();
    return true;
}

AppConfig ConfigStore::fromStore(const SerialSettings& serial, const RegisterStore& store, const UiState& ui) {
    AppConfig config;
    config.serial = serial;
    config.ui = ui;
    config.pollIntervalMs = 200;
    config.coils = store.snapshot(TableKind::Coils);
    config.discreteInputs = store.snapshot(TableKind::DiscreteInputs);
    config.inputRegisters = store.snapshot(TableKind::InputRegisters);
    config.holdingRegisters = store.snapshot(TableKind::HoldingRegisters);
    return config;
}

void ConfigStore::applyToStore(const AppConfig& config, RegisterStore& store) {
    store.replaceTable(TableKind::Coils, config.coils);
    store.replaceTable(TableKind::DiscreteInputs, config.discreteInputs);
    store.replaceTable(TableKind::InputRegisters, config.inputRegisters);
    store.replaceTable(TableKind::HoldingRegisters, config.holdingRegisters);
}

}  // namespace emulator
