#include "emulator/main_frame.h"

#include "emulator/app_art.h"
#include "emulator/register_panel.h"
#include "emulator/serial_ports.h"

#include <filesystem>
#include <string>

#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/datetime.h>
#include <wx/display.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>

namespace emulator {
namespace {

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

const unsigned kBaudRates[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400};

int ParityIndex(ParityMode mode) {
    switch (mode) {
    case ParityMode::Even:
        return 1;
    case ParityMode::Odd:
        return 2;
    case ParityMode::None:
    default:
        return 0;
    }
}

ParityMode ParityFromIndex(int index) {
    if (index == 1) {
        return ParityMode::Even;
    }
    if (index == 2) {
        return ParityMode::Odd;
    }
    return ParityMode::None;
}

}  // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, U8("Эмулятор Modbus RTU"), wxDefaultPosition, wxSize(1180, 760)),
      slave_(store_) {
    buildUi();
    bindEvents();
    restoreSession();
    updateConnectionUi();
    setStatus(U8("Отключено"));
}

MainFrame::~MainFrame() {
    server_.stop();
}

void MainFrame::buildUi() {
    auto* root = new wxPanel(this);
    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    ApplyAppIcon(this);

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    toolbar->Add(new wxStaticText(root, wxID_ANY, U8("COM-порт")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    portCombo_ = new wxComboBox(root, wxID_ANY, "COM1", wxDefaultPosition, wxSize(110, -1));
    toolbar->Add(portCombo_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    refreshButton_ = new wxButton(root, wxID_ANY, U8("Обновить"), wxDefaultPosition, wxSize(90, -1));
    toolbar->Add(refreshButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    toolbar->Add(new wxStaticText(root, wxID_ANY, U8("Скорость")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    baudCombo_ = new wxComboBox(root, wxID_ANY, "9600", wxDefaultPosition, wxSize(100, -1), 0, nullptr, wxCB_DROPDOWN);
    for (unsigned baud : kBaudRates) {
        baudCombo_->Append(wxString::Format("%u", baud));
    }
    baudCombo_->SetStringSelection("9600");
    toolbar->Add(baudCombo_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    toolbar->Add(new wxStaticText(root, wxID_ANY, U8("Чётность")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    parityCombo_ = new wxComboBox(root, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), 0, nullptr, wxCB_READONLY);
    parityCombo_->Append(U8("None"));
    parityCombo_->Append(U8("Even"));
    parityCombo_->Append(U8("Odd"));
    parityCombo_->SetSelection(0);
    toolbar->Add(parityCombo_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    toolbar->Add(new wxStaticText(root, wxID_ANY, U8("Stop")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    stopCombo_ = new wxComboBox(root, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(60, -1), 0, nullptr, wxCB_READONLY);
    stopCombo_->Append("1");
    stopCombo_->Append("2");
    stopCombo_->SetSelection(0);
    toolbar->Add(stopCombo_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    connectButton_ = new wxButton(root, wxID_ANY, U8("Подключить"), wxDefaultPosition, wxSize(130, -1));
    toolbar->Add(connectButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    auto* saveButton = new wxButton(root, wxID_ANY, U8("Сохранить"), wxDefaultPosition, wxSize(110, -1));
    auto* loadButton = new wxButton(root, wxID_ANY, U8("Загрузить"), wxDefaultPosition, wxSize(110, -1));
    toolbar->Add(saveButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    toolbar->Add(loadButton, 0, wxALIGN_CENTER_VERTICAL);

    notebook_ = new wxNotebook(root, wxID_ANY);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto kind = kAllTables[i];
        panels_[i] = new RegisterPanel(notebook_, store_, kind);
        notebook_->AddPage(panels_[i], U8(TableTitle(kind).data()));
    }

    auto* logLabel = new wxStaticText(root, wxID_ANY, U8("Журнал Modbus RTU"));
    log_ = new wxTextCtrl(root, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 140),
        wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
    log_->SetFont(wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

    rootSizer->Add(toolbar, 0, wxEXPAND | wxALL, 10);
    rootSizer->Add(notebook_, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);
    rootSizer->Add(logLabel, 0, wxLEFT | wxRIGHT | wxTOP, 10);
    rootSizer->Add(log_, 0, wxEXPAND | wxALL, 10);
    root->SetSizer(rootSizer);

    auto* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(root, 1, wxEXPAND);
    SetSizer(frameSizer);

    CreateStatusBar(2);
    SetMinSize(wxSize(900, 560));

    saveButton->Bind(wxEVT_BUTTON, &MainFrame::onSave, this);
    loadButton->Bind(wxEVT_BUTTON, &MainFrame::onLoad, this);
}

void MainFrame::bindEvents() {
    connectButton_->Bind(wxEVT_BUTTON, &MainFrame::onConnect, this);
    refreshButton_->Bind(wxEVT_BUTTON, &MainFrame::onRefreshPorts, this);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
    Bind(wxEVT_SIZE, &MainFrame::onSize, this);
}

void MainFrame::refreshPorts(const std::string& preferredPort) {
    wxString current = portCombo_->GetValue();
    if (!preferredPort.empty()) {
        current = wxString::FromUTF8(preferredPort.c_str());
    }
    portCombo_->Clear();
    const auto ports = ListSerialPorts();
    for (const auto& port : ports) {
        portCombo_->Append(wxString::FromUTF8(port.c_str()));
    }
    if (!current.IsEmpty()) {
        if (portCombo_->FindString(current) == wxNOT_FOUND) {
            portCombo_->Append(current);
        }
        portCombo_->SetValue(current);
    } else if (!ports.empty()) {
        portCombo_->SetValue(wxString::FromUTF8(ports.front().c_str()));
    }
}

void MainFrame::updateConnectionUi() {
    const bool connected = server_.running();
    connectButton_->SetLabel(connected ? U8("Отключить") : U8("Подключить"));
    portCombo_->Enable(!connected);
    baudCombo_->Enable(!connected);
    parityCombo_->Enable(!connected);
    stopCombo_->Enable(!connected);
    refreshButton_->Enable(!connected);
}

void MainFrame::setStatus(const wxString& connection, const wxString& extra) {
    if (GetStatusBar() == nullptr) {
        return;
    }
    SetStatusText(connection, 0);
    const wxString stats = extra.IsEmpty()
        ? wxString::Format("RX %llu  TX %llu", static_cast<unsigned long long>(rxCount_), static_cast<unsigned long long>(txCount_))
        : extra;
    SetStatusText(stats, 1);
}

SerialSettings MainFrame::settingsFromUi() const {
    SerialSettings settings;
    settings.port = NormalizeComPort(std::string(portCombo_->GetValue().ToUTF8()));
    unsigned long baud = 9600;
    if (!baudCombo_->GetValue().ToULong(&baud) || baud == 0) {
        baud = 9600;
    }
    settings.baud = static_cast<unsigned>(baud);
    settings.parity = ParityFromIndex(parityCombo_->GetSelection());
    settings.stopBits = stopCombo_->GetSelection() == 1 ? StopBitsMode::Two : StopBitsMode::One;
    return settings;
}

void MainFrame::settingsToUi(const SerialSettings& settings) {
    portCombo_->SetValue(wxString::FromUTF8(settings.port.c_str()));
    baudCombo_->SetValue(wxString::Format("%u", settings.baud));
    parityCombo_->SetSelection(ParityIndex(settings.parity));
    stopCombo_->SetSelection(settings.stopBits == StopBitsMode::Two ? 1 : 0);
}

void MainFrame::onConnect(wxCommandEvent&) {
    if (server_.running()) {
        server_.stop();
        updateConnectionUi();
        setStatus(U8("Отключено"));
        appendLog("Disconnected");
        return;
    }

    auto settings = settingsFromUi();
    if (settings.port.empty()) {
        wxMessageBox(U8("Укажите номер COM-порта."), U8("Подключение"), wxOK | wxICON_WARNING, this);
        return;
    }

    std::string error;
    const bool ok = server_.start(
        settings,
        [this](const std::uint8_t* data, std::size_t length) { return onFrame(data, length); },
        [this](const std::string& message) { onSerialError(message); },
        error);
    if (!ok) {
        wxMessageBox(U8(error.c_str()), U8("Подключение"), wxOK | wxICON_ERROR, this);
        return;
    }

    updateConnectionUi();
    const wxString parityTag = settings.parity == ParityMode::Even
        ? "E"
        : (settings.parity == ParityMode::Odd ? "O" : "N");
    const wxString stopTag = settings.stopBits == StopBitsMode::Two ? "2" : "1";
    setStatus(U8("Подключено  ") + wxString::FromUTF8(settings.port.c_str())
        + wxString::Format("  %u 8", settings.baud) + parityTag + stopTag);
    appendLog("Connected " + settings.port + " " + std::to_string(settings.baud));
}

void MainFrame::onRefreshPorts(wxCommandEvent&) {
    refreshPorts();
}

void MainFrame::onSize(wxSizeEvent& event) {
    if (!IsMaximized() && !IsIconized()) {
        normalRect_ = GetRect();
    }
    event.Skip();
}

void MainFrame::onSave(wxCommandEvent&) {
    commitAllEdits();
    wxFileDialog dialog(this, U8("Сохранить настройки"), wxEmptyString, "modbus_rtu_emulator.json",
        "JSON (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    saveTo(std::filesystem::path(dialog.GetPath().ToStdWstring()), false);
}

void MainFrame::onLoad(wxCommandEvent&) {
    wxFileDialog dialog(this, U8("Загрузить настройки"), wxEmptyString, wxEmptyString,
        "JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    loadFrom(std::filesystem::path(dialog.GetPath().ToStdWstring()), false);
}

void MainFrame::onClose(wxCloseEvent& event) {
    server_.stop();
    if (!persistSession(true) && event.CanVeto()) {
        const int answer = wxMessageBox(
            U8("Не удалось сохранить сессию. Закрыть окно без сохранения?"),
            U8("Сессия"),
            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
            this);
        if (answer != wxYES) {
            event.Veto();
            return;
        }
    }
    event.Skip();
}

std::optional<std::vector<std::uint8_t>> MainFrame::onFrame(const std::uint8_t* data, std::size_t length) {
    auto result = slave_.process(data, length);
    const bool hasTx = result.response.has_value();
    const auto log = result.log;
    const bool changed = result.storeChanged;
    const auto table = result.changedTable;
    CallAfter([this, log, hasTx, changed, table] {
        ++rxCount_;
        if (hasTx) {
            ++txCount_;
        }
        appendLog(log);
        if (changed) {
            for (auto* panel : panels_) {
                if (panel != nullptr && panel->kind() == table) {
                    panel->reloadFromStore();
                }
            }
        }
        setStatus(GetStatusBar() != nullptr ? GetStatusBar()->GetStatusText(0) : wxString());
    });
    return result.response;
}

void MainFrame::onSerialError(const std::string& message) {
    CallAfter([this, message] {
        appendLog("Serial error: " + message);
        if (server_.running()) {
            server_.stop();
        }
        updateConnectionUi();
        setStatus(U8("Ошибка порта"));
        wxMessageBox(U8(message.c_str()), U8("COM-порт"), wxOK | wxICON_ERROR, this);
    });
}

void MainFrame::appendLog(const std::string& line) {
    if (log_ == nullptr) {
        return;
    }
    const wxDateTime now = wxDateTime::UNow();
    const wxString stamp = now.Format("%H:%M:%S.")
        + wxString::Format("%03ld", static_cast<long>(now.GetMillisecond()));
    log_->AppendText("[" + stamp + "] " + wxString::FromUTF8(line.c_str()) + "\n");
    if (log_->GetNumberOfLines() > 400) {
        const auto pos = log_->XYToPosition(0, 80);
        log_->Remove(0, pos);
    }
}

void MainFrame::reloadPanels() {
    for (auto* panel : panels_) {
        if (panel != nullptr) {
            panel->reloadFromStore();
        }
    }
}

void MainFrame::commitAllEdits() {
    for (auto* panel : panels_) {
        if (panel != nullptr) {
            panel->commitEdits();
        }
    }
}

void MainFrame::seedDefaultRegisters() {
    for (auto kind : kAllTables) {
        if (store_.size(kind) > 0) {
            continue;
        }
        RegisterCell cell;
        cell.enabled = true;
        cell.device = 1;
        cell.address = 0;
        cell.value = 0;
        store_.add(kind, cell);
    }
    reloadPanels();
}

UiState MainFrame::uiStateFromWindow() const {
    UiState ui;
    const wxRect rect = (!IsMaximized() && !IsIconized()) ? GetRect() : normalRect_;
    ui.hasGeometry = true;
    ui.x = rect.x;
    ui.y = rect.y;
    ui.width = rect.width;
    ui.height = rect.height;
    ui.maximized = IsMaximized();
    ui.tab = notebook_ != nullptr ? notebook_->GetSelection() : 0;
    return ui;
}

void MainFrame::applyUiState(const UiState& ui) {
    if (notebook_ != nullptr && ui.tab >= 0 && ui.tab < static_cast<int>(notebook_->GetPageCount())) {
        notebook_->SetSelection(ui.tab);
    }
    if (!ui.hasGeometry || ui.width < 400 || ui.height < 300) {
        Centre();
        return;
    }

    wxRect rect(ui.x, ui.y, ui.width, ui.height);
    bool visible = false;
    const int displays = wxDisplay::GetCount();
    for (int i = 0; i < displays; ++i) {
        if (wxDisplay(i).GetGeometry().Intersects(rect)) {
            visible = true;
            break;
        }
    }
    if (!visible) {
        Centre();
        return;
    }
    SetSize(rect);
    if (ui.maximized) {
        Maximize(true);
    }
}

void MainFrame::restoreSession() {
    const std::filesystem::path candidates[] = {
        ConfigStore::SessionPath(),
        ConfigStore::NextToExecutable()
    };

    for (const auto& path : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        if (loadFrom(path, true)) {
            applyUiState(lastUi_);
            return;
        }
    }

    seedDefaultRegisters();
    refreshPorts();
    Centre();
}

bool MainFrame::persistSession(bool quiet) {
    commitAllEdits();
    if (saveTo(ConfigStore::SessionPath(), quiet)) {
        return true;
    }
    return saveTo(ConfigStore::NextToExecutable(), quiet);
}

bool MainFrame::saveTo(const std::filesystem::path& path, bool quiet) {
    std::string error;
    const auto config = ConfigStore::fromStore(settingsFromUi(), store_, uiStateFromWindow());
    if (!ConfigStore::save(path, config, error)) {
        if (!quiet) {
            wxMessageBox(U8(error.c_str()), U8("Сохранение"), wxOK | wxICON_ERROR, this);
        }
        return false;
    }
    if (!quiet) {
        appendLog("Saved " + path.string());
    }
    return true;
}

bool MainFrame::loadFrom(const std::filesystem::path& path, bool quiet) {
    std::string error;
    const auto config = ConfigStore::load(path, error);
    if (!error.empty()) {
        if (!quiet) {
            wxMessageBox(U8(error.c_str()), U8("Загрузка"), wxOK | wxICON_ERROR, this);
        }
        return false;
    }
    ConfigStore::applyToStore(config, store_);
    settingsToUi(config.serial);
    refreshPorts(config.serial.port);
    reloadPanels();
    lastUi_ = config.ui;
    if (!quiet) {
        appendLog("Loaded " + path.string());
    }
    return true;
}

}  // namespace emulator
