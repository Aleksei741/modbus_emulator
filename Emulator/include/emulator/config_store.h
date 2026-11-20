#pragma once

#include "emulator/modbus_types.h"
#include "emulator/register_store.h"
#include "emulator/serial_rtu.h"

#include <filesystem>
#include <string>
#include <vector>

namespace emulator {

struct UiState {
    bool hasGeometry = false;
    int x = 0;
    int y = 0;
    int width = 1180;
    int height = 760;
    bool maximized = false;
    int tab = 0;
};

struct AppConfig {
    SerialSettings serial;
    UiState ui;
    unsigned pollIntervalMs = 200;
    std::vector<RegisterCell> coils;
    std::vector<RegisterCell> discreteInputs;
    std::vector<RegisterCell> inputRegisters;
    std::vector<RegisterCell> holdingRegisters;
};

class ConfigStore {
public:
    static void SetAppIdentity(std::string folder, std::string fileName);
    static std::filesystem::path SessionPath();
    static std::filesystem::path NextToExecutable();

    static AppConfig load(const std::filesystem::path& path, std::string& error);
    static bool save(const std::filesystem::path& path, const AppConfig& config, std::string& error);

    static AppConfig fromStore(const SerialSettings& serial, const RegisterStore& store, const UiState& ui = {});
    static void applyToStore(const AppConfig& config, RegisterStore& store);
};

}  // namespace emulator
