#pragma once
#include <JuceHeader.h>
#include <functional>

class LicenseDialog : public juce::Component
{
public:
    LicenseDialog();

    void show(const juce::String& licenseText);
    void dismiss();
    bool isShowing() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void(bool accepted)> onResponse;

private:
    // REVIEW FIX: NO mouseDown override for outside-click dismiss.
    // License dialog MUST block until Accept/Decline.
    // Run thread is waiting on license_cv -- dismissing without
    // setting response would leave it hanging until timeout.

    bool showing = false;
    juce::Label titleLabel;
    juce::TextEditor licenseTextEditor;
    juce::TextButton acceptButton;
    juce::TextButton declineButton;

    static constexpr int kDialogWidth = 500;
    static constexpr int kDialogHeight = 400;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseDialog)
};
