#include "emulator/config_store.h"
#include "poller/main_frame.h"

#include <wx/app.h>

class PollerApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) {
            return false;
        }
        emulator::ConfigStore::SetAppIdentity("modbus_rtu_poller", "modbus_rtu_poller.json");
        SetAppName("modbus_rtu_poller");
        SetVendorName("modbus_emulator");
        auto* frame = new poller::MainFrame();
        frame->Show(true);
        SetTopWindow(frame);
        return true;
    }
};

wxIMPLEMENT_APP(PollerApp);
