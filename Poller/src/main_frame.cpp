#include "poller/main_frame.h"

#include "emulator/app_art.h"
#include "emulator/register_panel.h"
#include "emulator/serial_ports.h"

#include <chrono>
#include <filesystem>
#include <string>

#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/datetime.h>
#include <wx/display.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>

namespace poller {
namespace {

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

const unsigned kBaudRates[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400};

int ParityIndex(emulator::ParityMode mode) {
    switch (mode) {
    case emulator::ParityMode::Even:
        return 1;
    case emulator::ParityMode::Odd:
        return 2;
    case emulator::ParityMode::None:
    default:
        return 0;
    }
}

emulator::ParityMode ParityFromIndex(int index) {
    if (index == 1) {
        return emulator::ParityMode::Even;
    }
    if (index == 2) {
        return emulator::ParityMode::Odd;
    }
    return emulator::ParityMode::None;
}

}  // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, U8("Опрос Modbus RTU"), wxDefaultPosition, wxSize(1180, 760)),
      engine_(store_, master_) {
    buildUi();
    bindEvents();
    store_.setOnUserEdit([this](emulator::TableKind kind, std::uint8_t device, std::uint16_t address, std::uint16_t value) {
        onUserEdit(kind, device, address, value);
    });
    restoreSession();
    updateConnectionUi();
    setStatus(U8("Отключено"));
}

MainFrame::~MainFrame() {
    engine_.requestStop();
    master_.stop();
    engine_.stop();
}

void MainFrame::buildUi() {
    auto* root = new wxPanel(this);
    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    emulator::ApplyAppIcon(this);

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

    toolbar->Add(new wxStaticText(root, wxID_ANY, U8("Период, мс")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    intervalSpin_ = new wxSpinCtrl(root, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1),
        wxSP_ARROW_KEYS, 50, 10000, 200);
    intervalSpin_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        engine_.setInterval(std::chrono::milliseconds(intervalSpin_->GetValue()));
    });
    toolbar->Add(intervalSpin_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    connectButton_ = new wxButton(root, wxID_ANY, U8("Подключить"), wxDefaultPosition, wxSize(130, -1));
    toolbar->Add(connectButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    auto* saveButton = new wxButton(root, wxID_ANY, U8("Сохранить"), wxDefaultPosition, wxSize(110, -1));
    auto* loadButton = new wxButton(root, wxID_ANY, U8("Загрузить"), wxDefaultPosition, wxSize(110, -1));
    toolbar->Add(saveButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    toolbar->Add(loadButton, 0, wxALIGN_CENTER_VERTICAL);

    notebook_ = new wxNotebook(root, wxID_ANY);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto kind = emulator::kAllTables[i];
        panels_[i] = new emulator::RegisterPanel(
            notebook_,
            store_,
            kind,
            !emulator::IsWritableTable(kind));
        notebook_->AddPage(panels_[i], U8(emulator::TableTitle(kind).data()));
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
    const auto ports = emulator::ListSerialPorts();
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
    const bool connected = master_.running();
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
        ? wxString::Format("TX %llu  RX %llu", static_cast<unsigned long long>(txCount_), static_cast<unsigned long long>(rxCount_))
        : extra;
    SetStatusText(stats, 1);
}

emulator::SerialSettings MainFrame::settingsFromUi() const {
    emulator::SerialSettings settings;
    settings.port = emulator::NormalizeComPort(std::string(portCombo_->GetValue().ToUTF8()));
    unsigned long baud = 9600;
    if (!baudCombo_->GetValue().ToULong(&baud) || baud == 0) {
        baud = 9600;
    }
    settings.baud = static_cast<unsigned>(baud);
    settings.parity = ParityFromIndex(parityCombo_->GetSelection());
    settings.stopBits = stopCombo_->GetSelection() == 1 ? emulator::StopBitsMode::Two : emulator::StopBitsMode::One;
    return settings;
}

void MainFrame::settingsToUi(const emulator::SerialSettings& settings) {
    portCombo_->SetValue(wxString::FromUTF8(settings.port.c_str()));
    baudCombo_->SetValue(wxString::Format("%u", settings.baud));
    parityCombo_->SetSelection(ParityIndex(settings.parity));
    stopCombo_->SetSelection(settings.stopBits == emulator::StopBitsMode::Two ? 1 : 0);
}

void MainFrame::onConnect(wxCommandEvent&) {
    if (master_.running()) {
        engine_.requestStop();
        master_.stop();
        engine_.stop();
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
    if (!master_.start(settings, [this](const std::string& message) { onSerialError(message); }, error)) {
        wxMessageBox(U8(error.c_str()), U8("Подключение"), wxOK | wxICON_ERROR, this);
        return;
    }

    engine_.setInterval(std::chrono::milliseconds(intervalSpin_->GetValue()));
    engine_.setCallbacks(
        [this](const std::string& line, bool changed, emulator::TableKind table) {
            CallAfter([this, line, changed, table] {
                ++txCount_;
                ++rxCount_;
                appendLog(line);
                if (changed) {
                    reloadPanel(table);
                }
                setStatus(GetStatusBar() != nullptr ? GetStatusBar()->GetStatusText(0) : wxString());
            });
        },
        [this](const std::string& message) { onSerialError(message); });
    engine_.start();

    updateConnectionUi();
    const wxString parityTag = settings.parity == emulator::ParityMode::Even
        ? "E"
        : (settings.parity == emulator::ParityMode::Odd ? "O" : "N");
    const wxString stopTag = settings.stopBits == emulator::StopBitsMode::Two ? "2" : "1";
    setStatus(U8("Опрос  ") + wxString::FromUTF8(settings.port.c_str())
        + wxString::Format("  %u 8", settings.baud) + parityTag + stopTag
        + wxString::Format("  %d мс", intervalSpin_->GetValue()));
    appendLog("Connected " + settings.port + " " + std::to_string(settings.baud));
}

void MainFrame::onRefreshPorts(wxCommandEvent&) {
    refreshPorts();
}

void MainFrame::onSave(wxCommandEvent&) {
    commitAllEdits();
    wxFileDialog dialog(this, U8("Сохранить настройки"), wxEmptyString, "modbus_rtu_poller.json",
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
    engine_.requestStop();
    master_.stop();
    engine_.stop();
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

void MainFrame::onSize(wxSizeEvent& event) {
    if (!IsMaximized() && !IsIconized()) {
        normalRect_ = GetRect();
    }
    event.Skip();
}

void MainFrame::onUserEdit(emulator::TableKind kind, std::uint8_t device, std::uint16_t address, std::uint16_t value) {
    if (!emulator::IsWritableTable(kind) || !master_.running()) {
        return;
    }
    engine_.queueWrite(kind, device, address, value);
}

void MainFrame::onSerialError(const std::string& message) {
    CallAfter([this, message] {
        appendLog("Serial error: " + message);
        engine_.requestStop();
        if (master_.running()) {
            master_.stop();
        }
        engine_.stop();
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

void MainFrame::reloadPanel(emulator::TableKind kind) {
    for (auto* panel : panels_) {
        if (panel != nullptr && panel->kind() == kind) {
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
    for (auto kind : emulator::kAllTables) {
        if (store_.size(kind) > 0) {
            continue;
        }
        emulator::RegisterCell cell;
        cell.enabled = true;
        cell.device = 1;
        cell.address = 0;
        cell.value = 0;
        store_.add(kind, cell);
    }
    reloadPanels();
}

emulator::UiState MainFrame::uiStateFromWindow() const {
    emulator::UiState ui;
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

void MainFrame::applyUiState(const emulator::UiState& ui) {
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
        emulator::ConfigStore::SessionPath(),
        emulator::ConfigStore::NextToExecutable()
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
    if (saveTo(emulator::ConfigStore::SessionPath(), quiet)) {
        return true;
    }
    return saveTo(emulator::ConfigStore::NextToExecutable(), quiet);
}

bool MainFrame::saveTo(const std::filesystem::path& path, bool quiet) {
    std::string error;
    auto config = emulator::ConfigStore::fromStore(settingsFromUi(), store_, uiStateFromWindow());
    config.pollIntervalMs = static_cast<unsigned>(intervalSpin_->GetValue());
    if (!emulator::ConfigStore::save(path, config, error)) {
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
    const auto config = emulator::ConfigStore::load(path, error);
    if (!error.empty()) {
        if (!quiet) {
            wxMessageBox(U8(error.c_str()), U8("Загрузка"), wxOK | wxICON_ERROR, this);
        }
        return false;
    }
    emulator::ConfigStore::applyToStore(config, store_);
    settingsToUi(config.serial);
    if (intervalSpin_ != nullptr) {
        intervalSpin_->SetValue(static_cast<int>(config.pollIntervalMs));
    }
    refreshPorts(config.serial.port);
    reloadPanels();
    lastUi_ = config.ui;
    if (!quiet) {
        appendLog("Loaded " + path.string());
    }
    return true;
}

}  // namespace poller
