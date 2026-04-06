#pragma once
#include <JuceHeader.h>
#include <functional>

class VideoPrivacyDialog : public juce::Component
{
public:
    VideoPrivacyDialog();

    /// Show the privacy modal. showBrowserWarning = true if non-Chromium detected.
    void show(bool showBrowserWarning);
    void dismiss();
    bool isShowing() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    /// Callback: true = user accepted ("I Understand -- Open Video"), false = declined ("Skip Video")
    std::function<void(bool accepted)> onResponse;

private:
    bool showing_ = false;
    bool showBrowserWarning_ = false;

    juce::Label titleLabel_;
    juce::TextEditor bodyTextEditor_;
    juce::TextButton acceptButton_;
    juce::TextButton declineButton_;

    static constexpr int kDialogWidth = 480;   // per UI-SPEC
    static constexpr int kDialogHeight = 320;  // per UI-SPEC

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoPrivacyDialog)
};
