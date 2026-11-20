#include "emulator/register_panel.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

namespace emulator {
namespace {

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

enum {
    ColEnabled = 0,
    ColDevice = 1,
    ColAddress = 2,
    ColValue = 3,
    ColHex = 4
};

class RangeDialog : public wxDialog {
public:
    RangeDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, U8("Добавить диапазон"), wxDefaultPosition, wxDefaultSize) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto row = [&](const char* label, wxSpinCtrl*& ctrl, int min, int max, int value) {
            auto* line = new wxBoxSizer(wxHORIZONTAL);
            line->Add(new wxStaticText(this, wxID_ANY, U8(label)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
            ctrl = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(140, -1), wxSP_ARROW_KEYS, min, max, value);
            line->Add(ctrl, 1, wxEXPAND);
            root->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
        };

        row("Адрес устройства", device_, 1, 247, 1);
        row("Начальный адрес", start_, 0, 65535, 0);
        row("Количество", count_, 1, 65536, 10);

        root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
        SetSizerAndFit(root);
        CentreOnParent();
    }

    std::uint8_t device() const { return static_cast<std::uint8_t>(device_->GetValue()); }
    std::uint16_t start() const { return static_cast<std::uint16_t>(start_->GetValue()); }
    std::size_t count() const { return static_cast<std::size_t>(count_->GetValue()); }

private:
    wxSpinCtrl* device_ = nullptr;
    wxSpinCtrl* start_ = nullptr;
    wxSpinCtrl* count_ = nullptr;
};

bool ParseNumber(const wxString& text, long min, long max, long& out) {
    wxString trimmed = text;
    trimmed.Trim(true).Trim(false);
    int base = 10;
    if (trimmed.StartsWith("0x") || trimmed.StartsWith("0X")) {
        base = 16;
        trimmed = trimmed.Mid(2);
    }
    if (trimmed.IsEmpty()) {
        return false;
    }
    unsigned long value = 0;
    if (!trimmed.ToULong(&value, base)) {
        return false;
    }
    if (static_cast<long>(value) < min || static_cast<long>(value) > max) {
        return false;
    }
    out = static_cast<long>(value);
    return true;
}

}  // namespace

class RegisterGridTable : public wxGridTableBase {
public:
    RegisterGridTable(RegisterStore& store, TableKind kind, bool valuesReadOnly)
        : store_(store),
          kind_(kind),
          valuesReadOnly_(valuesReadOnly),
          lastViewRows_(static_cast<int>(store.size(kind))) {}

    bool isValueColumn(int col) const {
        return col == ColValue || col == ColHex;
    }

    int GetNumberRows() override {
        return static_cast<int>(store_.size(kind_));
    }

    int GetNumberCols() override {
        return IsBitTable(kind_) ? 4 : 5;
    }

    wxString GetValue(int row, int col) override {
        const auto cell = store_.at(kind_, static_cast<std::size_t>(row));
        switch (col) {
        case ColEnabled:
            return cell.enabled ? "1" : "0";
        case ColDevice:
            return wxString::Format("%u", static_cast<unsigned>(cell.device));
        case ColAddress:
            return wxString::Format("%u", static_cast<unsigned>(cell.address));
        case ColValue:
            if (IsBitTable(kind_)) {
                return cell.value != 0 ? "1" : "0";
            }
            return wxString::Format("%u", static_cast<unsigned>(cell.value));
        case ColHex:
            return wxString::Format("%04X", static_cast<unsigned>(cell.value));
        default:
            return {};
        }
    }

    void SetValue(int row, int col, const wxString& value) override {
        auto cell = store_.at(kind_, static_cast<std::size_t>(row));
        if (cell.id == 0 && store_.size(kind_) <= static_cast<std::size_t>(row)) {
            return;
        }
        long number = 0;
        switch (col) {
        case ColEnabled:
            cell.enabled = value != "0" && !value.IsEmpty() && value.CmpNoCase("false") != 0;
            break;
        case ColDevice:
            if (!ParseNumber(value, kMinDeviceAddress, kMaxDeviceAddress, number)) {
                return;
            }
            cell.device = static_cast<std::uint8_t>(number);
            break;
        case ColAddress:
            if (!ParseNumber(value, 0, kMaxRegisterAddress, number)) {
                return;
            }
            cell.address = static_cast<std::uint16_t>(number);
            break;
        case ColValue:
            if (valuesReadOnly_) {
                return;
            }
            if (IsBitTable(kind_)) {
                cell.value = (value != "0" && !value.IsEmpty() && value.CmpNoCase("false") != 0) ? 1 : 0;
            } else if (ParseNumber(value, 0, 65535, number)) {
                cell.value = static_cast<std::uint16_t>(number);
            } else {
                return;
            }
            break;
        case ColHex:
            if (valuesReadOnly_) {
                return;
            }
            if (!ParseNumber(value.StartsWith("0x") || value.StartsWith("0X") ? value : ("0x" + value), 0, 65535, number)) {
                return;
            }
            cell.value = static_cast<std::uint16_t>(number);
            break;
        default:
            return;
        }
        store_.setRow(kind_, static_cast<std::size_t>(row), cell);
    }

    wxString GetColLabelValue(int col) override {
        switch (col) {
        case ColEnabled:
            return U8("Вкл");
        case ColDevice:
            return U8("Адрес устройства");
        case ColAddress:
            return U8("Адрес регистра");
        case ColValue:
            if (IsBitTable(kind_)) {
                return valuesReadOnly_ ? U8("Значение (чтение)") : U8("Значение (0/1)");
            }
            return valuesReadOnly_ ? U8("Значение (чтение)") : U8("Значение");
        case ColHex:
            return U8("Hex");
        default:
            return {};
        }
    }

    wxString GetRowLabelValue(int row) override {
        return wxString::Format("%d", row + 1);
    }

    wxString GetTypeName(int, int col) override {
        if (col == ColEnabled || (col == ColValue && IsBitTable(kind_))) {
            return wxGRID_VALUE_BOOL;
        }
        if (col == ColHex) {
            return wxGRID_VALUE_STRING;
        }
        return wxGRID_VALUE_NUMBER;
    }

    bool CanGetValueAs(int, int col, const wxString& typeName) override {
        if (typeName == wxGRID_VALUE_BOOL) {
            return col == ColEnabled || (col == ColValue && IsBitTable(kind_));
        }
        if (typeName == wxGRID_VALUE_NUMBER) {
            return col == ColDevice || col == ColAddress || (col == ColValue && !IsBitTable(kind_));
        }
        return typeName == wxGRID_VALUE_STRING;
    }

    bool CanSetValueAs(int row, int col, const wxString& typeName) override {
        if (valuesReadOnly_ && isValueColumn(col)) {
            return false;
        }
        return CanGetValueAs(row, col, typeName);
    }

    bool GetValueAsBool(int row, int col) override {
        const auto cell = store_.at(kind_, static_cast<std::size_t>(row));
        if (col == ColEnabled) {
            return cell.enabled;
        }
        return cell.value != 0;
    }

    void SetValueAsBool(int row, int col, bool value) override {
        auto cell = store_.at(kind_, static_cast<std::size_t>(row));
        if (col == ColEnabled) {
            cell.enabled = value;
        } else if (valuesReadOnly_) {
            return;
        } else {
            cell.value = value ? 1 : 0;
        }
        store_.setRow(kind_, static_cast<std::size_t>(row), cell);
    }

    long GetValueAsLong(int row, int col) override {
        const auto cell = store_.at(kind_, static_cast<std::size_t>(row));
        switch (col) {
        case ColDevice:
            return cell.device;
        case ColAddress:
            return cell.address;
        case ColValue:
            return cell.value;
        default:
            return 0;
        }
    }

    void SetValueAsLong(int row, int col, long value) override {
        auto cell = store_.at(kind_, static_cast<std::size_t>(row));
        switch (col) {
        case ColDevice:
            if (value < kMinDeviceAddress || value > kMaxDeviceAddress) {
                return;
            }
            cell.device = static_cast<std::uint8_t>(value);
            break;
        case ColAddress:
            if (value < 0 || value > kMaxRegisterAddress) {
                return;
            }
            cell.address = static_cast<std::uint16_t>(value);
            break;
        case ColValue:
            if (valuesReadOnly_ || value < 0 || value > 65535) {
                return;
            }
            cell.value = static_cast<std::uint16_t>(value);
            break;
        default:
            return;
        }
        store_.setRow(kind_, static_cast<std::size_t>(row), cell);
    }

    wxGridCellAttr* GetAttr(int row, int col, wxGridCellAttr::wxAttrKind kind) override {
        wxGridCellAttr* attr = wxGridTableBase::GetAttr(row, col, kind);
        if (attr == nullptr) {
            attr = new wxGridCellAttr();
        } else {
            wxGridCellAttr* clone = attr->Clone();
            attr->DecRef();
            attr = clone;
        }
        attr->SetAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
        if (valuesReadOnly_ && isValueColumn(col)) {
            attr->SetReadOnly(true);
        }
        const auto cell = store_.at(kind_, static_cast<std::size_t>(row));
        if (!cell.enabled || (valuesReadOnly_ && isValueColumn(col))) {
            attr->SetTextColour(wxColour(140, 140, 140));
            attr->SetBackgroundColour(wxColour(245, 245, 245));
        }
        return attr;
    }

    void notifyChanged() {
        auto* grid = GetView();
        const int now = GetNumberRows();
        if (grid != nullptr && now != lastViewRows_) {
            if (now > lastViewRows_) {
                wxGridTableMessage msg(this, wxGRIDTABLE_NOTIFY_ROWS_APPENDED, now - lastViewRows_);
                grid->ProcessTableMessage(msg);
            } else {
                wxGridTableMessage msg(this, wxGRIDTABLE_NOTIFY_ROWS_DELETED, now, lastViewRows_ - now);
                grid->ProcessTableMessage(msg);
            }
        }
        lastViewRows_ = now;
        if (grid != nullptr) {
            grid->ForceRefresh();
        }
    }

    void captureViewRows() {
        lastViewRows_ = GetNumberRows();
    }

private:
    RegisterStore& store_;
    TableKind kind_;
    bool valuesReadOnly_ = false;
    int lastViewRows_ = 0;
};

RegisterPanel::RegisterPanel(wxWindow* parent, RegisterStore& store, TableKind kind, bool valuesReadOnly)
    : wxPanel(parent), store_(store), kind_(kind), valuesReadOnly_(valuesReadOnly) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);

    auto* addButton = new wxButton(this, wxID_ANY, U8("Добавить"));
    auto* rangeButton = new wxButton(this, wxID_ANY, U8("Добавить диапазон..."));
    auto* removeButton = new wxButton(this, wxID_ANY, U8("Удалить выбранные"));
    auto* clearButton = new wxButton(this, wxID_ANY, U8("Очистить"));

    buttons->Add(addButton, 0, wxRIGHT, 6);
    buttons->Add(rangeButton, 0, wxRIGHT, 6);
    buttons->Add(removeButton, 0, wxRIGHT, 6);
    buttons->Add(clearButton, 0);

    table_ = new RegisterGridTable(store_, kind_, valuesReadOnly_);
    grid_ = new wxGrid(this, wxID_ANY);
    grid_->SetTable(table_, true);
    table_->captureViewRows();
    grid_->SetRowLabelSize(60);
    grid_->SetDefaultRowSize(24, true);
    grid_->SetColSize(ColEnabled, 70);
    grid_->SetColSize(ColDevice, 150);
    grid_->SetColSize(ColAddress, 150);
    grid_->SetColSize(ColValue, 130);
    if (!IsBitTable(kind_)) {
        grid_->SetColSize(ColHex, 90);
    }
    grid_->SetColFormatBool(ColEnabled);
    if (IsBitTable(kind_)) {
        grid_->SetColFormatBool(ColValue);
    } else {
        grid_->SetColFormatNumber(ColValue);
    }
    grid_->SetColFormatNumber(ColDevice);
    grid_->SetColFormatNumber(ColAddress);
    grid_->EnableEditing(true);
    grid_->EnableDragColMove(false);
    grid_->SetSelectionMode(wxGrid::wxGridSelectRows);

    root->Add(buttons, 0, wxEXPAND | wxALL, 8);
    root->Add(grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    SetSizer(root);

    addButton->Bind(wxEVT_BUTTON, &RegisterPanel::onAdd, this);
    rangeButton->Bind(wxEVT_BUTTON, &RegisterPanel::onAddRange, this);
    removeButton->Bind(wxEVT_BUTTON, &RegisterPanel::onRemove, this);
    clearButton->Bind(wxEVT_BUTTON, &RegisterPanel::onClear, this);
}

void RegisterPanel::reloadFromStore() {
    if (table_ != nullptr) {
        table_->notifyChanged();
    }
}

void RegisterPanel::commitEdits() {
    if (grid_ != nullptr && grid_->IsCellEditControlEnabled()) {
        grid_->DisableCellEditControl();
    }
}

void RegisterPanel::onAdd(wxCommandEvent&) {
    if (store_.size(kind_) >= kMaxRowsPerTable) {
        wxMessageBox(U8("Достигнут предел 65536 строк."), U8("Регистры"), wxOK | wxICON_WARNING, this);
        return;
    }
    RegisterCell cell;
    cell.enabled = true;
    cell.device = 1;
    cell.address = 0;
    cell.value = 0;
    if (store_.size(kind_) > 0) {
        const auto last = store_.at(kind_, store_.size(kind_) - 1);
        cell.device = last.device;
        if (last.address < kMaxRegisterAddress) {
            cell.address = static_cast<std::uint16_t>(last.address + 1);
        } else {
            cell.address = last.address;
        }
    }
    if (store_.add(kind_, cell) == 0) {
        wxMessageBox(U8("Не удалось добавить регистр."), U8("Регистры"), wxOK | wxICON_ERROR, this);
        return;
    }
    table_->notifyChanged();
    grid_->GoToCell(grid_->GetNumberRows() - 1, ColAddress);
}

void RegisterPanel::onAddRange(wxCommandEvent&) {
    RangeDialog dialog(this);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const auto added = store_.addRange(kind_, dialog.device(), dialog.start(), dialog.count());
    table_->notifyChanged();
    if (added == 0) {
        wxMessageBox(U8("Ничего не добавлено: предел строк, выход за 65535 или такие адреса уже есть."),
            U8("Регистры"), wxOK | wxICON_INFORMATION, this);
    }
}

void RegisterPanel::onRemove(wxCommandEvent&) {
    const auto rows = selectedRows();
    if (rows.empty()) {
        return;
    }
    std::vector<std::uint64_t> ids;
    ids.reserve(rows.size());
    for (int row : rows) {
        const auto cell = store_.at(kind_, static_cast<std::size_t>(row));
        if (cell.id != 0) {
            ids.push_back(cell.id);
        }
    }
    store_.removeIds(kind_, ids);
    table_->notifyChanged();
}

void RegisterPanel::onClear(wxCommandEvent&) {
    if (store_.size(kind_) == 0) {
        return;
    }
    if (wxMessageBox(U8("Удалить все регистры на этой вкладке?"), U8("Очистить"),
            wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) {
        return;
    }
    store_.clear(kind_);
    table_->notifyChanged();
}

std::vector<int> RegisterPanel::selectedRows() const {
    std::set<int> rows;
    const wxArrayInt selected = grid_->GetSelectedRows();
    for (size_t i = 0; i < selected.size(); ++i) {
        rows.insert(selected[i]);
    }
    const auto blocksTop = grid_->GetSelectionBlockTopLeft();
    const auto blocksBottom = grid_->GetSelectionBlockBottomRight();
    for (size_t i = 0; i < blocksTop.size(); ++i) {
        for (int row = blocksTop[i].GetRow(); row <= blocksBottom[i].GetRow(); ++row) {
            rows.insert(row);
        }
    }
    if (rows.empty() && grid_->GetNumberRows() > 0 && grid_->GetGridCursorRow() >= 0) {
        rows.insert(grid_->GetGridCursorRow());
    }
    return {rows.begin(), rows.end()};
}

}  // namespace emulator
