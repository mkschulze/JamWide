#pragma once
#include <JuceHeader.h>

class JamWideJuceProcessor;  // Forward declaration (reference, not raw pointer -- review fix)

class BeatBar : public juce::Component
{
public:
    BeatBar();
    void update(int bpi, int currentBeat, int intervalPos, int intervalLen);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    // Phase 7 additions
    static constexpr int kLabelAreaWidth = 72;
    void setProcessor(JamWideJuceProcessor& p) { processorRef_ = &p; }
    void setBpm(float bpm) { currentBpm_ = bpm; }
    void triggerFlash();

private:
    int bpi_ = 0;
    int currentBeat_ = 0;
    int intervalPos_ = 0;
    int intervalLen_ = 0;

    // Phase 7: BPM/BPI label area
    float currentBpm_ = 0.0f;

    // Phase 7: Inline edit
    std::unique_ptr<juce::TextEditor> voteEditor_;
    bool editingBpm_ = false;  // true = editing BPM, false = editing BPI

    // Phase 7: Flash animation (D-10)
    double flashStartMs_ = 0.0;

    // Phase 7: Processor reference for command queue access
    // Uses pointer internally but set via reference to avoid dangling (review fix)
    JamWideJuceProcessor* processorRef_ = nullptr;

    void dismissVoteEditor();
    void createVoteEditor(bool forBpm);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatBar)
};
