#pragma once
#include <JuceHeader.h>

class JamWideLookAndFeel : public juce::LookAndFeel_V4
{
public:
    JamWideLookAndFeel();

    // Color tokens (public for direct use in custom paint)
    static constexpr juce::uint32 kBgPrimary       = 0xff1A1D2E;
    static constexpr juce::uint32 kBgElevated       = 0xff222540;
    static constexpr juce::uint32 kSurfaceStrip     = 0xff2A2D48;
    static constexpr juce::uint32 kSurfaceChild     = 0xff252845;
    static constexpr juce::uint32 kSurfaceInput     = 0xff252840;
    static constexpr juce::uint32 kSurfaceOverlay   = 0xff1E2138;
    // REVIEW FIX: kSurfaceScrim must be AARRGGBB format for JUCE.
    // 70% black = alpha 0xB3, RGB 000000 -> 0xB3000000
    static constexpr juce::uint32 kSurfaceScrim     = 0xB3000000;
    static constexpr juce::uint32 kBorderSubtle     = 0xff3A3D58;
    static constexpr juce::uint32 kBorderFocus      = 0xff5A6080;
    static constexpr juce::uint32 kTextPrimary      = 0xffE0E0E0;
    static constexpr juce::uint32 kTextSecondary    = 0xff8888AA;
    static constexpr juce::uint32 kTextHeading      = 0xffF0F0F0;
    static constexpr juce::uint32 kAccentConnect    = 0xff40E070;
    static constexpr juce::uint32 kAccentWarning    = 0xffCCB833;
    static constexpr juce::uint32 kAccentDestructive = 0xffE04040;
    static constexpr juce::uint32 kVuNominal        = 0xff33CC33;
    static constexpr juce::uint32 kVuWarm           = 0xffE6B333;
    static constexpr juce::uint32 kVuClip           = 0xffE63333;
    static constexpr juce::uint32 kVuUnlit          = 0xff0F1520;
    static constexpr juce::uint32 kVuBackground     = 0xff0A0D18;

    // Chat message colors
    static constexpr juce::uint32 kChatRegular      = 0xffE6E6E6;
    static constexpr juce::uint32 kChatPrivate      = 0xff66E6E6;
    static constexpr juce::uint32 kChatTopic        = 0xffE6CC33;
    static constexpr juce::uint32 kChatJoin         = 0xff66E666;
    static constexpr juce::uint32 kChatPart         = 0xffB3B3B3;
    static constexpr juce::uint32 kChatAction       = 0xffE680E6;
    static constexpr juce::uint32 kChatSystem       = 0xffE64D4D;

    // Overrides
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    void drawComboBox(juce::Graphics&, int width, int height,
                      bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height,
                               juce::TextEditor&) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&,
                       int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition,
                       int thumbSize, bool isMouseOver, bool isMouseDown) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideLookAndFeel)
};
