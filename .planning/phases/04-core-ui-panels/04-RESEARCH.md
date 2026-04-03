# Phase 4: Core UI Panels - Research

**Researched:** 2026-04-04
**Domain:** JUCE 8 custom Component UI, pro-audio mixer-style layout, thread-safe event consumption
**Confidence:** HIGH

## Summary

Phase 4 replaces the minimal Phase 3 editor with a full JUCE-native UI inspired by VB-Audio Voicemeeter Banana. The codebase already has all necessary threading infrastructure: SPSC command queue (UI->Run), event types (Run->UI), NJClient APIs for reading session state, and a working reference implementation in Dear ImGui. The primary work is building JUCE Components that replicate ImGui panel behavior, adding an event queue on the Processor for Run thread->UI communication, implementing a custom LookAndFeel for the dark pro-audio theme, and wiring the license callback to a non-blocking JUCE dialog.

The existing CLAP/ImGui plugin (`src/ui/ui_main.cpp`) provides a complete behavioral reference -- every event type, command dispatch, chat parsing, license dialog flow, and server browser interaction is already implemented. The JUCE editor must replicate this logic using JUCE Components, Timer-based polling, and SPSC event queue draining.

**Primary recommendation:** Build panel Components bottom-up (LookAndFeel first, then individual panels, then compose into the editor), reusing the existing `SpscRing<UiEvent, 256>` pattern for event delivery. Use Timer polling at 10-30Hz for both status reads and event queue draining. Do NOT use modal dialogs for the license -- use an async overlay Component.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Mixer-first layout inspired by VB-Audio Voicemeeter Banana -- everything visible on one screen, no tabs or navigation
- **D-02:** Remote users displayed as vertical channel strips with VU meters (faders + knobs added in Phase 5)
- **D-03:** Chat panel docked on the right side, always visible
- **D-04:** Connection bar across the top (server address, username, connect/disconnect button, status info, codec selector)
- **D-05:** Server browser opens as a modal overlay, not a separate panel
- **D-06:** Local channel strip displayed alongside remote user strips
- **D-07:** Full custom LookAndFeel -- dark theme, custom-drawn components, pro-audio aesthetic
- **D-08:** SVG assets designed via Sketch MCP server for all UI elements
- **D-09:** VB-Audio Voicemeeter Banana is the primary design reference (dark blue/gray tones, dense layout, big faders, illuminated VU meters)
- **D-10:** Custom-draw VU meters, channel strips, buttons, and all components -- no stock JUCE appearance
- **D-11:** Single click on a server fills the server address into the connection bar
- **D-12:** Double-click fills AND auto-connects
- **D-13:** Each server entry shows: server name + address, user count, BPM/BPI, topic/description
- **D-14:** Server browser overlay closes on connect
- **D-15:** Display all message types: regular chat (MSG), join/part notifications, topic/server messages, and private messages (PRIVMSG)
- **D-16:** Auto-scroll to newest messages by default
- **D-17:** Scrolling up pauses auto-scroll; a "jump to bottom" indicator appears
- **D-18:** Different visual styling per message type (color-coded or formatted, matching dark theme)
- **D-19:** Modal popup dialog appears when server sends license text after connection
- **D-20:** Dark-themed to match the rest of the UI, with Accept/Decline buttons
- **D-21:** Session blocked until user responds (consistent with NINJAM client behavior)
- **D-22:** Fixed window size (no free resizing) -- pixel-perfect layout like Voicemeeter Banana
- **D-23:** Scale options: 1x / 1.5x / 2x (accessible via menu or settings) for HiDPI support
- **D-24:** Password field hidden by default; a lock icon or toggle reveals it when needed
- **D-25:** Server address and username fields always visible in the top bar
- **D-26:** Codec selector (FLAC/Vorbis dropdown) in the connection/status bar area
- **D-27:** Same layout structure as connected state but channel strip area shows empty/dimmed placeholders
- **D-28:** Welcome message/prompt in the main area: "Connect to a server to start jamming"
- **D-29:** Prominent [Browse Servers] button in the empty state
- **D-30:** Chat area shows "Connect to a server to start chatting"
- **D-31:** Remove Timing Guide code (ui_latency_guide.cpp/h) -- confirmed not useful

### Claude's Discretion
- Exact fixed window dimensions (800x600 or larger to fit all panels)
- SVG asset design specifics and exact color values
- Chat message formatting details (timestamps, name colors)
- VU meter update rate and visual style (peak hold, ballistics)
- Scale option UI placement (menu bar, settings gear, etc.)
- Event queue consumption strategy (timer polling vs AsyncUpdater vs hybrid)
- Connection bar field widths and spacing

### Deferred Ideas (OUT OF SCOPE)
- DAW sync from JamTaba -- Phase 7 scope
- VDO.ninja video integration -- future milestone
- Mixer controls (volume, pan, mute, solo per channel) -- Phase 5 scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| JUCE-05 | All UI rebuilt as JUCE Components (no Dear ImGui) | Full editor replacement; custom LookAndFeel; all panels as JUCE Components. ImGui code remains for CLAP target but JUCE target uses zero ImGui. |
| UI-01 | Connection panel (server, username, password, connect/disconnect) | ConnectionBar component at top with TextEditor fields and TextButton. Uses existing ConnectCommand/DisconnectCommand via cmd_queue. Password field hidden by default (D-24). |
| UI-02 | Chat panel with message history and input | ChatPanel component with custom ListBox for message display, TextEditor for input. Drains ChatMessageEvent from event queue. Color-coded message types per existing ImGui chat logic. |
| UI-03 | Status display (connection state, BPM/BPI, user count) | StatusDisplay embedded in ConnectionBar. Polls cached_status atomic + reads BPM/BPI from NJClient under Timer. User count from GetNumUsers(). |
| UI-07 | Server browser with public server list | ServerBrowserOverlay component shown as modal overlay. Receives ServerListEvent via event queue. Single-click fills address (D-11), double-click auto-connects (D-12). |
| UI-09 | Codec selector (FLAC/Vorbis toggle per local channel) | ComboBox in ConnectionBar. Pushes SetEncoderFormatCommand on change. Shows current codec as FLAC or Vorbis. |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.0.12 | UI framework, AudioProcessor | Already pinned as submodule; provides Component, LookAndFeel, Graphics, Timer, Drawable |
| juce_gui_basics | 8.0.12 | Component, LookAndFeel, TextEditor, ListBox, ComboBox, Viewport | Core JUCE UI module, already linked |
| juce_gui_extra | 8.0.12 | Potentially needed for WebBrowser (not used) -- already linked | Already in CMakeLists |
| juce_opengl | 8.0.12 | OpenGL rendering context (linked but NOT attached per Phase 2 decision) | Available for future GPU acceleration |

### Supporting (Existing)
| Library | Purpose | When to Use |
|---------|---------|-------------|
| SpscRing<T,N> | Lock-free SPSC ring buffer | cmd_queue (UI->Run) already on Processor; add evt_queue (Run->UI) on Processor |
| UiEvent variant | Run->UI event types | ChatMessageEvent, StatusChangedEvent, ServerListEvent, TopicChangedEvent, UserInfoChangedEvent |
| UiCommand variant | UI->Run command types | ConnectCommand, DisconnectCommand, SendChatCommand, SetEncoderFormatCommand, RequestServerListCommand |
| ServerListFetcher | HTTP server list fetcher | Already implemented in src/net/server_list.cpp |

### No New Dependencies
This phase requires NO new external libraries. Everything is built with JUCE 8.0.12 modules already linked. SVG assets are compiled into BinaryData via JUCE's resource system.

## Architecture Patterns

### Recommended Project Structure
```
juce/
  JamWideJuceEditor.h/.cpp        # Main editor (replaced entirely)
  JamWideJuceProcessor.h/.cpp     # Add evt_queue, chat_queue, license sync primitives
  NinjamRunThread.h/.cpp           # Add event pushing, chat callback, license callback
  ui/
    JamWideLookAndFeel.h/.cpp      # Custom LookAndFeel_V4 subclass
    ConnectionBar.h/.cpp           # Top bar: server, username, password, connect, codec, status
    ChatPanel.h/.cpp               # Right panel: message list + input
    ChannelStripArea.h/.cpp        # Central area: local + remote channel strips (placeholder in Phase 4)
    ChannelStrip.h/.cpp            # Single channel strip with label + VU meter (faders Phase 5)
    VuMeter.h/.cpp                 # Custom-painted VU meter component
    ServerBrowserOverlay.h/.cpp    # Modal overlay with server list table
    LicenseDialog.h/.cpp           # Non-blocking license accept/reject overlay
    ChatMessageModel.h             # Data model for chat history (replaces UiState circular buffer)
```

### Pattern 1: Timer-Polled Event Drain (Recommended)
**What:** The editor uses a juce::Timer at 15-30Hz to drain events from SPSC queues and poll NJClient atomics. This avoids any real-time thread allocations or cross-thread message posting.
**When to use:** All Run->UI state updates.
**Why:** The CLAP ImGui plugin already uses this pattern (drain in render frame). For JUCE, Timer replaces the ImGui render loop. AsyncUpdater requires posting a message from the Run thread, which can allocate -- Timer polling avoids this entirely.

```cpp
// In JamWideJuceEditor::timerCallback() at ~20Hz
void JamWideJuceEditor::timerCallback()
{
    // 1. Drain event queue
    processorRef.evt_queue.drain([this](jamwide::UiEvent&& event) {
        std::visit([this](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, jamwide::StatusChangedEvent>) {
                connectionBar.setStatus(e.status, e.error_msg);
            }
            else if constexpr (std::is_same_v<T, jamwide::ChatMessageEvent>) {
                chatPanel.addMessage(e.type, e.user, e.text);
            }
            else if constexpr (std::is_same_v<T, jamwide::ServerListEvent>) {
                serverBrowser.updateList(std::move(e.servers), e.error);
            }
            else if constexpr (std::is_same_v<T, jamwide::TopicChangedEvent>) {
                chatPanel.setTopic(e.topic);
            }
            else if constexpr (std::is_same_v<T, jamwide::UserInfoChangedEvent>) {
                refreshChannelStrips();
            }
        }, std::move(event));
    });

    // 2. Drain chat queue
    processorRef.chat_queue.drain([this](ChatMessage&& msg) {
        chatPanel.addChatMessage(std::move(msg));
    });

    // 3. Poll NJClient atomics for high-frequency data
    if (auto* client = processorRef.getClient()) {
        int status = client->cached_status.load(std::memory_order_acquire);
        connectionBar.updateFromStatus(status);
        // BPM/BPI etc read from snapshot atomics
    }

    // 4. Check license pending
    if (processorRef.license_pending.load(std::memory_order_acquire)) {
        showLicenseDialog();
    }
}
```

### Pattern 2: Processor-Owned Event Queue
**What:** Add `SpscRing<UiEvent, 256> evt_queue` and `SpscRing<ChatMessage, 128> chat_queue` to JamWideJuceProcessor. NinjamRunThread pushes events; editor drains in timerCallback.
**Why:** Mirrors the CLAP plugin's `ui_queue` / `chat_queue` on JamWidePlugin. Processor is the natural owner because it outlives the editor.

```cpp
// In JamWideJuceProcessor.h:
jamwide::SpscRing<jamwide::UiEvent, 256> evt_queue;
jamwide::SpscRing<ChatMessage, 128> chat_queue;

// License sync (mirrors CLAP plugin pattern):
std::mutex license_mutex;
std::condition_variable license_cv;
std::atomic<bool> license_pending{false};
std::atomic<int> license_response{0};
std::string license_text;
```

### Pattern 3: Custom LookAndFeel for Pro-Audio Dark Theme
**What:** Subclass `juce::LookAndFeel_V4` with custom colours and overridden draw methods for buttons, sliders, combo boxes, text editors, and scrollbars.
**Why:** D-07 through D-10 mandate full custom appearance. LookAndFeel_V4 provides the most modern base to override.

```cpp
class JamWideLookAndFeel : public juce::LookAndFeel_V4
{
public:
    JamWideLookAndFeel()
    {
        // Voicemeeter Banana-inspired dark palette
        setColour(juce::ResizableWindow::backgroundColourId,
                  juce::Colour(0xff1a1d2e));    // Deep dark blue-gray
        setColour(juce::TextEditor::backgroundColourId,
                  juce::Colour(0xff252840));
        setColour(juce::TextEditor::textColourId,
                  juce::Colour(0xffe0e0e0));
        setColour(juce::TextButton::buttonColourId,
                  juce::Colour(0xff2a2d45));
        setColour(juce::TextButton::textColourOnId,
                  juce::Colour(0xff40e070));     // Green active state
        setColour(juce::ComboBox::backgroundColourId,
                  juce::Colour(0xff252840));
        // ... etc
    }

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        // Custom rounded-rect with gradient for pro-audio look
    }
};
```

### Pattern 4: AffineTransform-Based Scaling
**What:** For D-22/D-23 (fixed size + scale options), design at a base resolution (e.g. 1000x700) and use `setTransform(juce::AffineTransform::scale(scaleFactor))` on the editor's content component.
**Why:** This is the standard JUCE approach for fixed-layout plugins with DPI scaling. The editor reports the scaled size to the host via `setSize(baseW * scale, baseH * scale)`.

```cpp
void JamWideJuceEditor::setScaleFactor(float factor)
{
    scaleFactor = factor;
    setSize(static_cast<int>(baseWidth * factor),
            static_cast<int>(baseHeight * factor));
    setTransform(juce::AffineTransform::scale(factor));
}
```

### Pattern 5: Non-Blocking License Dialog
**What:** Show a Component overlay (not a modal AlertWindow) when `license_pending` is true. The overlay covers the editor, shows license text with Accept/Reject buttons. On click, it stores the response in the atomic and notifies the condition variable.
**Why:** Modal loops are problematic in plugins (some hosts don't support them). The Run thread already blocks with a condition_variable wait (see `run_thread.cpp:131-148`), so the UI just needs to set the atomic and notify.

### Pattern 6: Chat Panel with Custom List Rendering
**What:** Use a `juce::Viewport` containing a custom Component that renders chat messages with word-wrapping. Each message is a struct with type, sender, content, timestamp. The component calculates height dynamically based on text wrapping.
**Why:** JUCE's `ListBox` works for fixed-height rows but chat messages vary in length. A custom-painted component in a Viewport gives full control over message formatting, color coding, and scroll behavior.

**Alternative considered:** Using `juce::TextEditor` in read-only multiline mode. This is simpler but provides less control over per-message coloring and formatting. A Viewport approach better matches D-15/D-18 (different styling per message type).

### Anti-Patterns to Avoid
- **Modal loops in plugins:** Never use `AlertWindow::showMessageBoxSync()` or `runModalLoop()`. Use async callbacks or overlay components.
- **Locking from the message thread:** Never acquire `clientLock` from the JUCE message thread. Use SPSC queues and atomics exclusively.
- **AudioProcessor accessing UI Components:** The Processor must never reference the editor directly. Use queues and atomics for communication.
- **OpenGL context attachment:** Per Phase 2 decision, do NOT call `openGLContext.attachTo(editor)` -- it causes issues with some hosts. Use software rendering.
- **Polling NJClient member fields from UI thread without lock:** Fields like `m_active_bpm`, `m_active_bpi` are NOT atomic. Either read them under `clientLock` (in Run thread, then push to atomics/events) or use the already-provided atomic snapshot pattern.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Ring buffer | Custom lock-free queue | `jamwide::SpscRing<T,N>` (already exists) | Proven, cache-line-padded, power-of-2 optimized |
| Server list HTTP fetch | Custom HTTP/jnetlib integration | `ServerListFetcher` (already exists) | Dual-format parser (plain text + JSON) already battle-tested |
| Chat message parsing (/me, /msg, /topic) | Custom parser | Port `parse_chat_input()` from `ui_chat.cpp` | Handles all NINJAM chat commands, edge cases already solved |
| Chat callback mapping | Custom callback parser | Port `chat_callback()` from `run_thread.cpp` | Maps NINJAM callback parms to ChatMessage types correctly |
| Color theme system | Custom CSS-like system | JUCE `LookAndFeel::setColour()` + ColourId system | JUCE's colour system is designed exactly for this purpose |
| SVG rendering | Custom vector renderer | `juce::Drawable::createFromSVG()` + BinaryData | JUCE's SVG parser handles the subset needed for UI icons/assets |
| Thread-safe state updates | Custom locking scheme | SPSC queues + atomic reads (pattern from CLAP plugin) | Proven lock-free pattern already working in the codebase |

**Key insight:** Nearly all the "hard" logic already exists in the ImGui codebase. The JUCE port is primarily a Component/rendering translation, not a logic rewrite. Behavioral reference code is in `src/ui/ui_*.cpp` and `src/threading/run_thread.cpp`.

## Common Pitfalls

### Pitfall 1: Accessing NJClient from the Message Thread
**What goes wrong:** Reading `GetNumUsers()`, `GetUserState()`, `GetBPI()` etc. from the JUCE message thread without holding `clientLock` causes data races.
**Why it happens:** These NJClient methods read internal arrays that the Run thread modifies.
**How to avoid:** The Run thread must push state to event queues or atomic snapshots. The editor only reads atomics and drains queues. For user info, the Run thread should call `GetRemoteUsersSnapshot()` (exists on NJClient, returns a copy) and push it via event queue.
**Warning signs:** Intermittent crashes, corrupted user names, assertion failures.

### Pitfall 2: Editor Lifetime vs Processor Lifetime
**What goes wrong:** The JUCE editor can be created and destroyed multiple times while the Processor persists. If state (chat history, server list) is stored on the editor, it's lost when the editor is destroyed.
**Why it happens:** Hosts create/destroy plugin editor windows freely (DAW opens/closes plugin UI).
**How to avoid:** Store persistent UI state (chat history, connection settings, server list cache) on the Processor. The editor reads from and writes to Processor-owned state.
**Warning signs:** Chat history disappearing when reopening plugin UI.

### Pitfall 3: License Dialog Blocking Pitfall
**What goes wrong:** Using a modal JUCE dialog from a background thread deadlocks the Run thread.
**Why it happens:** The Run thread's `license_callback()` is called synchronously by NJClient. It needs to block until the user responds, but can't show UI from a non-message thread.
**How to avoid:** The existing CLAP pattern works: Run thread sets `license_pending` atomic + waits on condition_variable. UI thread (Timer) detects pending, shows overlay. User clicks Accept/Reject -> stores atomic + notifies CV. Run thread wakes up.
**Warning signs:** UI freeze on connect, unresponsive plugin.

### Pitfall 4: ScrollToEnd Race in Chat
**What goes wrong:** Calling `scrollToEnd()` on the chat viewport immediately after adding a message may not work because layout hasn't been recalculated yet.
**Why it happens:** JUCE defers layout calculations. The viewport's content size hasn't been updated when you call scroll.
**How to avoid:** Use `juce::MessageManager::callAsync()` to defer the scroll, or set a flag and perform the scroll in the next timer callback after the content has been resized.
**Warning signs:** Chat not scrolling to bottom after new messages, erratic scroll behavior.

### Pitfall 5: SVG Parsing Failures
**What goes wrong:** Complex SVGs with gradients, filters, or CSS styling fail to parse in JUCE's SVG parser.
**Why it happens:** JUCE's `Drawable::createFromSVG()` supports a subset of SVG -- no CSS styling, limited gradient support, no filters.
**How to avoid:** Design SVGs with simple fills, strokes, and basic shapes. Avoid CSS `<style>` blocks, embedded fonts, filters, and clip-paths. Test each SVG immediately after creation.
**Warning signs:** `createFromSVG()` returns nullptr, garbled rendering, missing elements.

### Pitfall 6: Plugin Window Size and Host Interactions
**What goes wrong:** Some DAW hosts override plugin window size or don't respect fixed-size constraints.
**Why it happens:** Different hosts handle `setResizable(false, false)` differently. Logic Pro, for instance, may scale AU plugins.
**How to avoid:** Call `setResizable(false, false)` in the editor constructor. For scaling support, use `setSize(w*scale, h*scale)` and `setTransform(AffineTransform::scale(s))`. The editor reports its size; the host should respect it.
**Warning signs:** Stretched UI, cut-off panels, blank areas around the plugin window.

### Pitfall 7: RequestServerListCommand Not Handled in NinjamRunThread
**What goes wrong:** The JUCE NinjamRunThread currently does NOT handle `RequestServerListCommand`. It's listed in a comment as "will be handled when Phase 4/5 UI needs them."
**Why it happens:** Phase 3 was minimal; server list wasn't needed.
**How to avoid:** Add `ServerListFetcher` to NinjamRunThread and handle RequestServerListCommand dispatch + polling. Reference: `run_thread.cpp:167-186` and `run_thread.cpp:296-305`.
**Warning signs:** Server browser shows "Loading..." forever, no server list populates.

## Code Examples

### Event Queue Addition to Processor
```cpp
// JamWideJuceProcessor.h additions:
#include "threading/ui_event.h"
#include "ui/ui_state.h"  // For ChatMessage

// Add to public section:
jamwide::SpscRing<jamwide::UiEvent, 256> evt_queue;
jamwide::SpscRing<ChatMessage, 128> chat_queue;

// License synchronization:
std::mutex license_mutex;
std::condition_variable license_cv;
std::atomic<bool> license_pending{false};
std::atomic<int> license_response{0};
juce::String license_text;
```

### NinjamRunThread Chat Callback (Port from CLAP)
```cpp
// In NinjamRunThread.cpp, replace no-op chat_callback:
void chat_callback(void* user_data, NJClient* /*client*/,
                   const char** parms, int nparms)
{
    auto& proc = *static_cast<JamWideJuceProcessor*>(user_data);
    if (nparms < 1 || !parms[0]) return;

    const std::string type = parms[0];
    const std::string user = (nparms > 1 && parms[1]) ? parms[1] : "";
    const std::string text = (nparms > 2 && parms[2]) ? parms[2] : "";

    ChatMessage msg;

    if (type == "TOPIC") {
        // Push topic changed event + chat message
        jamwide::TopicChangedEvent te;
        te.topic = text;
        proc.evt_queue.try_push(std::move(te));

        if (!user.empty()) {
            msg.type = ChatMessageType::Topic;
            msg.sender = user;
            msg.content = text.empty()
                ? (user + " removes topic.")
                : (user + " sets topic to: " + text);
            proc.chat_queue.try_push(std::move(msg));
        }
        return;
    }

    if (type == "MSG") {
        if (user.empty() || text.empty()) return;
        if (text.rfind("/me ", 0) == 0) {
            msg.type = ChatMessageType::Action;
            msg.sender = user;
            msg.content = text.substr(4);
        } else {
            msg.type = ChatMessageType::Message;
            msg.sender = user;
            msg.content = text;
        }
        proc.chat_queue.try_push(std::move(msg));
        return;
    }

    if (type == "PRIVMSG") {
        msg.type = ChatMessageType::PrivateMessage;
        msg.sender = user;
        msg.content = text;
        proc.chat_queue.try_push(std::move(msg));
        return;
    }

    if (type == "JOIN" || type == "PART") {
        msg.type = (type == "JOIN") ? ChatMessageType::Join : ChatMessageType::Part;
        msg.sender = user;
        msg.content = user + (type == "JOIN" ? " has joined" : " has left");
        proc.chat_queue.try_push(std::move(msg));
        return;
    }
}
```

### Custom VU Meter Component
```cpp
class VuMeter : public juce::Component, private juce::Timer
{
public:
    VuMeter() { startTimerHz(30); }

    void setLevels(float left, float right)
    {
        targetLeft = left;
        targetRight = right;
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float halfW = bounds.getWidth() / 2.0f - 1.0f;

        // Left channel
        auto leftBar = bounds.removeFromLeft(halfW);
        paintBar(g, leftBar, displayLeft);

        bounds.removeFromLeft(2.0f); // gap

        // Right channel
        paintBar(g, bounds, displayRight);
    }

private:
    void paintBar(juce::Graphics& g, juce::Rectangle<float> r, float level)
    {
        g.setColour(juce::Colour(0xff0a0d18));
        g.fillRoundedRectangle(r, 2.0f);

        float h = r.getHeight() * juce::jlimit(0.0f, 1.0f, level);
        auto lit = r.removeFromBottom(h);

        // Green -> Yellow -> Red gradient
        if (level < 0.7f)
            g.setColour(juce::Colour(0xff20c050));
        else if (level < 0.9f)
            g.setColour(juce::Colour(0xffc0c020));
        else
            g.setColour(juce::Colour(0xffc02020));

        g.fillRoundedRectangle(lit, 2.0f);
    }

    void timerCallback() override
    {
        // Ballistic smoothing (fast attack, slow release)
        const float attack = 0.8f;
        const float release = 0.92f;

        displayLeft = (targetLeft > displayLeft)
            ? targetLeft * attack + displayLeft * (1.0f - attack)
            : displayLeft * release;
        displayRight = (targetRight > displayRight)
            ? targetRight * attack + displayRight * (1.0f - attack)
            : displayRight * release;

        repaint();
    }

    float targetLeft = 0.0f, targetRight = 0.0f;
    float displayLeft = 0.0f, displayRight = 0.0f;
};
```

### Server Browser Overlay
```cpp
class ServerBrowserOverlay : public juce::Component,
                              public juce::ListBoxModel
{
public:
    std::function<void(const juce::String&)> onServerSelected;
    std::function<void(const juce::String&)> onServerDoubleClicked;

    int getNumRows() override { return static_cast<int>(servers.size()); }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < (int)servers.size())
        {
            auto addr = formatAddress(servers[row]);
            if (onServerSelected) onServerSelected(addr);
        }
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < (int)servers.size())
        {
            auto addr = formatAddress(servers[row]);
            if (onServerDoubleClicked) onServerDoubleClicked(addr);
        }
    }

    void updateList(std::vector<ServerListEntry>&& newServers, const std::string& error)
    {
        servers = std::move(newServers);
        listBox.updateContent();
        listBox.repaint();
    }

private:
    std::vector<ServerListEntry> servers;
    juce::ListBox listBox;
};
```

### Window Dimensions Recommendation
Based on Voicemeeter Banana's layout density and the need to fit: connection bar (top, ~50px), channel strip area (center, ~400px), chat panel (right, ~280px wide):

**Recommended base dimensions: 1000 x 700 pixels**
- Connection bar: full width x 50px
- Channel strip area: 720px wide x 600px tall (left side)
- Chat panel: 280px wide x 650px tall (right side, below connection bar)
- This fits comfortably on a 1080p display at 1x scale
- At 1.5x scale: 1500 x 1050 (fits 1080p with some scrolling)
- At 2x scale: 2000 x 1400 (optimized for 4K displays)

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| LookAndFeel_V2/V3 | LookAndFeel_V4 | JUCE 5+ | V4 has ColourScheme, DarkColourScheme, more modern defaults |
| Modal loops for dialogs | Async callbacks / overlay components | JUCE 6+ | Modal loops can deadlock plugins; async is required for reliability |
| Direct component scaling | AffineTransform::scale() on content | JUCE 6+ | Cleaner scaling that works with all component types |
| OpenGL for all rendering | Software rendering by default | Best practice | OpenGL causes issues with many DAW hosts; use only when needed |

**Deprecated/outdated:**
- `LookAndFeel_V1`, `_V2`, `_V3`: Still functional but V4 is the standard base class
- `Component::setBufferedToImage(true)`: Was needed for performance; modern JUCE handles this better
- `AlertWindow::showMessageBox()` (sync): Replaced by async variants

## Open Questions

1. **Exact base window dimensions**
   - What we know: Must fit connection bar + channel strips + chat. Voicemeeter Banana is roughly 1050x600.
   - What's unclear: Exact pixel count depends on final SVG asset sizes and font choices.
   - Recommendation: Start with 1000x700, adjust during implementation based on layout fit. This is Claude's discretion.

2. **NJClient thread safety for GetRemoteUsersSnapshot**
   - What we know: `GetRemoteUsersSnapshot()` exists on NJClient and takes a lock internally. It was designed for this purpose.
   - What's unclear: Whether the JUCE NinjamRunThread should call it and push the result, or if the editor can call it directly.
   - Recommendation: Call from NinjamRunThread when `HasUserInfoChanged()` returns true, push snapshot via event queue. Safer and consistent with the lock-free UI pattern.

3. **SVG asset creation workflow**
   - What we know: D-08 says "SVG assets designed via Sketch MCP server." Assets must be simple (no CSS, no filters).
   - What's unclear: Exact assets needed for Phase 4 (buttons, icons, VU meter frames, etc.).
   - Recommendation: Start with custom painting in `paint()` methods. Add SVG assets incrementally. Custom-drawn VU meters are more flexible than SVG anyway.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| cmake | Build system | Yes | 4.3.1 | -- |
| ninja | Fast build | Yes | 1.13.2 | make (slower) |
| JUCE 8.0.12 | UI framework | Yes (submodule) | 8.0.12 | -- |
| Xcode / Apple toolchain | macOS build | Yes | -- | -- |
| node | GSD tooling | Yes | v22.22.1 | -- |

**Missing dependencies with no fallback:** None.
**Missing dependencies with fallback:** None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | CMake CTest + custom test executables |
| Config file | CMakeLists.txt (JAMWIDE_BUILD_TESTS option) |
| Quick run command | `cmake --build build --target JamWideJuce_Standalone && open build/JamWideJuce_artefacts/Standalone/JamWide\ JUCE.app` |
| Full suite command | `cmake --build build -DJAMWIDE_BUILD_TESTS=ON && ctest --test-dir build` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| JUCE-05 | All UI as JUCE Components, no ImGui | manual | Build JUCE target, verify no ImGui includes | N/A (build verification) |
| UI-01 | Connect/disconnect via connection panel | manual | Launch standalone, enter server, click connect | N/A (manual) |
| UI-02 | Chat send/receive with history | manual | Connect to server, send message, verify display | N/A (manual) |
| UI-03 | Status shows BPM/BPI/user count | manual | Connect to server, verify status bar updates | N/A (manual) |
| UI-07 | Server browser with public list | manual | Click Browse, verify list populates | N/A (manual) |
| UI-09 | Codec selector FLAC/Vorbis | manual | Change codec dropdown, verify command sent | N/A (manual) |

### Sampling Rate
- **Per task commit:** `cmake --build build --target JamWideJuce_Standalone` (build succeeds)
- **Per wave merge:** Build + launch standalone + manual smoke test
- **Phase gate:** All 6 requirements manually verified via standalone app

### Wave 0 Gaps
- None for automated testing -- UI phases are primarily manual verification
- Pluginval validation: `cmake --build build --target validate` (existing target, verifies VST3 loads)

## Sources

### Primary (HIGH confidence)
- Codebase analysis: `juce/JamWideJuceEditor.h/.cpp`, `juce/JamWideJuceProcessor.h/.cpp`, `juce/NinjamRunThread.h/.cpp` -- current Phase 3 implementation
- Codebase analysis: `src/ui/ui_main.cpp` -- CLAP ImGui event drain pattern (behavioral reference)
- Codebase analysis: `src/threading/run_thread.cpp` -- CLAP Run thread with chat_callback, license_callback, event pushing
- Codebase analysis: `src/threading/ui_event.h`, `src/threading/ui_command.h` -- existing event/command types
- Codebase analysis: `src/ui/ui_state.h` -- UiState with 90+ fields (data model reference)
- Codebase analysis: `src/ui/ui_chat.cpp` -- chat parsing, message formatting, scroll behavior
- Codebase analysis: `src/ui/ui_server_browser.cpp` -- server browser table, address formatting
- JUCE 8.0.12 submodule (verified pinned version)

### Secondary (MEDIUM confidence)
- [JUCE LookAndFeel tutorial](https://juce.com/tutorials/tutorial_look_and_feel_customisation/) -- LookAndFeel_V4 customization patterns
- [JUCE Colours tutorial](https://juce.com/tutorials/tutorial_colours/) -- ColourId system
- [JUCE AudioProcessorEditor docs](https://docs.juce.com/master/classAudioProcessorEditor.html) -- setResizable, scaling
- [JUCE AsyncUpdater docs](https://docs.juce.com/master/classAsyncUpdater.html) -- why Timer is preferred over AsyncUpdater for RT safety
- [JUCE ListBox docs](https://docs.juce.com/master/classListBox.html) -- ListBoxModel for server browser
- [JUCE Drawable docs](https://docs.juce.com/master/classDrawable.html) -- SVG loading via createFromSVG
- [JUCE forum: plugin editor scaling](https://forum.juce.com/t/current-advised-way-to-do-plugin-editor-contents-scaling/48044) -- AffineTransform pattern

### Tertiary (LOW confidence)
- [VB-Audio Voicemeeter Banana page](https://vb-audio.com/Voicemeeter/banana.htm) -- visual reference (page provides limited UI spec detail; screenshots are the primary reference)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- JUCE 8.0.12 already pinned, all modules linked, no new dependencies
- Architecture: HIGH -- event queue pattern proven in CLAP plugin, direct port with JUCE idioms
- Pitfalls: HIGH -- identified from codebase analysis and established JUCE plugin development patterns
- Code examples: HIGH -- based on existing working code in the codebase and standard JUCE patterns

**Research date:** 2026-04-04
**Valid until:** 2026-05-04 (stable -- JUCE 8.0.12 pinned, no external dependency changes expected)
