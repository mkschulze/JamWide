---
phase: 260413-udi
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - juce/ui/ServerBrowserOverlay.cpp
autonomous: false
requirements: [260413-udi]

must_haves:
  truths:
    - "Each server row in the browser shows usernames below the topic line"
    - "Empty rooms display a dimmed 'No users' placeholder"
    - "All rows have consistent height regardless of user presence"
    - "Username text is smaller and de-emphasized compared to server name"
  artifacts:
    - path: "juce/ui/ServerBrowserOverlay.cpp"
      provides: "Username line rendering in paintListBoxItem, increased row height"
      contains: "user_list"
  key_links:
    - from: "juce/ui/ServerBrowserOverlay.cpp paintListBoxItem()"
      to: "ServerListEntry.user_list"
      via: "direct field access on entry"
      pattern: "entry\\.user_list"
---

<objective>
Add username display to the server browser overlay so users can see who is in each room before joining.

Purpose: Users currently see server name, address, user count, BPM/BPI, and topic -- but not WHO is in the room. Displaying usernames helps musicians find rooms with people they know.

Output: Modified ServerBrowserOverlay.cpp with username line rendered below topic, increased row height, and dimmed placeholder for empty rooms.

Note: Audio prelisten was assessed in research and found architecturally infeasible as a quick task (requires full NJClient connection, audio pipeline, license handling). Deferred per Claude's discretion.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/quick/260413-udi-add-usernames-in-server-room-list-and-au/260413-udi-CONTEXT.md
@.planning/quick/260413-udi-add-usernames-in-server-room-list-and-au/260413-udi-RESEARCH.md

<interfaces>
<!-- Key types and contracts the executor needs. Extracted from codebase. -->

From src/ui/server_list_types.h:
```cpp
struct ServerListEntry {
    std::string name;
    std::string host;
    int port = 0;
    int users = 0;
    int max_users = 0;       // max user slots
    std::string user_list;   // comma-separated usernames
    std::string topic;
    int bpm = 0;
    int bpi = 0;
    bool is_lobby = false;
};
```

From juce/ui/JamWideLookAndFeel.h (color constants):
```cpp
static constexpr juce::uint32 kBorderSubtle     = 0xff3A3D58;
static constexpr juce::uint32 kTextPrimary      = 0xffE0E0E0;
static constexpr juce::uint32 kTextSecondary    = 0xff8888AA;
```

From juce/ui/ServerBrowserOverlay.h:
```cpp
static constexpr int kDialogWidth = 600;
static constexpr int kDialogHeight = 500;
// Row height set in constructor: listBox.setRowHeight(56);
```
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Add username line to server browser rows</name>
  <files>juce/ui/ServerBrowserOverlay.cpp</files>
  <action>
Modify `juce/ui/ServerBrowserOverlay.cpp` to display usernames in each server browser row:

1. **Increase row height** in the constructor: change `listBox.setRowHeight(56)` to `listBox.setRowHeight(72)`. This adds 16px for the new username line.

2. **Add username rendering** in `paintListBoxItem()`, after the topic block (after the `drawText` for `entry.topic` around line 209) and before the bottom border line. Insert:

```cpp
// Username line (below topic)
auto userFont = juce::FontOptions(10.0f);
g.setFont(userFont);
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

Key details:
- The username line is always rendered (Y=52, height=14) whether the topic exists or not. This ensures consistent row layout per the locked decision on consistent height.
- Font size 10.0f is slightly smaller than the 11.0f used elsewhere, de-emphasizing usernames relative to server name and topic.
- Populated rooms use kTextSecondary (0xff8888AA) matching address/topic styling.
- Empty rooms use kTextSecondary at 0.4 alpha for a dimmed "No users" placeholder.
- `drawText(..., true)` enables automatic ellipsis truncation for long user lists (25-user servers).
- Do NOT filter bot names (ninbot_, Jambot) -- show raw data from server list.
- Do NOT conditionally hide the line based on JSON vs plain-text format -- empty user_list gracefully shows "No users" for both truly empty rooms and JSON-format entries.
- The bottom 1px border at `height - 1` automatically adjusts since it uses the `height` parameter (now 72).
  </action>
  <verify>
    <automated>cd /Users/cell/dev/JamWide && cmake --build build --target JamWide_VST3 2>&1 | tail -5</automated>
  </verify>
  <done>
    - Server browser rows are 72px tall (was 56px)
    - Each row shows a username line at Y=52 in 10pt font
    - Rooms with users show comma-separated names in kTextSecondary color
    - Empty rooms show "No users" in dimmed kTextSecondary (0.4 alpha)
    - Build succeeds with no errors or warnings in ServerBrowserOverlay.cpp
  </done>
</task>

<task type="checkpoint:human-verify" gate="blocking">
  <name>Task 2: Verify username display in server browser</name>
  <files>juce/ui/ServerBrowserOverlay.cpp</files>
  <what-built>Username display in server browser overlay rows. Each row now shows who is in the room below the topic line, or "No users" dimmed for empty rooms.</what-built>
  <action>Human visual verification of the username display in the server browser overlay.</action>
  <how-to-verify>
    1. Launch the built VST3 or Standalone in a DAW or host
    2. Open the server browser (Browse Servers button)
    3. Verify each server row shows:
       - Server name and address (unchanged, top)
       - User count and BPM/BPI (unchanged, right side)
       - Topic (unchanged, below name/address)
       - NEW: Username list below topic in smaller, slightly dimmed text
    4. Verify empty rooms show "No users" in noticeably dimmer text
    5. Verify rows with many users truncate gracefully with ellipsis
    6. Verify row spacing feels right (not too cramped or too sparse)
    7. Verify clicking/double-clicking rows still works correctly
  </how-to-verify>
  <verify>Human confirms visual appearance and interaction behavior</verify>
  <done>User approves the username display layout, font, color, and row spacing</done>
  <resume-signal>Type "approved" or describe issues with spacing, font size, or colors</resume-signal>
</task>

</tasks>

<threat_model>
## Trust Boundaries

No new trust boundaries introduced. The `user_list` data comes from the existing server list HTTP response (already trusted as remote server data). No new network requests, no user input handling changes.

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-260413-01 | Information Disclosure | paintListBoxItem | accept | Username list is public server data already visible to any NINJAM client; no PII beyond chosen display names |
| T-260413-02 | Spoofing | server_list parser | accept | Server list endpoint could return fake usernames; this is pre-existing trust of the server list source, not new to this change |
</threat_model>

<verification>
- Build completes without errors: `cmake --build build --target JamWide_VST3`
- Visual verification: server browser shows usernames in each row
- Empty room rows show dimmed "No users" placeholder
- Row height increased to accommodate new line without layout overlap
</verification>

<success_criteria>
- Server browser overlay displays comma-separated usernames below the topic line in every row
- Empty rooms show "No users" as a dimmed placeholder
- All rows have consistent 72px height
- Username text is visually de-emphasized (smaller font, secondary color)
- No regressions in click/double-click behavior or overlay dismiss
</success_criteria>

<output>
After completion, create `.planning/quick/260413-udi-add-usernames-in-server-room-list-and-au/260413-udi-SUMMARY.md`
</output>
