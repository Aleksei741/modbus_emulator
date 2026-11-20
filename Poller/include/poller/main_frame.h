#pragma once

#include <wx/frame.h>
#include <wx/gdicmn.h>

#include "emulator/config_store.h"
#include "emulator/register_store.h"
#include "poller/poll_engine.h"
#include "poller/serial_rtu_master.h"

#include <array>
#include <filesystem>
#include <string>

class wxButton;
class wxComboBox;
class wxNotebook;
class wxSpinCtrl;
class wxTextCtrl;

namespace emulator {
class RegisterPanel;
}

namespace poller {

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

    emulator::SerialSettings settingsFromUi() const;
    void settingsToUi(const emulator::SerialSettings& settings);
    emulator::UiState uiStateFromWindow() const;
    void applyUiState(const emulator::UiState& ui);
    void seedDefaultRegisters();
    void restoreSession();
    bool persistSession(bool quiet);

    void onConnect(wxCommandEvent&);
    void onRefreshPorts(wxCommandEvent&);
    void onSave(wxCommandEvent&);
    void onLoad(wxCommandEvent&);
    void onClose(wxCloseEvent&);
    void onSize(wxSizeEvent&);
    void onUserEdit(emulator::TableKind kind, std::uint8_t device, std::uint16_t address, std::uint16_t value);
    void onSerialError(const std::string& message);
    void appendLog(const std::string& line);
    void reloadPanels();
    void reloadPanel(emulator::TableKind kind);

    bool saveTo(const std::filesystem::path& path, bool quiet);
    bool loadFrom(const std::filesystem::path& path, bool quiet);
    void commitAllEdits();

    emulator::RegisterStore store_;
    SerialRtuMaster master_;
    PollEngine engine_;

    wxComboBox* portCombo_ = nullptr;
    wxComboBox* baudCombo_ = nullptr;
    wxComboBox* parityCombo_ = nullptr;
    wxComboBox* stopCombo_ = nullptr;
    wxSpinCtrl* intervalSpin_ = nullptr;
    wxButton* connectButton_ = nullptr;
    wxButton* refreshButton_ = nullptr;
    wxNotebook* notebook_ = nullptr;
    wxTextCtrl* log_ = nullptr;
    std::array<emulator::RegisterPanel*, 4> panels_{};
    std::uint64_t rxCount_ = 0;
    std::uint64_t txCount_ = 0;
    wxRect normalRect_{0, 0, 1180, 760};
    emulator::UiState lastUi_{};
};

}  // namespace poller
