# Phase 10: OSC Remote Users and Template - Research

**Researched:** 2026-04-06
**Domain:** OSC remote user control, roster broadcast, connect/disconnect triggers, TouchOSC template generation
**Confidence:** HIGH

## Summary

Phase 10 extends the Phase 9 OSC server with four capabilities: (1) remote user parameter control via index-based OSC addresses, (2) roster name broadcast on user join/leave, (3) connect/disconnect triggers via OSC, and (4) a shipped TouchOSC `.tosc` template file. All four capabilities build directly on established patterns from Phase 9 -- the dirty-flag sender, SPSC cmd_queue dispatch, VU meter always-dirty broadcast, and callAsync message thread marshalling.

The primary technical challenge is prefix-based dynamic address parsing for incoming remote user messages (Phase 9 used a static HashMap lookup), and string argument handling for both the roster name broadcast and the connect trigger. The existing `oscMessageReceived()` only handles float/int arguments and will need extension for string-typed OSC messages. The `.tosc` template is a zlib-compressed XML file that can be generated programmatically or hand-crafted in the TouchOSC editor.

**Primary recommendation:** Extend OscServer with three new send methods (`sendDirtyRemoteUsers`, `sendRemoteRoster`, `sendRemoteVuMeters`) following the existing pattern, add prefix-based dispatch in `handleOscOnMessageThread` before the static map lookup, extend `oscMessageReceived` to handle string arguments for `/session/connect`, and create the `.tosc` template using the TouchOSC editor application (not programmatic XML generation).

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Positional index model. Remote slot 1 maps to the first connected remote user, slot 2 to the second, etc. When users leave, subsequent indices shift down. The roster name broadcast tells the control surface who is in each slot.
- **D-02:** Address pattern: `/JamWide/remote/{idx}/volume`, `/pan`, `/mute`, `/solo` (idx 1-based, matching local channel convention from Phase 9). dB variant: `/JamWide/remote/{idx}/volume/db`.
- **D-03:** Per sub-channel control. Multi-channel remote users expose each sub-channel: `/JamWide/remote/{idx}/ch/{n}/volume`, `/pan`, `/mute`, `/solo`. The "group bus" level control at `/JamWide/remote/{idx}/volume` controls the user's combined output (SetUserState on channel 0 / group).
- **D-04:** VU meters per remote user: `/JamWide/remote/{idx}/vu/left`, `/vu/right`. Sent every 100ms tick (same always-dirty pattern as local VU from Phase 9). Per sub-channel VU: `/JamWide/remote/{idx}/ch/{n}/vu/left`, `/vu/right`.
- **D-05:** Max 16 remote user slots (matching the 16 routing bus limit in JamWideJuceProcessor). If more than 16 users connect, slots 17+ are not controllable via OSC (they still appear in-plugin).
- **D-06:** Per-slot name messages. When roster changes (UserInfoChangedEvent fires), send `/JamWide/remote/{idx}/name` as a string for each active slot. Empty string `""` for empty slots. This lets TouchOSC bind text labels directly to each slot.
- **D-07:** Broadcast triggered by `UserInfoChangedEvent` from NinjamRunThread. Read from `processor.cachedUsers` snapshot (already thread-safe). Also send `/JamWide/session/users` count (already implemented in Phase 9 telemetry).
- **D-08:** Sub-channel names: `/JamWide/remote/{idx}/ch/{n}/name` -- sends the channel name string. Broadcast alongside user names on roster change.
- **D-09:** Simple trigger model. `/JamWide/session/connect` with string argument `"host:port"` -- uses username/password from last connection config (stored on processor). `/JamWide/session/disconnect` with no args (or float 1.0 as trigger). Minimal for a single button on a control surface.
- **D-10:** Connect dispatches via cmd_queue using existing connection flow. Disconnect dispatches a disconnect command. Both follow the SPSC single-producer pattern from Phase 9 (callAsync to message thread, then cmd_queue push).
- **D-11:** Connection status feedback already exists: `/JamWide/session/status` (Phase 9 telemetry). No new status addresses needed.
- **D-12:** Ship a `.tosc` template file in the repo at `assets/JamWide.tosc`. Template targets iPad landscape layout (1024x768 base).
- **D-13:** Template includes 8 remote user slots (covers most sessions). Each slot has: name label, volume fader (0-1), pan knob (0-1), mute button, solo button. OSC server supports all 16 slots -- template can be extended by the user if needed.
- **D-14:** Template also includes: 4 local channel strips (from Phase 9), master volume + mute, metronome volume + pan + mute, session info panel (BPM, BPI, beat, status, users), connect/disconnect button.
- **D-15:** Template uses the full address namespace documented in docs/osc.md. Zero manual configuration -- user imports template, sets host IP in TouchOSC connections, and it works.
- **D-16:** VU meters in template: master L/R and local 1-4 L/R bars. Remote VU optional (may be too dense for 16 slots).
- **D-17:** Remote user addresses are generated dynamically at send time in OscServer::timerCallback(), not added to the static OscAddressMap. This avoids rebuilding the map on roster changes and matches the VU meter pattern from Phase 9 (always-dirty, no dirty tracking).
- **D-18:** Incoming remote user OSC messages are parsed by prefix matching `/JamWide/remote/` in handleOscOnMessageThread(), extracting idx and control name from the path. No static map entry needed for receive either.
- **D-19:** Use `processor.cachedUsers` (std::vector<RemoteUserInfo>) for send-time iteration. This is already populated by NinjamRunThread on the run thread and is safe to read from the message thread (atomic swap pattern).

### Claude's Discretion
- TouchOSC template visual design (colors, label fonts, fader styles within TouchOSC editor)
- Exact layout grid for 16 remote slots (rows x columns, spacing)
- Whether to include VU meters for all 16 remote slots or just first 8 in the template
- Exact string format for connect trigger parsing (delimiter between host and port)
- Error handling for malformed connect strings

### Deferred Ideas (OUT OF SCOPE)
- Video control via OSC namespace (`/JamWide/video/*`) -- Phase 13
- Phone-optimized template (4 remote slots) -- future template variant
- OSC query protocol (OSCQuery) for automatic address discovery -- future
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| OSC-04 | User can control remote users via index-based OSC addressing (`/remote/{idx}/volume`) | Dynamic prefix parsing in `handleOscOnMessageThread`, dispatch via `SetUserChannelStateCommand` / `SetUserStateCommand` through `cmd_queue`; feedback via `sendDirtyRemoteUsers` reading from `cachedUsers` |
| OSC-05 | User can see remote user names update on their control surface when the roster changes | String-typed OSC messages via `juce::OSCMessage::addString()`; roster broadcast triggered by `UserInfoChangedEvent` consuming `processor.cachedUsers` |
| OSC-08 | User can connect/disconnect from a NINJAM server via OSC trigger | String argument extraction from incoming OSC (`getString()`); dispatch `ConnectCommand` / `DisconnectCommand` via `cmd_queue`; uses stored `lastServerAddress`, `lastUsername` credentials |
| OSC-11 | User can load a shipped TouchOSC template for immediate use with JamWide | `.tosc` file is zlib-compressed XML; create in TouchOSC editor; ship at `assets/JamWide.tosc`; addresses match `docs/osc.md` namespace |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| juce_osc | JUCE 8 (bundled) | OSC send/receive, bundle mode, string arguments | Already linked in Phase 9; `OSCMessage::addString()` / `getString()` fully supported [VERIFIED: libs/juce/modules/juce_osc/osc/juce_OSCArgument.h] |
| NJClient | In-tree (src/core) | Remote user state: GetUserState, SetUserState, SetUserChannelState, GetRemoteUsersSnapshot | Existing API, thread-safe snapshot pattern [VERIFIED: src/core/njclient.h] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| juce::String | JUCE 8 | String parsing for address prefix matching and connect trigger | `startsWith()`, `substring()`, `upToFirstOccurrenceOf()` for OSC address parsing |
| TouchOSC Editor | Latest | Template creation tool | Create `.tosc` file visually rather than programmatic XML generation |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| TouchOSC Editor (manual) | tosclib Python (programmatic) | Programmatic generation is fragile -- XML schema is undocumented, editor is the canonical tool |
| Dynamic prefix parsing | Static map entries for all 16 slots | 16 x 4 controls x 2 (dB variant) = ~160 entries; dynamic parsing is cleaner and matches VU pattern |

## Architecture Patterns

### Recommended Project Structure
```
juce/osc/OscServer.h            # Add new method declarations
juce/osc/OscServer.cpp           # Add 4 new methods + extend 2 existing methods
docs/osc.md                      # Update with remote user address tables
assets/JamWide.tosc              # New: shipped TouchOSC template
```

### Pattern 1: Dynamic Address Generation (Send Path)
**What:** Build OSC address strings at send time by iterating `cachedUsers` with positional index, rather than pre-registering addresses in OscAddressMap.
**When to use:** For remote user parameters and VU meters where the address set changes dynamically as users join/leave.
**Example:**
```cpp
// Source: follows existing sendVuMeters pattern in OscServer.cpp:407-431
void OscServer::sendDirtyRemoteUsers(juce::OSCBundle& bundle, bool& hasContent)
{
    const auto& users = processor.cachedUsers;
    const int count = juce::jmin(static_cast<int>(users.size()), 16);  // D-05: max 16

    for (int i = 0; i < count; ++i)
    {
        juce::String idx(i + 1);  // 1-based
        juce::String prefix = "/JamWide/remote/" + idx;

        // Group bus level: read from RemoteUserInfo
        const auto& user = users[static_cast<size_t>(i)];

        // Volume feedback (normalized 0-1, user.volume is NJClient linear)
        // ... dirty check against lastSentRemoteValues ...
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern(prefix + "/volume"), user.volume));

        // Per sub-channel: iterate user.channels
        for (size_t c = 0; c < user.channels.size(); ++c)
        {
            juce::String chPrefix = prefix + "/ch/" + juce::String(c + 1);
            const auto& ch = user.channels[c];
            bundle.addElement(juce::OSCMessage(
                juce::OSCAddressPattern(chPrefix + "/volume"), ch.volume));
            // ... pan, mute, solo similarly
        }

        hasContent = true;
    }
}
```

### Pattern 2: Prefix-Based Dispatch (Receive Path)
**What:** Parse incoming `/JamWide/remote/{idx}/{control}` addresses by prefix matching before falling through to the static OscAddressMap lookup.
**When to use:** For all incoming remote user control messages.
**Example:**
```cpp
// Source: extends existing handleOscOnMessageThread in OscServer.cpp:154-242
void OscServer::handleOscOnMessageThread(const juce::String& address, float value)
{
    // NEW: Check for remote user prefix before static map lookup
    if (address.startsWith("/JamWide/remote/"))
    {
        handleRemoteUserOsc(address, value);
        return;
    }

    // Existing static map lookup follows...
    int idx = addressMap.resolve(address);
    // ...
}
```

### Pattern 3: String OSC Message Handling (Connect Trigger)
**What:** Extend `oscMessageReceived` to handle string-typed OSC arguments alongside the existing float/int handling. The connect trigger sends a string "host:port".
**When to use:** For `/JamWide/session/connect` (string arg) and `/JamWide/session/disconnect` (float trigger or no-arg).
**Example:**
```cpp
// Source: extends existing oscMessageReceived in OscServer.cpp:109-139
void OscServer::oscMessageReceived(const juce::OSCMessage& msg)
{
    auto address = msg.getAddressPattern().toString();

    // Check for string argument (connect trigger)
    if (msg.size() > 0 && msg[0].isString())
    {
        juce::String strValue = msg[0].getString();
        auto aliveFlag = alive;
        juce::MessageManager::callAsync([this, aliveFlag,
                                         addr = std::move(address),
                                         str = std::move(strValue)]()
        {
            if (!aliveFlag->load(std::memory_order_acquire))
                return;
            handleOscStringOnMessageThread(addr, str);
        });
        return;
    }

    // Existing float/int handling...
}
```

### Pattern 4: Roster Broadcast via UserInfoChangedEvent
**What:** When the editor receives `UserInfoChangedEvent`, the OscServer also needs to broadcast roster names. Since OscServer runs on the message thread (timerCallback is message thread), it can observe roster changes via a dirty flag set by the editor event drain or by checking `cachedUsers` directly.
**When to use:** On every `UserInfoChangedEvent` from the run thread.
**Example:**
```cpp
// Approach: Add a rosterDirty flag on OscServer. Set it from the editor's event drain
// (same place that calls refreshChannelStrips). OR: simpler approach --
// check cachedUsers size/content hash each timer tick and broadcast when changed.
// The simpler approach avoids coupling OscServer to the editor event drain.
void OscServer::sendRemoteRoster(juce::OSCBundle& bundle, bool& hasContent)
{
    const auto& users = processor.cachedUsers;
    const int count = juce::jmin(static_cast<int>(users.size()), 16);

    for (int i = 0; i < count; ++i)
    {
        juce::String idx(i + 1);
        juce::String name = juce::String(users[static_cast<size_t>(i)].name);
        // Strip @IP suffix
        int atIdx = name.lastIndexOfChar('@');
        if (atIdx > 0) name = name.substring(0, atIdx);

        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/remote/" + idx + "/name"), name));
        hasContent = true;

        // Sub-channel names
        const auto& user = users[static_cast<size_t>(i)];
        for (size_t c = 0; c < user.channels.size(); ++c)
        {
            juce::String chName = juce::String(user.channels[c].name);
            bundle.addElement(juce::OSCMessage(
                juce::OSCAddressPattern("/JamWide/remote/" + idx + "/ch/"
                    + juce::String(c + 1) + "/name"), chName));
        }
    }

    // Clear empty slots (send empty string)
    for (int i = count; i < 16; ++i)
    {
        juce::String idx(i + 1);
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/remote/" + idx + "/name"),
            juce::String()));
        hasContent = true;
    }
}
```

### Pattern 5: Group Bus vs. Sub-Channel Dispatch
**What:** Distinguish between `/JamWide/remote/{idx}/volume` (group bus, uses `SetUserStateCommand`) and `/JamWide/remote/{idx}/ch/{n}/volume` (sub-channel, uses `SetUserChannelStateCommand`). The group bus controls the overall user volume/pan/mute, while sub-channel controls individual channels.
**When to use:** In the `handleRemoteUserOsc` prefix parser.
**Key distinction:**
- Group bus path has 4 segments after `/JamWide/remote/{idx}/`: e.g., `volume`, `pan`, `mute`, `solo`
- Sub-channel path has 6+ segments: `ch/{n}/volume`, `ch/{n}/pan`, etc.

```cpp
// Parse: /JamWide/remote/3/volume -> user_index=2 (0-based), group bus volume
// Parse: /JamWide/remote/3/ch/2/volume -> user_index=2, channel=1 (0-based), sub-channel volume
```

### Anti-Patterns to Avoid
- **Static map entries for remote users:** Don't add remote user addresses to OscAddressMap. It would require rebuilding the map on every roster change and creating up to 160+ entries. Dynamic parsing is cleaner.
- **Reading NJClient directly from message thread:** Never call `NJClient::GetUserState()` or `SetUserChannelState()` from the message thread. Always use `cachedUsers` for reads and `cmd_queue` for writes. This preserves the threading contract.
- **Sending roster on every timer tick:** Only send roster names when users actually change (dirty flag check). Sending 16+ string messages every 100ms is wasteful.
- **Parsing connect string without validation:** The connect trigger string "host:port" must be parsed defensively. Missing port, empty host, malformed strings should be handled gracefully (ignore or log, do not crash).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| TouchOSC template | XML generation code | TouchOSC Editor application | .tosc format is undocumented, zlib-compressed XML; editor is the canonical creation tool [VERIFIED: tosclib docs confirm XML+zlib format] |
| OSC string argument handling | Custom string serialization | `juce::OSCMessage::addString()` / `msg[0].getString()` | Already supported by juce_osc; handles null-padding, size alignment per OSC spec [VERIFIED: juce_osc source] |
| Remote user state reads | Direct NJClient access from message thread | `processor.cachedUsers` snapshot | Thread-safe snapshot pattern established in Phase 4; breaking this causes data races [VERIFIED: JamWideJuceProcessor.h threading contract] |
| Address string parsing | Regex or complex tokenizer | `juce::String::startsWith()` + `substring()` + `getIntValue()` | OSC addresses are simple `/` delimited; JUCE String methods are sufficient and efficient |

**Key insight:** The entire remote user OSC system follows patterns already proven in Phase 9. The VU meter sender pattern (dynamic address, always-dirty, iterate snapshot) is the template for remote user parameter sends. The cmd_queue dispatch pattern (used by local solo) is the template for incoming remote user control. No new architectural patterns are required.

## Common Pitfalls

### Pitfall 1: User Index Mismatch Between OSC and NJClient
**What goes wrong:** OSC uses 1-based indexing (matching UI convention), but NJClient APIs use 0-based user indices. A naive implementation passes the OSC index directly to `SetUserChannelState()`, causing off-by-one control of the wrong user.
**Why it happens:** The codebase uses both conventions in different places. OSC local channels are 1-based, APVTS parameters are 0-based.
**How to avoid:** Always subtract 1 from the OSC index before dispatching to cmd_queue. Add bounds checking: `if (oscIdx < 1 || oscIdx > count) return;`
**Warning signs:** Controlling remote user 1 in TouchOSC changes remote user 2 in the plugin.

### Pitfall 2: Sub-Channel Index vs. channel_index (Bit Index)
**What goes wrong:** NJClient's `SetUserChannelState` uses `channelidx` which is a NINJAM bit index (sparse, from the channel present bitmask), not a sequential 0-based index. Passing the OSC sub-channel number directly causes controlling the wrong channel or a no-op (if that bit index has no channel).
**Why it happens:** The OSC address uses sequential sub-channel numbering `/ch/1`, `/ch/2`, but NJClient uses `RemoteChannelInfo::channel_index` which is a bit position in the user's `chanpresentmask`.
**How to avoid:** When receiving an OSC sub-channel command, look up the user in `cachedUsers`, index into `user.channels[oscChIdx - 1]`, and use `channels[n].channel_index` as the actual NINJAM channel index for `SetUserChannelStateCommand::channel_index`.
**Warning signs:** Solo/mute on sub-channel 2 does nothing, or controls the wrong channel.

### Pitfall 3: String Argument Handling in oscMessageReceived
**What goes wrong:** The current `oscMessageReceived()` returns early if the first argument is not float32 or int32 (line 122: `return; // Unsupported argument type`). This means string-typed connect triggers are silently dropped.
**Why it happens:** Phase 9 only needed float/int arguments. String was not implemented.
**How to avoid:** Add a `msg[0].isString()` branch before the existing float/int handling. Dispatch string-typed messages through a separate code path on the message thread.
**Warning signs:** Connect button in TouchOSC does nothing; no error visible.

### Pitfall 4: Roster Broadcast Flooding
**What goes wrong:** If roster broadcast fires on every 100ms timer tick (treating it like VU meters), it sends 16+ string messages per tick continuously. String OSC messages are larger than float messages and this creates unnecessary network traffic.
**Why it happens:** Treating roster as "always-dirty" like VU meters.
**How to avoid:** Track roster state (e.g., last user count + hash of names) and only re-broadcast when changed. A simple approach: store `lastSentUserCount` and compare on each tick, or use a `rosterDirty` flag set when `cachedUsers` changes.
**Warning signs:** High OSC traffic even when session is idle; TouchOSC shows CPU usage from constant string message processing.

### Pitfall 5: Connect Trigger Uses Stale Credentials
**What goes wrong:** The connect trigger uses `processor.lastUsername` and stored password, but these may be empty or stale (default: "anonymous" / "ninbot.com"). User expects to connect to a specific server but gets default credentials.
**Why it happens:** D-09 specifies using "last connection config stored on processor", but if the user has never connected through the plugin UI, there are no stored credentials.
**How to avoid:** Check that `lastUsername` is not empty before dispatching. If empty, use "anonymous" as fallback (matching NinjamRunThread ConnectCommand processing). Document that the connect trigger reuses the last UI-configured credentials.
**Warning signs:** Connect via OSC logs in as wrong user or fails authentication.

### Pitfall 6: Group Bus SetUserState vs. SetUserChannelState Confusion
**What goes wrong:** Using `SetUserChannelState` with `channel_index=0` for the group bus level, when `SetUserState` is the correct API for user-level volume/pan/mute.
**Why it happens:** D-03 says "SetUserChannelState on channel 0 / group", but looking at the actual NJClient API, `SetUserState(idx, setvol, vol, setpan, pan, setmute, mute)` controls the user-level state, while `SetUserChannelState` controls individual channels. Channel 0 is a specific channel, not a group bus alias.
**How to avoid:** Use `SetUserStateCommand` for `/JamWide/remote/{idx}/volume`, `/pan`, `/mute`. Use `SetUserChannelStateCommand` for `/JamWide/remote/{idx}/ch/{n}/volume`, etc. The UI code confirms this pattern (ChannelStripArea.cpp lines 522-557 use `SetUserStateCommand` for parent strip callbacks).
**Warning signs:** Group bus volume control only affects the first channel, not the overall user mix.

## Code Examples

Verified patterns from the existing codebase:

### Remote User Control Dispatch (UI Reference Pattern)
```cpp
// Source: juce/ui/ChannelStripArea.cpp:525-530 (VERIFIED)
// Parent strip (group bus) uses SetUserStateCommand
jamwide::SetUserStateCommand cmd;
cmd.user_index = u;          // 0-based
cmd.set_vol = true;
cmd.volume = vol;             // NJClient linear (0-2 range)
processorRef.cmd_queue.try_push(std::move(cmd));
```

### Sub-Channel Control Dispatch (UI Reference Pattern)
```cpp
// Source: juce/ui/ChannelStripArea.cpp:437-442 (VERIFIED)
// Child strip (sub-channel) uses SetUserChannelStateCommand
jamwide::SetUserChannelStateCommand cmd;
cmd.user_index = uIdx;       // 0-based
cmd.channel_index = cIdx;    // NINJAM bit index from findRemoteIndex
cmd.set_vol = true;
cmd.volume = vol;
processorRef.cmd_queue.try_push(std::move(cmd));
```

### String OSC Message (juce_osc API)
```cpp
// Source: libs/juce/modules/juce_osc/osc/juce_OSCMessage.h:144 (VERIFIED)
// Sending a string:
juce::OSCMessage msg(juce::OSCAddressPattern("/JamWide/remote/1/name"));
msg.addString("Dave");
// In bundle:
bundle.addElement(juce::OSCMessage(
    juce::OSCAddressPattern("/JamWide/remote/1/name"), juce::String("Dave")));

// Receiving a string:
if (msg[0].isString())
    juce::String value = msg[0].getString();
```

### Connect Command Dispatch (Existing Pattern)
```cpp
// Source: juce/NinjamRunThread.cpp:467-476 (VERIFIED)
// ConnectCommand is already handled in processCommands
jamwide::ConnectCommand cmd;
cmd.server = "ninbot.com:2049";
cmd.username = processor.lastUsername.toStdString();
cmd.password = "";  // from last config
processor.cmd_queue.try_push(std::move(cmd));
```

### VU Meter Always-Dirty Pattern (Template for Remote VU)
```cpp
// Source: juce/osc/OscServer.cpp:407-431 (VERIFIED)
void OscServer::sendVuMeters(juce::OSCBundle& bundle, bool& hasContent)
{
    // VU meters are always "dirty" -- sent every tick
    auto& snap = processor.uiSnapshot;
    bundle.addElement(juce::OSCMessage(
        juce::OSCAddressPattern("/JamWide/master/vu/left"),
        snap.master_vu_left.load(std::memory_order_relaxed)));
    // ... pattern continues for local channels
    hasContent = true;
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Static address map for all params | Dynamic prefix parsing for remote users | Phase 10 (new) | Avoids map rebuild on roster changes; cleaner for variable-count entities |
| Float-only OSC arguments | Float + String argument support | Phase 10 (new) | Enables name broadcast and connect trigger |
| Local-only OSC control | Local + remote user OSC control | Phase 10 (new) | Full mixer control from external surfaces |

**Not deprecated:**
- Phase 9 static `OscAddressMap` remains for local channels, master, metro, telemetry -- no changes needed to existing map

## Assumptions Log

> List all claims tagged `[ASSUMED]` in this research. The planner and discuss-phase use this
> section to identify decisions that need user confirmation before execution.

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | TouchOSC `.tosc` template should be created manually in the TouchOSC editor app rather than generated programmatically | Don't Hand-Roll | If user expects auto-generation from code, approach differs significantly; however, manual creation produces a verified working template |
| A2 | Remote user volume range in NJClient is 0-2 linear (matching local channel APVTS range) | Code Examples | If range differs, OSC normalization math would be wrong; verified from NJClient API that SetUserState takes float vol directly |
| A3 | The `@IP` suffix stripping for display names should match the pattern used in ChannelStripArea.cpp | Architecture Patterns | Cosmetic only; if pattern differs, name labels show raw usernames with IP |

## Open Questions

1. **Remote User Volume Range and Normalization**
   - What we know: NJClient `SetUserState` takes a raw float volume. The UI's parent strip fader feeds this directly. `RemoteUserInfo::volume` is read back directly from `user->volume`.
   - What's unclear: Is the NJClient user volume range 0-1 or 0-2? Local APVTS volumes use 0-2 (with 0.5 normalized = unity). If remote user volume is 0-1 (where 1.0 = unity), the OSC normalization differs from local channels.
   - Recommendation: Check `NJClient::SetUserState` and the fader range in `ChannelStripArea` parent strip. If range is 0-2, normalize to OSC 0-1 the same way as local channels. If range is 0-1, send raw value directly.

2. **Solo on Remote Users (Group Bus Level)**
   - What we know: `SetUserStateCommand` has `set_vol`, `set_pan`, `set_mute` fields but no `set_solo` field. `SetUserChannelStateCommand` has `set_solo`. D-02 specifies `/JamWide/remote/{idx}/solo`.
   - What's unclear: NJClient's `SetUserState` doesn't have a solo parameter (only vol, pan, mute). Solo at the group bus level may need to be implemented by toggling solo on all sub-channels, or it may be a concept that only exists at the sub-channel level.
   - Recommendation: Implement group-level solo by setting solo on all sub-channels of the user (iterate `user.channels`, dispatch `SetUserChannelStateCommand` with `set_solo=true` for each). This matches the "solo a user" mental model.

3. **Roster Change Detection Without Editor Coupling**
   - What we know: `UserInfoChangedEvent` is consumed by the editor's event drain. OscServer doesn't currently subscribe to events.
   - What's unclear: Best way for OscServer to know when roster changed without coupling to the editor.
   - Recommendation: Track `lastSentUserCount` and a simple hash (sum of name lengths) in OscServer. On each timer tick, compare against current `cachedUsers`. When different, broadcast roster and update cache. This is simple, decoupled, and fires on the message thread where `cachedUsers` is safe to read.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Custom C++ smoke tests (no framework; return 0/1 pattern) |
| Config file | CMakeLists.txt `JAMWIDE_BUILD_TESTS` flag |
| Quick run command | `cd build-juce && ctest --test-dir . -R osc_loopback --output-on-failure` |
| Full suite command | `cd build-juce && ctest --test-dir . --output-on-failure` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| OSC-04 | Remote user volume/pan/mute/solo controllable via OSC | manual | Manual: send OSC from TouchOSC to connected session | N/A |
| OSC-05 | Roster name broadcast updates control surface labels | manual | Manual: connect users, verify name labels update | N/A |
| OSC-08 | Connect/disconnect via OSC trigger | manual | Manual: send `/session/connect` string from TouchOSC | N/A |
| OSC-11 | Shipped .tosc template works in TouchOSC | manual | Manual: import .tosc, verify controls work | N/A |

**Justification for manual-only:** All four requirements involve bidirectional interaction between JamWide (compiled C++ JUCE plugin running in a DAW or standalone) and a running NINJAM server with other connected users. There is no way to simulate a multi-user NINJAM session in an automated unit test. The existing `test_osc_loopback` validates juce_osc linkage but cannot test business logic without a full NJClient session.

### Sampling Rate
- **Per task commit:** Build check (`cmake --build build-juce --target JamWide_VST3 --parallel`)
- **Per wave merge:** Full build + existing tests (`ctest --test-dir build-juce --output-on-failure`)
- **Phase gate:** Full build on all targets (VST3 + CLAP + Standalone) + manual TouchOSC verification

### Wave 0 Gaps
None -- existing test infrastructure covers build validation. No new automated tests are feasible for this phase's requirements.

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | N/A (OSC is local network, no auth) |
| V3 Session Management | no | N/A |
| V4 Access Control | no | N/A (OSC server is user-controlled) |
| V5 Input Validation | yes | Bounds checking on OSC index/value ranges; string length limits on connect trigger; graceful handling of malformed addresses |
| V6 Cryptography | no | N/A |

### Known Threat Patterns for OSC

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed OSC address injection | Tampering | Prefix matching with strict format validation; ignore unrecognized paths |
| Out-of-range user index | Tampering | Bounds check: `idx >= 1 && idx <= min(cachedUsers.size(), 16)` before dispatch |
| Oversized connect string | Denial of Service | Limit string length (e.g., 256 chars max, matching server_input buffer in UiState) |
| Connect trigger to arbitrary server | Information Disclosure | Document that connect uses stored credentials; no new attack surface beyond what UI already exposes |

## Sources

### Primary (HIGH confidence)
- `juce/osc/OscServer.cpp` -- Phase 9 implementation, all send/receive patterns verified
- `juce/osc/OscServer.h` -- Current public API to extend
- `juce/osc/OscAddressMap.h` / `.cpp` -- Static map structure, pan/dB conversion utilities
- `juce/JamWideJuceProcessor.h` -- Threading contract, cachedUsers, cmd_queue
- `src/threading/ui_command.h` -- SetUserStateCommand, SetUserChannelStateCommand, ConnectCommand
- `src/threading/ui_event.h` -- UserInfoChangedEvent definition
- `src/core/njclient.h` -- RemoteUserInfo/RemoteChannelInfo structs, GetRemoteUsersSnapshot, SetUserChannelState, SetUserState APIs
- `src/core/njclient.cpp` -- SetUserState implementation (line 2764), SetUserChannelState (line 2811), GetRemoteUsersSnapshot (line 2696)
- `juce/ui/ChannelStripArea.cpp` -- findRemoteIndex pattern, parent strip vs child strip dispatch, VU meter update pattern
- `juce/NinjamRunThread.cpp` -- processCommands variant dispatch, ConnectCommand/DisconnectCommand handling, UserInfoChangedEvent push
- `libs/juce/modules/juce_osc/osc/juce_OSCArgument.h` -- getString(), isString() API confirmed
- `libs/juce/modules/juce_osc/osc/juce_OSCMessage.h` -- addString() API confirmed
- `docs/osc.md` -- Current Phase 9 OSC address reference

### Secondary (MEDIUM confidence)
- [TouchOSC Manual - hexler.net](https://hexler.net/touchosc/manual/complete) -- Control types (FADER, BUTTON, LABEL, GROUP), OSC message configuration
- [TouchOSC Script Properties](https://hexler.net/touchosc/manual/script-properties-and-values) -- Control property names
- [tosclib documentation](https://albertov5.github.io/tosclib/docs/build/html/index.html) -- .tosc file format: zlib-compressed XML, node structure

### Tertiary (LOW confidence)
- [tosclib GitHub](https://github.com/AlbertoV5/tosclib) -- Python library for .tosc manipulation (not recommended for use, but confirms format details)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all libraries already in use from Phase 9, APIs verified in source
- Architecture: HIGH -- all patterns directly extend Phase 9 patterns verified in OscServer.cpp
- Pitfalls: HIGH -- identified from actual code analysis (index mismatch, string handling gap, SetUserState vs SetUserChannelState)
- TouchOSC template: MEDIUM -- format confirmed as zlib+XML, but exact template creation is manual and design is discretionary

**Research date:** 2026-04-06
**Valid until:** 2026-05-06 (stable -- no external dependencies changing)
