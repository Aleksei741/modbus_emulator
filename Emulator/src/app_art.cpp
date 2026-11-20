#include "emulator/app_art.h"

#include <wx/bitmap.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/stdpaths.h>
#include <wx/window.h>

namespace emulator {
namespace {

wxFileName ArtPath(const char* fileName) {
    wxFileName path(wxStandardPaths::Get().GetExecutablePath());
    path.SetFullName(fileName);
    return path;
}

}  // namespace

void InitAppArt() {
    wxInitAllImageHandlers();
}

wxIcon LoadAppIcon() {
    InitAppArt();
    const auto path = ArtPath("modbus_icon.png");
    wxIcon icon;
    if (!path.FileExists()) {
        return icon;
    }
    wxBitmap bmp(path.GetFullPath(), wxBITMAP_TYPE_PNG);
    if (bmp.IsOk()) {
        icon.CopyFromBitmap(bmp);
    }
    return icon;
}

void ApplyAppIcon(wxWindow* frame) {
    if (auto* top = wxDynamicCast(frame, wxFrame)) {
        const wxIcon icon = LoadAppIcon();
        if (icon.IsOk()) {
            top->SetIcon(icon);
        }
    }
}

}  // namespace emulator
