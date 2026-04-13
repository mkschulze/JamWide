# Quick Task: Add Usernames in Server Room List and Audio Prelisten - Research

**Researched:** 2026-04-13
**Domain:** JUCE ListBox UI painting, NINJAM server list data
**Confidence:** HIGH (username display), LOW (audio prelisten)

## Summary

Username display is straightforward: the `user_list` field in `ServerListEntry` is already populated by the plain-text NINJAM server list parser, and the default server list endpoint (`autosong.ninjam.com/serverlist.php`) returns plain-text format with comma-separated usernames. The main work is adding a third text line to `paintListBoxItem()` and increasing the row height.

Audio prelisten is architecturally infeasible as a quick task. NJClient requires a full protocol connection (TCP, auth handshake, license acceptance, audio thread integration). There is no spectator/listen-only mode in the NINJAM protocol. A background NJClient instance would need its own audio processing pipeline, thread management, and workdir -- essentially a second full client. This should be deferred.

**Primary recommendation:** Implement username display only. Defer audio prelisten to a future phase.

<user_constraints>

## User Constraints (from CONTEXT.md)

### Locked Decisions
- Show usernames **inline in the row**, below the topic line, in a smaller font
- Comma-separated list (e.g. "alice, bob, charlie")
- Row height increases slightly to accommodate the extra line
- Display a dimmed placeholder like "No users" for empty rooms so all rows have consistent height

### Claude's Discretion
- Audio prelisten approach (background connect vs. deferred)
- Exact font size and color for username line
- Row height adjustment amount

### Deferred Ideas (OUT OF SCOPE)
None specified.

</user_constraints>

## Current Implementation Analysis

### ServerBrowserOverlay Layout (paintListBoxItem)

Current row height: **56px**, set in constructor via `listBox.setRowHeight(56)`.

Current layout within each row (verified from source):

| Y offset | Height | Content | Font | Color |
|----------|--------|---------|------|-------|
| 4 | 18 | Server name (left half) | 13.0f | kTextPrimary (0xffE0E0E0) |
| 22 | 14 | Address host:port (left half) | 11.0f | kTextSecondary (0xff8888AA) |
| 4 | 14 | "N/M users" (right half, right-aligned) | 11.0f | kTextPrimary |
| 18 | 14 | "BPM / BPI" or "Lobby" (right half, right-aligned) | 11.0f | kTextPrimary |
| 36 | 14 | Topic (full width) | 11.0f (inherited) | kTextSecondary |
| height-1 | 1px | Bottom border line | -- | kBorderSubtle 50% alpha |

Selection highlight: fills entire row with `kBorderSubtle` (0xff3A3D58). [VERIFIED: ServerBrowserOverlay.cpp]

### ServerListEntry.user_list Field

**Plain-text format (default):** `user_list` IS populated. The parser extracts the portion after the colon in the third quoted string (e.g., `"5/25:Jake,Jason,Mark,ninbot_,SebNL"` yields `user_list = "Jake,Jason,Mark,ninbot_,SebNL"`). The `(empty)` placeholder is stripped to an empty string. [VERIFIED: server_list.cpp line 241-245]

**JSON format:** `user_list` is NOT populated. The `parse_json_format()` method does not read a `user_list` field from JSON. It also does not populate `max_users`, `bpm`, or `bpi`. [VERIFIED: server_list.cpp line 266-316]

**Live data from default endpoint** (`autosong.ninjam.com/serverlist.php`): Returns plain-text format. Tested 2026-04-13, returns `SERVER` lines with user data. Examples:
- `"5/25:Jake,Jason,Mark,ninbot_,SebNL"` -- 5 users, names available
- `"0/5:(empty)"` -- 0 users, parsed as empty string
- `"1/8:Jambot"` -- single bot user

[VERIFIED: curl to live endpoint]

### Interaction Callbacks

- `listBoxItemClicked(row, MouseEvent)` -- single click fills address into connection bar via `onServerSelected` callback [VERIFIED: ServerBrowserOverlay.cpp line 229-235]
- `listBoxItemDoubleClicked(row, MouseEvent)` -- double click fills address AND auto-connects via `onServerDoubleClicked`, then calls `dismiss()` [VERIFIED: ServerBrowserOverlay.cpp line 237-245]

No changes needed to interaction callbacks for username display.

## Architecture Patterns

### Username Line Addition

Add a fourth text line below the topic line. The user_list data is a comma-separated string that can be drawn directly -- no parsing needed.

**Recommended row height:** 72px (current 56px + 16px for the new line). This gives:
- Lines 1-3 unchanged (name, address, topic occupy Y=4 through Y=50)
- Line 4 at Y=52: username list, 14px tall
- 6px bottom padding before the 1px border

**Recommended font/color for usernames:**
- Font: 10.0f (slightly smaller than the 11.0f used for address/topic, to de-emphasize)
- Color for populated rooms: kTextSecondary (0xff8888AA) -- matches address/topic styling
- Color for empty rooms: kTextSecondary with reduced alpha (e.g., 0.4f) -- dimmed "No users" placeholder

### Code Changes Required

```cpp
// In constructor, change row height:
listBox.setRowHeight(72);  // was 56

// In paintListBoxItem, after the topic block (around line 210):
// Username line
auto usernameFont = juce::FontOptions(10.0f);
g.setFont(usernameFont);
if (!entry.user_list.empty())
{
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
    g.drawText(juce::String(entry.user_list),
        8, 52, width - 16, 14, juce::Justification::centredLeft, true);
}
else
{
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary).withAlpha(0.4f));
    g.drawText("No users",
        8, 52, width - 16, 14, juce::Justification::centredLeft, true);
}
```

### Dialog Height Consideration

Current dialog is 500px tall. With row height increasing from 56 to 72, ~7 rows visible instead of ~8. The dialog height constants (`kDialogWidth = 600`, `kDialogHeight = 500`) might need a small bump to 540 to compensate, but this is optional -- 7 visible rows is still reasonable.

## Audio Prelisten Feasibility Assessment

### Why It Cannot Work as a Quick Task

1. **No spectator mode in NINJAM protocol.** NJClient::Connect() initiates a full protocol handshake: TCP connect, auth challenge/response, license acceptance, channel configuration. There is no "listen-only" flag. [VERIFIED: njclient.h, NinjamRunThread.cpp]

2. **NJClient requires AudioProc integration.** The audio mixing happens in `NJClient::AudioProc()` which must be called from the audio thread with real sample buffers. A background "listen" would need to either hijack the current audio pipeline or create a parallel one. [VERIFIED: njclient.h line 115-116]

3. **Single NJClient per processor.** The architecture has one NJClient owned by JamWideJuceProcessor, one NinjamRunThread driving it. Running a second NJClient for prelisten would require a parallel infrastructure (thread, workdir for temp files, audio output routing). [VERIFIED: NinjamRunThread.cpp, JamWideJuceProcessor relationship]

4. **License acceptance is blocking.** Many NINJAM servers require license acceptance before streaming begins. The current implementation blocks the run thread and shows a UI dialog. A background prelisten would need to handle this silently. [VERIFIED: NinjamRunThread.cpp license_callback]

5. **Resource cost.** Each NJClient connection downloads compressed audio from all users, decodes it, and mixes it. This is not lightweight background work.

### Recommendation

**Defer audio prelisten entirely.** It is not a UI tweak but a fundamental architectural addition requiring a secondary NJClient instance, audio routing, and careful lifecycle management. If pursued in the future, it would warrant its own phase.

## Common Pitfalls

### Pitfall 1: JSON Format Missing user_list
**What goes wrong:** If a user configures a custom server list URL that returns JSON format, `user_list` will be empty for all entries even when users are connected.
**How to avoid:** Show "No users" gracefully (which the design already requires for empty rooms). Do NOT hide the username line conditionally based on format -- always show it, and the empty-room placeholder handles both truly empty rooms and JSON-format missing data identically.

### Pitfall 2: Long Username Lists Truncation
**What goes wrong:** A server with 25 users could produce a very long comma-separated string that gets truncated by `drawText(..., true)` (the `true` parameter enables ellipsis).
**How to avoid:** The JUCE `drawText` with `useEllipsesIfTooBig=true` handles this automatically. The current row width of 600px minus padding gives ~584px for text, which fits roughly 60-80 characters of usernames at 10pt. For servers with many users this will naturally truncate with "..." which is acceptable.

### Pitfall 3: Bot Users in List
**What goes wrong:** Bot users like "Jambot" and "ninbot_" appear in user_list, which may look odd.
**How to avoid:** Do not filter bots from the server list display. The server list shows raw data from the server -- filtering is only appropriate in the mixer where you control subscriptions. The user_list string comes pre-formatted from the server; modifying it adds complexity for negligible benefit.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | 72px row height provides enough space without feeling too sparse | Architecture Patterns | Rows look too tall or too cramped -- easily adjustable |
| A2 | 10.0f font size is readable for username line | Architecture Patterns | Text too small -- try 11.0f instead |

## Sources

### Primary (HIGH confidence)
- `juce/ui/ServerBrowserOverlay.cpp` -- full paintListBoxItem implementation, row height, layout
- `src/ui/server_list_types.h` -- ServerListEntry struct definition
- `src/net/server_list.cpp` -- both parsers (plain-text and JSON)
- `juce/NinjamRunThread.cpp` -- connection lifecycle, NJClient integration
- `src/core/njclient.h` -- NJClient API, no spectator mode
- Live curl to `autosong.ninjam.com/serverlist.php` -- confirmed plain-text format with user data

### Metadata

**Confidence breakdown:**
- Username display: HIGH -- all data verified in source, live endpoint tested
- Audio prelisten: HIGH (that it should be deferred) -- NJClient architecture clearly requires full connection
- Layout specifics: MEDIUM -- font sizes and heights are reasonable defaults but may need tuning

**Research date:** 2026-04-13
**Valid until:** 2026-05-13
