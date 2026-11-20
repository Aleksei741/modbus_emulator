#include "emulator/main_frame.h"

#include <wx/app.h>

class EmulatorApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) {
            return false;
        }
        SetAppName("modbus_rtu_emulator");
        SetVendorName("modbus_emulator");
        auto* frame = new emulator::MainFrame();
        frame->Show(true);
        SetTopWindow(frame);
        return true;
    }
};

wxIMPLEMENT_APP(EmulatorApp);
