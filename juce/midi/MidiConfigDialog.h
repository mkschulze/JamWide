#pragma once
#include <JuceHeader.h>
#include "MidiTypes.h"

class MidiMapper;
class MidiLearnManager;

class MidiConfigDialog : public juce::Component,
                          private juce::Timer
{
public:
    MidiConfigDialog(MidiMapper& mapper, MidiLearnManager* learnMgr = nullptr);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshMappingTable();
    void refreshDeviceList();

    MidiMapper& midiMapper;
    MidiLearnManager* midiLearnMgr = nullptr;

    // Section 1: Mapping table (D-03)
    juce::Label mappingHeaderLabel;
    juce::TextButton clearAllButton;
    juce::TextButton learnButton;  // Host right-click fallback (review feedback)

    // Section 2: Standalone device selector (D-05)
    juce::Label inputDeviceLabel;
    juce::ComboBox inputDeviceSelector;
    juce::Label outputDeviceLabel;
    juce::ComboBox outputDeviceSelector;

    // Section 3: Status
    juce::Label statusLabel;

    // Table data model
    struct MappingRow
    {
        juce::String paramId;
        int number;
        int midiChannel;
        MidiMsgType type = MidiMsgType::CC;
    };
    std::vector<MappingRow> mappingRows;

    // Table components (manual layout, no TableListBox to avoid complexity)
    juce::Viewport tableViewport;
    juce::Component tableContent;
    struct RowComponent : public juce::Component
    {
        juce::Label paramLabel, ccLabel, chLabel, rangeLabel;
        juce::TextButton learnBtn, deleteBtn;
        void resized() override;
    };
    juce::OwnedArray<RowComponent> rowComponents;

    void rebuildTableRows();

    // Helper: human-readable display name for paramId showing SLOT NUMBER (review feedback)
    static juce::String getDisplayNameForParam(const juce::String& paramId);

    // Helper: derive Range text from paramId
    static juce::String getRangeTextForParam(const juce::String& paramId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiConfigDialog)
};
