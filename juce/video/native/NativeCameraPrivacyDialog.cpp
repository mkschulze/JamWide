#include "NativeCameraPrivacyDialog.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <utility>

// Phase 19-02 Task 1 baseline: minimal stub so the editor's privacyDialog_
// unique_ptr<> resolves. Task 3 Edit 1 fills in the AlertWindow::showAsync
// invocation with the D-22 message + I-understand/Cancel buttons + the
// label-keyed isAckResult dispatch.

namespace jamwide {

void NativeCameraPrivacyDialog::show(std::function<void(bool)> /*onAck*/)
{
    // Task 3 Edit 1 implements the MessageBoxOptions + showAsync path.
}

} // namespace jamwide
