#include "MidiConfigDialog.h"
#include "midi/MidiMapper.h"
#include "midi/MidiLearnManager.h"
#include "ui/JamWideLookAndFeel.h"

//==============================================================================
void MidiConfigDialog::RowComponent::resized()
{
    auto area = getLocalBounds();
    paramLabel.setBounds(area.removeFromLeft(160));
    ccLabel.setBounds(area.removeFromLeft(40));
    chLabel.setBounds(area.removeFromLeft(35));
    rangeLabel.setBounds(area.removeFromLeft(70));
    learnBtn.setBounds(area.removeFromLeft(45));
    deleteBtn.setBounds(area.removeFromLeft(30));
}

//==============================================================================
MidiConfigDialog::MidiConfigDialog(MidiMapper& mapper, MidiLearnManager* learnMgr)
    : midiMapper(mapper), midiLearnMgr(learnMgr)
{
    // Mapping header
    mappingHeaderLabel.setText("Mappings", juce::dontSendNotification);
    mappingHeaderLabel.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
    mappingHeaderLabel.setColour(juce::Label::textColourId,
                                  juce::Colour(JamWideLookAndFeel::kTextHeading));
    addAndMakeVisible(mappingHeaderLabel);

    // Learn New button (host right-click fallback per review feedback)
    learnButton.setButtonText("Learn New...");
    learnButton.setColour(juce::TextButton::buttonColourId,
                           juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    learnButton.setColour(juce::TextButton::textColourOffId,
                           juce::Colour(JamWideLookAndFeel::kAccentConnect));
    learnButton.setTooltip("Start MIDI Learn for a parameter (host right-click fallback)");
    learnButton.onClick = [this]()
    {
        // Build menu of all APVTS params that don't have a mapping
        juce::PopupMenu menu;
        int itemId = 1;

        // Predefined param list (subset that makes sense for MIDI control)
        const juce::StringArray allParams = {
            "masterVol", "masterMute",
            "metroVol", "metroPan", "metroMute",
            "localVol_0", "localPan_0", "localMute_0", "localSolo_0",
            "localVol_1", "localPan_1", "localMute_1", "localSolo_1",
            "localVol_2", "localPan_2", "localMute_2", "localSolo_2",
            "localVol_3", "localPan_3", "localMute_3", "localSolo_3",
            "remoteVol_0", "remotePan_0", "remoteMute_0", "remoteSolo_0",
            "remoteVol_1", "remotePan_1", "remoteMute_1", "remoteSolo_1",
            "remoteVol_2", "remotePan_2", "remoteMute_2", "remoteSolo_2",
            "remoteVol_3", "remotePan_3", "remoteMute_3", "remoteSolo_3",
            "remoteVol_4", "remotePan_4", "remoteMute_4", "remoteSolo_4",
            "remoteVol_5", "remotePan_5", "remoteMute_5", "remoteSolo_5",
            "remoteVol_6", "remotePan_6", "remoteMute_6", "remoteSolo_6",
            "remoteVol_7", "remotePan_7", "remoteMute_7", "remoteSolo_7",
            "remoteVol_8", "remotePan_8", "remoteMute_8", "remoteSolo_8",
            "remoteVol_9", "remotePan_9", "remoteMute_9", "remoteSolo_9",
            "remoteVol_10", "remotePan_10", "remoteMute_10", "remoteSolo_10",
            "remoteVol_11", "remotePan_11", "remoteMute_11", "remoteSolo_11",
            "remoteVol_12", "remotePan_12", "remoteMute_12", "remoteSolo_12",
            "remoteVol_13", "remotePan_13", "remoteMute_13", "remoteSolo_13",
            "remoteVol_14", "remotePan_14", "remoteMute_14", "remoteSolo_14",
            "remoteVol_15", "remotePan_15", "remoteMute_15", "remoteSolo_15"
        };

        std::vector<juce::String> unmapped;
        for (const auto& paramId : allParams)
        {
            if (!midiMapper.hasMapping(paramId))
            {
                unmapped.push_back(paramId);
                menu.addItem(itemId++, getDisplayNameForParam(paramId));
            }
        }

        if (unmapped.empty())
        {
            menu.addItem(-1, "All parameters mapped", false);
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&learnButton),
            [this, unmapped](int result) {
                if (result <= 0 || result > static_cast<int>(unmapped.size()))
                    return;
                const auto& paramId = unmapped[static_cast<size_t>(result - 1)];
                if (midiLearnMgr != nullptr)
                {
                    // Start MIDI Learn for selected parameter (host right-click fallback)
                    midiLearnMgr->startLearning(paramId,
                        [this, paramId](int cc, int ch) {
                            midiMapper.addMapping(paramId, cc, ch);
                            refreshMappingTable();
                        });
                }
            });
    };
    addAndMakeVisible(learnButton);

    // Clear All button
    clearAllButton.setButtonText("Clear All");
    clearAllButton.setColour(juce::TextButton::buttonColourId,
                              juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    clearAllButton.setColour(juce::TextButton::textColourOffId,
                              juce::Colour(JamWideLookAndFeel::kAccentDestructive));
    clearAllButton.onClick = [this]()
    {
        // Confirmation dialog per UI-SPEC
        auto options = juce::MessageBoxOptions()
            .withTitle("Clear All MIDI Mappings")
            .withMessage("Remove all MIDI mappings?")
            .withButton("Clear All")
            .withButton("Keep Mappings");
        juce::AlertWindow::showAsync(options, [this](int result) {
            if (result == 1)  // "Clear All" button
            {
                midiMapper.clearAllMappings();
                refreshMappingTable();
            }
        });
    };
    addAndMakeVisible(clearAllButton);

    // Device selectors (standalone only)
    inputDeviceLabel.setText("MIDI Input", juce::dontSendNotification);
    inputDeviceLabel.setFont(juce::FontOptions(11.0f));
    inputDeviceLabel.setColour(juce::Label::textColourId,
                                juce::Colour(JamWideLookAndFeel::kTextPrimary));
    addAndMakeVisible(inputDeviceLabel);

    inputDeviceSelector.setColour(juce::ComboBox::backgroundColourId,
                                   juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    inputDeviceSelector.setColour(juce::ComboBox::textColourId,
                                   juce::Colour(JamWideLookAndFeel::kTextPrimary));
    inputDeviceSelector.setColour(juce::ComboBox::outlineColourId,
                                   juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    inputDeviceSelector.onChange = [this]() {
        int idx = inputDeviceSelector.getSelectedId();
        if (idx == 1)
            midiMapper.closeMidiInput();
        else
        {
            auto devices = juce::MidiInput::getAvailableDevices();
            int deviceIdx = idx - 2;
            if (deviceIdx >= 0 && deviceIdx < devices.size())
                midiMapper.openMidiInput(devices[deviceIdx].identifier);
        }
    };
    addAndMakeVisible(inputDeviceSelector);

    outputDeviceLabel.setText("MIDI Output", juce::dontSendNotification);
    outputDeviceLabel.setFont(juce::FontOptions(11.0f));
    outputDeviceLabel.setColour(juce::Label::textColourId,
                                 juce::Colour(JamWideLookAndFeel::kTextPrimary));
    addAndMakeVisible(outputDeviceLabel);

    outputDeviceSelector.setColour(juce::ComboBox::backgroundColourId,
                                    juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    outputDeviceSelector.setColour(juce::ComboBox::textColourId,
                                    juce::Colour(JamWideLookAndFeel::kTextPrimary));
    outputDeviceSelector.setColour(juce::ComboBox::outlineColourId,
                                    juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    outputDeviceSelector.onChange = [this]() {
        int idx = outputDeviceSelector.getSelectedId();
        if (idx == 1)
            midiMapper.closeMidiOutput();
        else
        {
            auto devices = juce::MidiOutput::getAvailableDevices();
            int deviceIdx = idx - 2;
            if (deviceIdx >= 0 && deviceIdx < devices.size())
                midiMapper.openMidiOutput(devices[deviceIdx].identifier);
        }
    };
    addAndMakeVisible(outputDeviceSelector);

    // Status label
    statusLabel.setFont(juce::FontOptions(9.0f));
    statusLabel.setColour(juce::Label::textColourId,
                           juce::Colour(JamWideLookAndFeel::kTextSecondary));
    addAndMakeVisible(statusLabel);

    // Table viewport
    addAndMakeVisible(tableViewport);
    tableViewport.setViewedComponent(&tableContent, false);
    tableViewport.setScrollBarsShown(true, false);

    // Populate device lists
    refreshDeviceList();

    // Initial table populate
    refreshMappingTable();

    // Start timer for periodic refresh
    startTimer(500);
}

void MidiConfigDialog::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kBgElevated));
}

void MidiConfigDialog::resized()
{
    auto area = getLocalBounds().reduced(16);

    // Header row: "Mappings" label + "Learn New..." button right-aligned
    auto headerRow = area.removeFromTop(24);
    learnButton.setBounds(headerRow.removeFromRight(100));
    headerRow.removeFromRight(8);
    mappingHeaderLabel.setBounds(headerRow);

    area.removeFromTop(8);

    // Clear All button row
    auto clearRow = area.removeFromBottom(30);
    clearAllButton.setBounds(clearRow.removeFromRight(80));

    // Status label at very bottom
    auto statusRow = area.removeFromBottom(16);
    statusLabel.setBounds(statusRow);
    area.removeFromBottom(4);

    // Device section at bottom (if standalone)
    #if JucePlugin_Build_Standalone
    auto deviceSection = area.removeFromBottom(90);
    deviceSection.removeFromTop(8);  // separator gap

    auto inputRow = deviceSection.removeFromTop(16);
    inputDeviceLabel.setBounds(inputRow);
    auto inputSelectorRow = deviceSection.removeFromTop(24);
    inputDeviceSelector.setBounds(inputSelectorRow);
    deviceSection.removeFromTop(8);
    auto outputRow = deviceSection.removeFromTop(16);
    outputDeviceLabel.setBounds(outputRow);
    auto outputSelectorRow = deviceSection.removeFromTop(24);
    outputDeviceSelector.setBounds(outputSelectorRow);
    #else
    inputDeviceLabel.setVisible(false);
    inputDeviceSelector.setVisible(false);
    outputDeviceLabel.setVisible(false);
    outputDeviceSelector.setVisible(false);
    #endif

    area.removeFromBottom(8);

    // Column headers
    auto colHeaderRow = area.removeFromTop(18);
    {
        auto g_area = colHeaderRow;
        // Just draw column headers as text in paint if needed,
        // or we can add labels. For now they're part of the table
        // display and the first row component will serve as reference.
    }
    juce::ignoreUnused(colHeaderRow);

    // Remaining area for table viewport
    tableViewport.setBounds(area);

    // Resize table content
    int rowHeight = 24;
    int tableHeight = juce::jmax(rowHeight, static_cast<int>(rowComponents.size()) * rowHeight);
    tableContent.setBounds(0, 0, area.getWidth() - 12, tableHeight);  // -12 for scrollbar

    int y = 0;
    for (auto* row : rowComponents)
    {
        row->setBounds(0, y, tableContent.getWidth(), rowHeight);
        y += rowHeight;
    }
}

void MidiConfigDialog::timerCallback()
{
    refreshMappingTable();

    // Update status
    int count = midiMapper.getMappingCount();
    auto status = midiMapper.getStatus();
    juce::String statusText = juce::String(count) + " mapping" + (count != 1 ? "s" : "");
    if (status == MidiMapper::Status::Healthy)
        statusText += " | Receiving MIDI";
    else if (status == MidiMapper::Status::Failed)
        statusText += " | Device error";
    statusLabel.setText(statusText, juce::dontSendNotification);
}

void MidiConfigDialog::refreshMappingTable()
{
    auto mappings = midiMapper.getAllMappings();

    // Check if mappings changed
    bool changed = (mappings.size() != mappingRows.size());
    if (!changed)
    {
        for (size_t i = 0; i < mappings.size(); ++i)
        {
            if (mappings[i].paramId != mappingRows[i].paramId
                || mappings[i].ccNumber != mappingRows[i].ccNumber
                || mappings[i].midiChannel != mappingRows[i].midiChannel)
            {
                changed = true;
                break;
            }
        }
    }

    if (!changed) return;

    mappingRows.clear();
    for (const auto& m : mappings)
        mappingRows.push_back({m.paramId, m.ccNumber, m.midiChannel});

    rebuildTableRows();
    resized();
}

void MidiConfigDialog::rebuildTableRows()
{
    rowComponents.clear();
    tableContent.removeAllChildren();

    for (size_t i = 0; i < mappingRows.size(); ++i)
    {
        const auto& row = mappingRows[i];
        auto* comp = rowComponents.add(new RowComponent());

        // Alternating row background via custom paint override not needed;
        // we'll use label backgrounds directly
        bool altRow = (i % 2 == 0);
        auto bgColour = altRow ? juce::Colour(JamWideLookAndFeel::kSurfaceStrip)
                               : juce::Colour(JamWideLookAndFeel::kBgPrimary);

        // Parameter name (slot-labeled per review feedback)
        comp->paramLabel.setText(getDisplayNameForParam(row.paramId), juce::dontSendNotification);
        comp->paramLabel.setFont(juce::FontOptions(12.0f));
        comp->paramLabel.setColour(juce::Label::textColourId,
                                    juce::Colour(JamWideLookAndFeel::kTextPrimary));
        comp->paramLabel.setColour(juce::Label::backgroundColourId, bgColour);
        comp->addAndMakeVisible(comp->paramLabel);

        // CC#
        comp->ccLabel.setText(juce::String(row.ccNumber), juce::dontSendNotification);
        comp->ccLabel.setFont(juce::FontOptions(12.0f));
        comp->ccLabel.setColour(juce::Label::textColourId,
                                 juce::Colour(JamWideLookAndFeel::kTextPrimary));
        comp->ccLabel.setColour(juce::Label::backgroundColourId, bgColour);
        comp->ccLabel.setJustificationType(juce::Justification::centred);
        comp->addAndMakeVisible(comp->ccLabel);

        // Channel
        comp->chLabel.setText(juce::String(row.midiChannel), juce::dontSendNotification);
        comp->chLabel.setFont(juce::FontOptions(12.0f));
        comp->chLabel.setColour(juce::Label::textColourId,
                                 juce::Colour(JamWideLookAndFeel::kTextPrimary));
        comp->chLabel.setColour(juce::Label::backgroundColourId, bgColour);
        comp->chLabel.setJustificationType(juce::Justification::centred);
        comp->addAndMakeVisible(comp->chLabel);

        // Range
        comp->rangeLabel.setText(getRangeTextForParam(row.paramId), juce::dontSendNotification);
        comp->rangeLabel.setFont(juce::FontOptions(10.0f));
        comp->rangeLabel.setColour(juce::Label::textColourId,
                                    juce::Colour(JamWideLookAndFeel::kTextSecondary));
        comp->rangeLabel.setColour(juce::Label::backgroundColourId, bgColour);
        comp->rangeLabel.setJustificationType(juce::Justification::centred);
        comp->addAndMakeVisible(comp->rangeLabel);

        // Learn button (re-learn to different CC)
        comp->learnBtn.setButtonText("L");
        comp->learnBtn.setColour(juce::TextButton::buttonColourId, bgColour);
        comp->learnBtn.setColour(juce::TextButton::textColourOffId,
                                  juce::Colour(JamWideLookAndFeel::kAccentConnect));
        comp->learnBtn.setTooltip("Re-learn MIDI CC for this parameter");
        juce::String paramIdCopy = row.paramId;
        comp->learnBtn.onClick = [this, paramIdCopy]() {
            if (midiLearnMgr != nullptr)
            {
                // Remove existing mapping, then start learn for re-assignment
                midiMapper.removeMapping(paramIdCopy);
                midiLearnMgr->startLearning(paramIdCopy,
                    [this, paramIdCopy](int cc, int ch) {
                        midiMapper.addMapping(paramIdCopy, cc, ch);
                        refreshMappingTable();
                    });
            }
        };
        comp->addAndMakeVisible(comp->learnBtn);

        // Delete button
        comp->deleteBtn.setButtonText("X");
        comp->deleteBtn.setColour(juce::TextButton::buttonColourId, bgColour);
        comp->deleteBtn.setColour(juce::TextButton::textColourOffId,
                                   juce::Colour(JamWideLookAndFeel::kAccentDestructive));
        comp->deleteBtn.setTooltip("Remove mapping");
        comp->deleteBtn.onClick = [this, paramIdCopy]() {
            midiMapper.removeMapping(paramIdCopy);
            refreshMappingTable();
        };
        comp->addAndMakeVisible(comp->deleteBtn);

        tableContent.addAndMakeVisible(comp);
    }

    // Empty state
    if (mappingRows.empty())
    {
        auto* emptyComp = rowComponents.add(new RowComponent());
        emptyComp->paramLabel.setText("No MIDI mappings", juce::dontSendNotification);
        emptyComp->paramLabel.setFont(juce::FontOptions(13.0f));
        emptyComp->paramLabel.setColour(juce::Label::textColourId,
                                         juce::Colour(JamWideLookAndFeel::kTextSecondary));
        emptyComp->paramLabel.setJustificationType(juce::Justification::centred);
        emptyComp->addAndMakeVisible(emptyComp->paramLabel);
        tableContent.addAndMakeVisible(emptyComp);
    }
}

void MidiConfigDialog::refreshDeviceList()
{
    // Input devices
    inputDeviceSelector.clear(juce::dontSendNotification);
    inputDeviceSelector.addItem("None", 1);
    auto inputDevices = juce::MidiInput::getAvailableDevices();
    int selectedInputId = 1;  // default to "None"
    auto currentInputId = midiMapper.getInputDeviceId();
    for (int i = 0; i < inputDevices.size(); ++i)
    {
        inputDeviceSelector.addItem(inputDevices[i].name, i + 2);
        if (currentInputId.isNotEmpty() && inputDevices[i].identifier == currentInputId)
            selectedInputId = i + 2;
    }
    inputDeviceSelector.setSelectedId(selectedInputId, juce::dontSendNotification);

    // Output devices
    outputDeviceSelector.clear(juce::dontSendNotification);
    outputDeviceSelector.addItem("None", 1);
    auto outputDevices = juce::MidiOutput::getAvailableDevices();
    int selectedOutputId = 1;  // default to "None"
    auto currentOutputId = midiMapper.getOutputDeviceId();
    for (int i = 0; i < outputDevices.size(); ++i)
    {
        outputDeviceSelector.addItem(outputDevices[i].name, i + 2);
        if (currentOutputId.isNotEmpty() && outputDevices[i].identifier == currentOutputId)
            selectedOutputId = i + 2;
    }
    outputDeviceSelector.setSelectedId(selectedOutputId, juce::dontSendNotification);
}

//==============================================================================
juce::String MidiConfigDialog::getDisplayNameForParam(const juce::String& paramId)
{
    // Remote params: show "Remote Slot N" (not username) per review feedback
    if (paramId.startsWith("remoteVol_"))
        return "Remote Slot " + juce::String(paramId.getTrailingIntValue() + 1) + " Volume";
    if (paramId.startsWith("remotePan_"))
        return "Remote Slot " + juce::String(paramId.getTrailingIntValue() + 1) + " Pan";
    if (paramId.startsWith("remoteMute_"))
        return "Remote Slot " + juce::String(paramId.getTrailingIntValue() + 1) + " Mute";
    if (paramId.startsWith("remoteSolo_"))
        return "Remote Slot " + juce::String(paramId.getTrailingIntValue() + 1) + " Solo";
    // Local params
    if (paramId.startsWith("localVol_"))
        return "Local Ch " + juce::String(paramId.getTrailingIntValue() + 1) + " Volume";
    if (paramId.startsWith("localPan_"))
        return "Local Ch " + juce::String(paramId.getTrailingIntValue() + 1) + " Pan";
    if (paramId.startsWith("localMute_"))
        return "Local Ch " + juce::String(paramId.getTrailingIntValue() + 1) + " Mute";
    if (paramId.startsWith("localSolo_"))
        return "Local Ch " + juce::String(paramId.getTrailingIntValue() + 1) + " Solo";
    // Master/Metro
    if (paramId == "masterVol") return "Master Volume";
    if (paramId == "masterMute") return "Master Mute";
    if (paramId == "metroVol") return "Metronome Volume";
    if (paramId == "metroPan") return "Metronome Pan";
    if (paramId == "metroMute") return "Metronome Mute";
    return paramId;  // fallback: raw paramId
}

juce::String MidiConfigDialog::getRangeTextForParam(const juce::String& paramId)
{
    if (paramId.contains("Vol")) return "0-2 vol";
    if (paramId.contains("Pan")) return "+/-1 pan";
    if (paramId.contains("Mute") || paramId.contains("Solo")) return "toggle";
    return "0-1";
}
