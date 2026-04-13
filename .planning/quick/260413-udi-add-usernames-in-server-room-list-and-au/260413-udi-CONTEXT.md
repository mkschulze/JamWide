# Quick Task 260413-udi: Add usernames in server room list and audio prelisten before entering a room - Context

**Gathered:** 2026-04-13
**Status:** Ready for planning

<domain>
## Task Boundary

Add usernames from people in the room list when browsing the servers, and a prelisten of the audio going on in the room before entering.

Note: Audio prelisten requires a full NINJAM protocol connection (not available via HTTP server list). This was not selected for discussion — defer to planning/research for feasibility assessment.

</domain>

<decisions>
## Implementation Decisions

### Username Display
- Show usernames **inline in the row**, below the topic line, in a smaller font
- Comma-separated list (e.g. "alice, bob, charlie")
- Row height increases slightly to accommodate the extra line

### Empty Rooms
- Display a dimmed placeholder like "No users" so all rows have consistent height

### Claude's Discretion
- Audio prelisten approach (background connect vs. deferred)
- Exact font size and color for username line
- Row height adjustment amount

</decisions>

<specifics>
## Specific Ideas

- `ServerListEntry` already has a `user_list` field (comma-separated string) parsed from the server list HTTP response
- Current row height is 56px in `paintListBoxItem()`
- Username data is "free" — no extra network requests needed
- Audio prelisten would require connecting to the NINJAM server silently in the background

</specifics>

<canonical_refs>
## Canonical References

- `src/ui/server_list_types.h` — `ServerListEntry` struct with `user_list` field
- `juce/ui/ServerBrowserOverlay.h/.cpp` — Server browser overlay UI component
- `src/net/server_list.h/.cpp` — HTTP server list fetcher and parser

</canonical_refs>
