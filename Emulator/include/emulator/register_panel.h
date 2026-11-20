#pragma once

#include "emulator/modbus_types.h"
#include "emulator/register_store.h"

#include <wx/panel.h>

class wxButton;
class wxGrid;
class wxBoxSizer;

namespace emulator {

class RegisterGridTable;

class RegisterPanel : public wxPanel {
public:
    RegisterPanel(wxWindow* parent, RegisterStore& store, TableKind kind, bool valuesReadOnly = false);

    void reloadFromStore();
    void commitEdits();
    TableKind kind() const { return kind_; }

private:
    void onAdd(wxCommandEvent&);
    void onAddRange(wxCommandEvent&);
    void onRemove(wxCommandEvent&);
    void onClear(wxCommandEvent&);

    std::vector<int> selectedRows() const;

    RegisterStore& store_;
    TableKind kind_;
    bool valuesReadOnly_ = false;
    wxGrid* grid_ = nullptr;
    RegisterGridTable* table_ = nullptr;
};

}  // namespace emulator
