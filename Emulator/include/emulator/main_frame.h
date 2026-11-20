#pragma once

#include <wx/frame.h>
#include <wx/gdicmn.h>

#include "emulator/config_store.h"
#include "emulator/modbus_slave.h"
#include "emulator/register_store.h"
#include "emulator/serial_rtu.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class wxButton;
class wxComboBox;
class wxNotebook;
class wxStatusBar;
class wxTextCtrl;

namespace emulator {

class RegisterPanel;

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    void buildUi();
    void bindEvents();
    void refreshPorts(const std::string& preferredPort = {});
    void updateConnectionUi();
    void setStatus(const wxString& connection, const wxString& extra = {});

    SerialSettings settingsFromUi() const;
    void settingsToUi(const SerialSettings& settings);
    UiState uiStateFromWindow() const;
    void applyUiState(const UiState& ui);
    void seedDefaultRegisters();
    void restoreSession();
    bool persistSession(bool quiet);

    void onConnect(wxCommandEvent&);
    void onRefreshPorts(wxCommandEvent&);
    void onSave(wxCommandEvent&);
    void onLoad(wxCommandEvent&);
    void onClose(wxCloseEvent&);
    void onSize(wxSizeEvent&);

    std::optional<std::vector<std::uint8_t>> onFrame(const std::uint8_t* data, std::size_t length);
    void onSerialError(const std::string& message);
    void appendLog(const std::string& line);
    void reloadPanels();

    bool saveTo(const std::filesystem::path& path, bool quiet);
    bool loadFrom(const std::filesystem::path& path, bool quiet);
    void commitAllEdits();

    RegisterStore store_;
    ModbusSlave slave_;
    SerialRtuServer server_;

    wxComboBox* portCombo_ = nullptr;
    wxComboBox* baudCombo_ = nullptr;
    wxComboBox* parityCombo_ = nullptr;
    wxComboBox* stopCombo_ = nullptr;
    wxButton* connectButton_ = nullptr;
    wxButton* refreshButton_ = nullptr;
    wxNotebook* notebook_ = nullptr;
    wxTextCtrl* log_ = nullptr;
    std::array<RegisterPanel*, 4> panels_{};
    std::uint64_t rxCount_ = 0;
    std::uint64_t txCount_ = 0;
    wxRect normalRect_{0, 0, 1180, 760};
    UiState lastUi_{};
};

}  // namespace emulator
