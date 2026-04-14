#pragma once
#include <JuceHeader.h>
#include "ui/server_list_types.h"
#include "threading/ui_event.h"
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

    // Prelisten callbacks (wired by editor)
    std::function<void(const std::string& host, int port)> onListenClicked;
    std::function<void()> onStopListenClicked;

    // Prelisten state management
    void setPrelistenState(jamwide::PrelistenStatus status,
                           const std::string& host, int port);
    void setListenEnabled(bool enabled);

    // Public prelisten state (read by editor for volume sync)
    float prelistenVolume = 0.7f;

private:
    void mouseDown(const juce::MouseEvent& e) override;  // click outside dismisses

    juce::String formatAddress(int row) const;

    std::vector<ServerListEntry> servers;
    juce::String errorMessage;
    bool loading = false;
    bool showing = false;

    // Prelisten active state -- keyed by host+port, NOT row index
    // (Addresses review: prelistenRow is stale after server list refresh)
    int prelistenRow = -1;           // Derived from host+port, recalculated on every updateList
    std::string prelistenHost;       // Source of truth for active prelisten identity
    int prelistenPort = 0;           // Source of truth for active prelisten identity
    bool prelistenConnected = false; // True once PrelistenStatus::Connected received
    bool listenEnabled = true;       // False when in a non-prelisten session

    juce::Slider volumeSlider;
    juce::Label volumeLabel;

    void resolvePrelistenRow();      // Re-derive prelistenRow from host+port

    juce::ListBox listBox;
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton closeButton;
    juce::TextButton refreshButton;

    static constexpr int kDialogWidth = 600;
    static constexpr int kDialogHeight = 500;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ServerBrowserOverlay)
};
