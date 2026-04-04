#pragma once
#include <JuceHeader.h>
#include "ui/server_list_types.h"
#include <vector>
#include <functional>

class ServerBrowserOverlay : public juce::Component,
                              public juce::ListBoxModel
{
public:
    ServerBrowserOverlay();

    void show();
    void dismiss();
    bool isShowing() const;

    void updateList(const std::vector<ServerListEntry>& servers, const juce::String& error);
    void setLoading();

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool isSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    std::function<void(const juce::String&)> onServerSelected;       // single-click (D-11)
    std::function<void(const juce::String&)> onServerDoubleClicked;  // double-click (D-12)
    std::function<void()> onDismissed;
    std::function<void()> onRefreshClicked;

private:
    void mouseDown(const juce::MouseEvent& e) override;  // click outside dismisses

    juce::String formatAddress(int row) const;

    std::vector<ServerListEntry> servers;
    juce::String errorMessage;
    bool loading = false;
    bool showing = false;

    juce::ListBox listBox;
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton closeButton;
    juce::TextButton refreshButton;

    static constexpr int kDialogWidth = 600;
    static constexpr int kDialogHeight = 500;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ServerBrowserOverlay)
};
