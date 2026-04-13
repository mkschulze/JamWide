# Audio Prelisten Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users hear what's happening in a NINJAM server room before joining, via a "Listen" button in the server browser.

**Architecture:** Reuse the main NJClient in a receive-only mode — zero local channels, `justmonitor=true`, auto-accept license, auto-subscribe all channels. One room at a time. Audio mixes into plugin output with a dedicated prelisten volume control.

**Tech Stack:** JUCE 8, C++20, NJClient (NINJAM protocol), existing SpscRing command/event queues

**Spec:** `docs/superpowers/specs/2026-04-13-audio-prelisten-design.md`

---

## File Structure

| File | Responsibility | Action |
|------|---------------|--------|
| `src/threading/ui_command.h` | Command types for prelisten start/stop | Modify: add 2 structs + variant entries |
| `src/threading/ui_event.h` | Event type for prelisten state feedback | Modify: add 1 struct + variant entry |
| `juce/JamWideJuceProcessor.h` | Prelisten state flags on processor | Modify: add 2 atomic members |
| `juce/JamWideJuceProcessor.cpp` | Scale output by prelisten volume | Modify: processBlock() |
| `juce/NinjamRunThread.cpp` | Handle prelisten connect differently | Modify: processCommands(), status handling |
| `juce/ui/ServerBrowserOverlay.h` | Listen button, volume slider, active state | Modify: add members, callbacks |
| `juce/ui/ServerBrowserOverlay.cpp` | Render listen buttons, active row, volume slider | Modify: paintListBoxItem(), resized(), constructor |
| `juce/JamWideJuceEditor.cpp` | Wire prelisten commands/events between overlay and processor | Modify: drainEvents(), constructor |

---

### Task 1: Add Prelisten Command and Event Types

**Files:**
- Modify: `src/threading/ui_command.h:14-110`
- Modify: `src/threading/ui_event.h:17-111`

- [ ] **Step 1: Add PrelistenCommand and StopPrelistenCommand structs**

In `src/threading/ui_command.h`, add after the `SyncDisableCommand` struct (before the `UiCommand` variant):

```cpp
struct PrelistenCommand {
    std::string host;
    int port = 2049;
};

struct StopPrelistenCommand {};
```

- [ ] **Step 2: Add to UiCommand variant**

In `src/threading/ui_command.h`, add `PrelistenCommand` and `StopPrelistenCommand` to the `UiCommand` variant:

```cpp
using UiCommand = std::variant<
    ConnectCommand,
    DisconnectCommand,
    SetLocalChannelInfoCommand,
    SetLocalChannelMonitoringCommand,
    SetUserStateCommand,
    SetUserChannelStateCommand,
    RequestServerListCommand,
    SendChatCommand,
    SetEncoderFormatCommand,
    SetRoutingModeCommand,
    SyncCommand,
    SyncCancelCommand,
    SyncDisableCommand,
    PrelistenCommand,
    StopPrelistenCommand
>;
```

- [ ] **Step 3: Add PrelistenStateEvent struct**

In `src/threading/ui_event.h`, add after the `SyncStateChangedEvent` struct:

```cpp
struct PrelistenStateEvent {
    bool connected = false;
    std::string server_name;
};
```

- [ ] **Step 4: Add to UiEvent variant**

In `src/threading/ui_event.h`, add `PrelistenStateEvent` to the variant:

```cpp
using UiEvent = std::variant<
    ChatMessageEvent,
    StatusChangedEvent,
    UserInfoChangedEvent,
    TopicChangedEvent,
    ServerListEvent,
    BpmChangedEvent,
    BpiChangedEvent,
    SyncStateChangedEvent,
    PrelistenStateEvent
>;
```

- [ ] **Step 5: Verify compilation**

Run: `cmake --build build --target JamWideJuce_VST3 -j$(sysctl -n hw.ncpu) 2>&1 | tail -5`
Expected: Build succeeds (types are added but not yet used)

- [ ] **Step 6: Commit**

```bash
git add src/threading/ui_command.h src/threading/ui_event.h
git commit -m "feat(prelisten): add PrelistenCommand, StopPrelistenCommand, and PrelistenStateEvent types"
```

---

### Task 2: Add Prelisten State to Processor

**Files:**
- Modify: `juce/JamWideJuceProcessor.h:155-190`
- Modify: `juce/JamWideJuceProcessor.cpp:456-510`

- [ ] **Step 1: Add prelisten atomic flags to processor header**

In `juce/JamWideJuceProcessor.h`, add after the `cachedHostBpm_` declaration (around line 158):

```cpp
    // ── Prelisten state ──
    std::atomic<bool>  prelisten_mode{false};
    std::atomic<float> prelisten_volume{0.7f};
```

- [ ] **Step 2: Scale output by prelisten_volume in processBlock()**

In `juce/JamWideJuceProcessor.cpp`, inside `processBlock()`, add prelisten volume scaling after the `accumulateBusesToMainMix()` call and before `routeOutputsToJuceBuses()`. Find lines 490-491:

```cpp
    accumulateBusesToMainMix(outPtrs, numSamples);
    routeOutputsToJuceBuses(buffer, numSamples);
```

Insert between them:

```cpp
    // Scale prelisten audio (prelisten connects with justmonitor so only remote
    // audio is present — apply the dedicated prelisten volume knob)
    if (prelisten_mode.load(std::memory_order_relaxed))
    {
        const float pv = prelisten_volume.load(std::memory_order_relaxed);
        for (int ch = 0; ch < kTotalOutChannels; ++ch)
            juce::FloatVectorOperations::multiply(outPtrs[ch], pv, numSamples);
    }
```

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build --target JamWideJuce_VST3 -j$(sysctl -n hw.ncpu) 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add juce/JamWideJuceProcessor.h juce/JamWideJuceProcessor.cpp
git commit -m "feat(prelisten): add prelisten_mode and prelisten_volume to processor, scale output in processBlock"
```

---

### Task 3: Handle Prelisten Commands in NinjamRunThread

**Files:**
- Modify: `juce/NinjamRunThread.cpp:260-288` (local channel setup)
- Modify: `juce/NinjamRunThread.cpp:149-192` (license callback)
- Modify: `juce/NinjamRunThread.cpp:200-250` (processCommands)

- [ ] **Step 1: Add PrelistenCommand handler in processCommands()**

In `juce/NinjamRunThread.cpp`, inside the `processCommands()` method, find the block that handles `DisconnectCommand` via `std::visit`. Add handlers for `PrelistenCommand` and `StopPrelistenCommand` in the visitor. Add these cases:

```cpp
            else if constexpr (std::is_same_v<T, jamwide::PrelistenCommand>)
            {
                // Disconnect any existing connection first
                if (client->GetStatus() >= 0)
                    client->Disconnect();

                processor.prelisten_mode.store(true, std::memory_order_release);

                // Connect with [preview] prefix, empty password
                std::string addr = c.host + ":" + std::to_string(c.port);
                std::string username = "[preview]" + std::string(client->GetUser());
                client->Connect(addr.c_str(), username.c_str(), "");
            }
            else if constexpr (std::is_same_v<T, jamwide::StopPrelistenCommand>)
            {
                if (processor.prelisten_mode.load(std::memory_order_acquire))
                {
                    client->Disconnect();
                    processor.prelisten_mode.store(false, std::memory_order_release);
                    processor.evt_queue.try_push(jamwide::PrelistenStateEvent{false, ""});
                }
            }
```

- [ ] **Step 2: Skip local channel setup when in prelisten mode**

In `juce/NinjamRunThread.cpp`, find the local channel setup block (around line 260) that runs when `currentStatus == NJClient::NJC_STATUS_OK`. Wrap the existing `SetLocalChannelInfo()` calls in a guard:

```cpp
if (currentStatus == NJClient::NJC_STATUS_OK)
{
    if (processor.prelisten_mode.load(std::memory_order_acquire))
    {
        // Prelisten: no local channels, just notify UI
        processor.evt_queue.try_push(jamwide::PrelistenStateEvent{true, client->GetHostName() ? client->GetHostName() : ""});
    }
    else
    {
        // Normal connection: set up local channels
        client->SetLocalChannelInfo(0, "Ch1",
            true, processor.localInputSelector[0] * 2 | (1 << 10),
            true, 256,
            true, processor.localTransmit[0]);

        for (int ch = 1; ch < 4; ++ch)
        {
            juce::String name = "Ch" + juce::String(ch + 1);
            int srcch = processor.localInputSelector[ch] * 2 | (1 << 10);
            client->SetLocalChannelInfo(ch, name.toRawUTF8(),
                true, srcch, true, 256, true, processor.localTransmit[ch]);
        }

        int rm = processor.routingMode.load(std::memory_order_relaxed);
        client->config_remote_autochan = rm;
        client->config_remote_autochan_nch = (rm > 0) ? 32 : 0;
        client->SetMetronomeChannel(32);
    }
}
```

- [ ] **Step 3: Auto-accept license in prelisten mode**

In `juce/NinjamRunThread.cpp`, at the top of the `license_callback()` function (around line 151), add an early return for prelisten mode:

```cpp
int license_callback(void* user_data, const char* license_text)
{
    auto& proc = *static_cast<JamWideJuceProcessor*>(user_data);

    // Auto-accept license in prelisten mode (no UI dialog)
    if (proc.prelisten_mode.load(std::memory_order_acquire))
        return 1;

    // ... existing license callback code unchanged ...
```

- [ ] **Step 4: Handle prelisten connection failure**

In `juce/NinjamRunThread.cpp`, in the status-change handling block (where `currentStatus` transitions are detected), add a check for prelisten connection failures. When the client transitions to a negative status while in prelisten mode, emit a failure event and clear the mode:

```cpp
            if (currentStatus < 0 && processor.prelisten_mode.load(std::memory_order_acquire))
            {
                // Prelisten connection failed — clean up silently
                processor.prelisten_mode.store(false, std::memory_order_release);
                processor.evt_queue.try_push(jamwide::PrelistenStateEvent{false, ""});
            }
```

- [ ] **Step 5: Clear prelisten mode on normal ConnectCommand**

In the existing `ConnectCommand` handler in `processCommands()`, add a line to clear prelisten mode before connecting normally:

```cpp
            if constexpr (std::is_same_v<T, jamwide::ConnectCommand>)
            {
                // Clear prelisten if active — user is joining for real
                processor.prelisten_mode.store(false, std::memory_order_release);

                // ... existing connect logic ...
```

- [ ] **Step 6: Verify compilation**

Run: `cmake --build build --target JamWideJuce_VST3 -j$(sysctl -n hw.ncpu) 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 7: Commit**

```bash
git add juce/NinjamRunThread.cpp
git commit -m "feat(prelisten): handle PrelistenCommand in NinjamRunThread — zero channels, auto-accept license"
```

---

### Task 4: Add Listen Button and Active State to Server Browser UI

**Files:**
- Modify: `juce/ui/ServerBrowserOverlay.h:7-54`
- Modify: `juce/ui/ServerBrowserOverlay.cpp:1-231`

- [ ] **Step 1: Add prelisten state and callbacks to header**

In `juce/ui/ServerBrowserOverlay.h`, add new members after the existing callback declarations (after line 32):

```cpp
    std::function<void(const std::string& host, int port)> onListenClicked;
    std::function<void()> onStopListenClicked;
```

Add new state after `showing` (after line 43):

```cpp
    int prelistenRow = -1;          // -1 = not prelistening
    float prelistenVolume = 0.7f;
    juce::Slider volumeSlider;
    juce::Label volumeLabel;
```

Add public methods:

```cpp
    void setPrelistenState(bool active, const juce::String& serverName);
```

- [ ] **Step 2: Initialize volume slider in constructor**

In `juce/ui/ServerBrowserOverlay.cpp`, in the constructor, after the `refreshButton` setup (around line 28), add:

```cpp
    volumeLabel.setText("VOL", juce::dontSendNotification);
    volumeLabel.setFont(juce::FontOptions(10.0f));
    volumeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8888ff));
    volumeLabel.setJustificationType(juce::Justification::centredRight);
    addChildComponent(volumeLabel);  // hidden until prelistening

    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(0.7, juce::dontSendNotification);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff8888ff));
    volumeSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffaaccff));
    volumeSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0x26ffffff));
    volumeSlider.onValueChange = [this]() {
        prelistenVolume = static_cast<float>(volumeSlider.getValue());
    };
    addChildComponent(volumeSlider);  // hidden until prelistening
```

- [ ] **Step 3: Layout volume slider in resized()**

In `juce/ui/ServerBrowserOverlay.cpp`, in `resized()`, modify the title row layout. After `refreshButton.setBounds(...)` and before `titleLabel.setBounds(titleRow)`, add:

```cpp
    // Volume slider (only visible when prelistening, between title and refresh)
    if (prelistenRow >= 0)
    {
        auto volArea = titleRow.removeFromRight(90);
        titleRow.removeFromRight(4);
        volumeLabel.setBounds(volArea.removeFromLeft(28).withTrimmedTop(6).withTrimmedBottom(6));
        volumeSlider.setBounds(volArea.withTrimmedTop(6).withTrimmedBottom(6));
    }
```

- [ ] **Step 4: Render listen buttons and active row state in paintListBoxItem()**

In `juce/ui/ServerBrowserOverlay.cpp`, in `paintListBoxItem()`, replace the existing selection highlight block (around lines 167-170) and add listen button rendering. After the bottom border drawing (line 228-230), add:

```cpp
    // Active prelisten row: blue tint background + left edge indicator
    if (row == prelistenRow)
    {
        g.setColour(juce::Colour(0x14648cff));  // subtle blue tint
        g.fillRect(0, 0, width, height);

        // Animated left edge bar (3px)
        g.setColour(juce::Colour(0xff8888ff));
        g.fillRect(0, 0, 3, height);

        // "LISTENING" badge next to server name
        auto badgeFont = juce::FontOptions(9.0f);
        g.setFont(badgeFont);
        g.setColour(juce::Colour(0xffaaccff));
        auto nameWidth = juce::Font(juce::FontOptions(13.0f, juce::Font::bold))
            .getStringWidth(juce::String(entry.name));
        g.setColour(juce::Colour(0x4d648cff));  // badge background
        int badgeX = 8 + nameWidth + 8;
        g.fillRoundedRectangle(static_cast<float>(badgeX), 3.0f, 62.0f, 14.0f, 3.0f);
        g.setColour(juce::Colour(0xffaaccff));
        g.drawText("LISTENING", badgeX, 3, 62, 14, juce::Justification::centred, false);
    }

    // Listen/Stop button (bottom-right of row, only for rooms with users)
    if (entry.users > 0)
    {
        int btnX = width - 70;
        int btnY = height - 22;
        int btnW = 62;
        int btnH = 16;

        if (row == prelistenRow)
        {
            // Stop button (active state)
            g.setColour(juce::Colour(0x40648cff));
            g.fillRoundedRectangle(static_cast<float>(btnX), static_cast<float>(btnY),
                                   static_cast<float>(btnW), static_cast<float>(btnH), 4.0f);
            g.setColour(juce::Colour(0xff8888ff));
            g.drawRoundedRectangle(static_cast<float>(btnX), static_cast<float>(btnY),
                                   static_cast<float>(btnW), static_cast<float>(btnH), 4.0f, 1.0f);
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(juce::CharPointer_UTF8("\xe2\x96\xa0 Stop"), btnX, btnY, btnW, btnH,
                       juce::Justification::centred, false);
        }
        else
        {
            // Listen button (idle state)
            g.setColour(juce::Colour(0x0fffffff));
            g.fillRoundedRectangle(static_cast<float>(btnX), static_cast<float>(btnY),
                                   static_cast<float>(btnW), static_cast<float>(btnH), 4.0f);
            g.setColour(juce::Colour(0x1fffffff));
            g.drawRoundedRectangle(static_cast<float>(btnX), static_cast<float>(btnY),
                                   static_cast<float>(btnW), static_cast<float>(btnH), 4.0f, 1.0f);
            g.setFont(juce::FontOptions(10.0f));
            g.setColour(juce::Colour(0xff888888));
            g.drawText(juce::CharPointer_UTF8("\xe2\x96\xb6 Listen"), btnX, btnY, btnW, btnH,
                       juce::Justification::centred, false);
        }
    }
```

- [ ] **Step 5: Handle listen button clicks in mouseDown()**

In `juce/ui/ServerBrowserOverlay.cpp`, add a `mouseDown` handler (or modify the existing `listBoxItemClicked`/`listBoxItemDoubleClicked` methods). Override `listBoxItemClicked` to detect clicks in the button region:

Add a new method to handle the listen button hit test. In the existing `listBoxItemClicked()` method (or add one if it delegates to callbacks directly), add logic to check if the click was in the button area:

```cpp
void ServerBrowserOverlay::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= static_cast<int>(servers.size()))
        return;

    const auto& entry = servers[static_cast<size_t>(row)];

    // Check if click is in the listen/stop button area (bottom-right 62x16)
    auto rowBounds = listBox.getRowPosition(row, true);
    int btnX = rowBounds.getWidth() - 70;
    int btnY = rowBounds.getHeight() - 22;
    auto localPoint = e.getPosition();

    if (entry.users > 0 &&
        localPoint.x >= btnX && localPoint.x <= btnX + 62 &&
        localPoint.y >= btnY && localPoint.y <= btnY + 16)
    {
        if (row == prelistenRow)
        {
            // Stop prelistening
            prelistenRow = -1;
            volumeSlider.setVisible(false);
            volumeLabel.setVisible(false);
            resized();
            listBox.repaint();
            if (onStopListenClicked) onStopListenClicked();
        }
        else
        {
            // Start prelistening to this room
            prelistenRow = row;
            volumeSlider.setVisible(true);
            volumeLabel.setVisible(true);
            resized();
            listBox.repaint();
            if (onListenClicked) onListenClicked(entry.host, entry.port);
        }
        return;
    }

    // Default: fill address bar (existing behavior)
    if (onServerSelected)
        onServerSelected(formatAddress(row));
}
```

- [ ] **Step 6: Implement setPrelistenState()**

In `juce/ui/ServerBrowserOverlay.cpp`, add:

```cpp
void ServerBrowserOverlay::setPrelistenState(bool active, const juce::String& serverName)
{
    if (!active)
    {
        prelistenRow = -1;
        volumeSlider.setVisible(false);
        volumeLabel.setVisible(false);
    }
    else
    {
        // Find the row matching this server name
        for (int i = 0; i < static_cast<int>(servers.size()); ++i)
        {
            if (juce::String(servers[static_cast<size_t>(i)].host) == serverName)
            {
                prelistenRow = i;
                break;
            }
        }
        volumeSlider.setVisible(true);
        volumeLabel.setVisible(true);
    }
    resized();
    listBox.repaint();
}
```

- [ ] **Step 7: Clear prelisten state on dismiss**

In `juce/ui/ServerBrowserOverlay.cpp`, in the `dismiss()` method, add cleanup:

```cpp
void ServerBrowserOverlay::dismiss()
{
    // Stop any active prelisten when closing browser
    if (prelistenRow >= 0)
    {
        prelistenRow = -1;
        volumeSlider.setVisible(false);
        volumeLabel.setVisible(false);
        if (onStopListenClicked) onStopListenClicked();
    }

    // ... existing dismiss logic (setVisible(false), onDismissed callback) ...
```

- [ ] **Step 8: Verify compilation**

Run: `cmake --build build --target JamWideJuce_VST3 -j$(sysctl -n hw.ncpu) 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 9: Commit**

```bash
git add juce/ui/ServerBrowserOverlay.h juce/ui/ServerBrowserOverlay.cpp
git commit -m "feat(prelisten): add listen button, volume slider, and active row state to server browser"
```

---

### Task 5: Wire Prelisten Commands and Events in Editor

**Files:**
- Modify: `juce/JamWideJuceEditor.cpp:79-87` (server browser wiring)
- Modify: `juce/JamWideJuceEditor.cpp:348-419` (drainEvents)

- [ ] **Step 1: Wire onListenClicked and onStopListenClicked callbacks**

In `juce/JamWideJuceEditor.cpp`, in the constructor, after the existing server browser callback wiring (around line 87), add:

```cpp
    serverBrowser.onListenClicked = [this](const std::string& host, int port) {
        jamwide::PrelistenCommand cmd;
        cmd.host = host;
        cmd.port = port;
        processorRef.cmd_queue.try_push(std::move(cmd));
    };

    serverBrowser.onStopListenClicked = [this]() {
        processorRef.cmd_queue.try_push(jamwide::StopPrelistenCommand{});
    };
```

- [ ] **Step 2: Handle PrelistenStateEvent in drainEvents()**

In `juce/JamWideJuceEditor.cpp`, in `drainEvents()`, add a new case to the `std::visit` block (after the `SyncStateChangedEvent` handler):

```cpp
            else if constexpr (std::is_same_v<T, jamwide::PrelistenStateEvent>)
            {
                serverBrowser.setPrelistenState(e.connected, juce::String(e.server_name));
                // Sync volume from slider to processor
                processorRef.prelisten_volume.store(
                    serverBrowser.prelistenVolume, std::memory_order_relaxed);
            }
```

- [ ] **Step 3: Sync prelisten volume continuously in timerCallback()**

In `juce/JamWideJuceEditor.cpp`, in `timerCallback()` (called at 20Hz), add after `drainEvents()`:

```cpp
    // Sync prelisten volume slider → processor (20Hz is sufficient for a volume knob)
    if (processorRef.prelisten_mode.load(std::memory_order_relaxed))
        processorRef.prelisten_volume.store(
            serverBrowser.prelistenVolume, std::memory_order_relaxed);
```

- [ ] **Step 4: Disable listen buttons when already in a session**

In `juce/JamWideJuceEditor.cpp`, in the `showServerBrowser()` method, after `serverBrowser.show()`, add a check:

```cpp
    // Disable prelisten if already in a non-prelisten session
    bool inSession = (processorRef.client->GetStatus() >= 0)
                  && !processorRef.prelisten_mode.load(std::memory_order_relaxed);
    serverBrowser.setListenEnabled(!inSession);
```

Then in `juce/ui/ServerBrowserOverlay.h`, add:

```cpp
    bool listenEnabled = true;
    void setListenEnabled(bool enabled);
```

And in `juce/ui/ServerBrowserOverlay.cpp`:

```cpp
void ServerBrowserOverlay::setListenEnabled(bool enabled)
{
    listenEnabled = enabled;
    listBox.repaint();
}
```

Update the listen button rendering in `paintListBoxItem()` to check `listenEnabled` — skip drawing the button if `!listenEnabled && row != prelistenRow`.

Update the click handler in `listBoxItemClicked()` to early-return if `!listenEnabled && row != prelistenRow`.

- [ ] **Step 5: Verify compilation**

Run: `cmake --build build --target JamWideJuce_VST3 -j$(sysctl -n hw.ncpu) 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 6: Commit**

```bash
git add juce/JamWideJuceEditor.cpp juce/ui/ServerBrowserOverlay.h juce/ui/ServerBrowserOverlay.cpp
git commit -m "feat(prelisten): wire prelisten commands/events in editor, sync volume, session guard"
```

---

### Task 6: Manual Integration Test

**Files:** None (testing only)

- [ ] **Step 1: Build the VST3**

Run: `cmake --build build --target JamWideJuce_VST3 -j$(sysctl -n hw.ncpu) 2>&1 | tail -5`
Expected: `[100%] Built target JamWideJuce_VST3`

- [ ] **Step 2: Copy VST3 to plugin directory**

Run: `cp -r build/JamWideJuce_artefacts/Release/VST3/JamWide.vst3 ~/Library/Audio/Plug-Ins/VST3/`

(Note: the CMake "Copy After Build" step may do this automatically.)

- [ ] **Step 3: Manual test checklist**

Open JamWide in a DAW or standalone and verify:

1. Open server browser (Browse Servers button)
2. Populated rooms show a "Listen" button (bottom-right of row)
3. Empty rooms (0 users) have NO listen button
4. Click "Listen" on a populated room:
   - Row highlights with blue tint
   - "LISTENING" badge appears
   - Left-edge blue bar visible
   - Button changes to "Stop"
   - Volume slider appears in title bar
   - Audio from the room plays through output (may take 1-3 seconds for first interval)
5. Adjust volume slider — audio level changes
6. Click "Listen" on a different room — switches to that room
7. Click "Stop" — audio stops, row returns to normal
8. Close server browser — audio stops
9. Single-click row (not on button) — still fills address bar as before
10. Double-click row — connects normally (not in prelisten mode)

- [ ] **Step 4: Commit any fixes discovered during testing**

```bash
git add -A
git commit -m "fix(prelisten): integration test fixes"
```

(Only if fixes were needed.)
