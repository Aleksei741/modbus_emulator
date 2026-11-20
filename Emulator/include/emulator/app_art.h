#pragma once

class wxIcon;
class wxWindow;

namespace emulator {

void InitAppArt();
wxIcon LoadAppIcon();
void ApplyAppIcon(wxWindow* frame);

}  // namespace emulator
