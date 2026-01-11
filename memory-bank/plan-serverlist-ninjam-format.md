# Plan: Switch to ninjam.com Server List Format

## Goal
Replace the current ninbot.com JSON API with the official `autosong.ninjam.com/serverlist.php` plain-text format, matching ReaNINJAM's approach.

## Current State
- **URL**: `http://ninbot.com/serverlist` (JSON)
- **Format**: JSON with `servers` array, each with `host`, `port`, `bpm`, `bpi`, `user_count`
- **Limitation**: User counts are snapshots, no usernames shown

## Target State
- **URL**: `http://autosong.ninjam.com/serverlist.php`
- **Format**: Plain text, line-based
- **Advantage**: Shows actual usernames, official source

## API Format Analysis

### Response Format
```
SERVER "ninjamer.com:2049" "110 BPM/16" "1/8:Jambot"
SERVER "ninbot.com:2052" "95 BPM/16" "6/8:DonPolettone,FoxIT,jhonep,ninbot_,Sharonn_OzZ,Waii_D"
SERVER "mutantlab.com:2049" "105 BPM/16" "0/5:(empty)"
END
```

### Fields per line
1. `SERVER` - keyword
2. `"host:port"` - server address (quoted)
3. `"BPM/BPI"` or `"lobby"` - tempo info (quoted)
4. `"users/max:usernames"` - user info (quoted)
   - Format: `current/max:name1,name2,...`
   - Empty servers show `(empty)` as username

### Parsing Logic
```cpp
// Parse line: SERVER "host:port" "tempo" "users/max:names"
// 1. Skip lines not starting with "SERVER"
// 2. Extract 3 quoted strings
// 3. Parse host:port -> separate fields
// 4. Parse "BPM/BPI" -> extract BPM and BPI integers
// 5. Parse "users/max:names" -> user_count, max_users, user_list
```

## Implementation Steps

### Phase 1: Update ServerListEntry struct
**File:** `src/ui/server_list_types.h`

```cpp
struct ServerListEntry {
    std::string name;        // unchanged
    std::string host;        // unchanged
    int port = 0;            // unchanged
    int users = 0;           // unchanged (current user count)
    int max_users = 0;       // NEW: max user slots
    std::string user_list;   // NEW: comma-separated usernames
    std::string topic;       // unchanged (may be empty)
    int bpm = 0;             // NEW: parsed BPM (0 for lobby)
    int bpi = 0;             // NEW: parsed BPI (0 for lobby)
    bool is_lobby = false;   // NEW: lobby flag
};
```

### Phase 2: Add plain-text parser
**File:** `src/net/server_list.cpp`

Add new parsing function:
```cpp
bool ServerListFetcher::parse_ninjam_format(const std::string& data,
                                            ServerListResult& result);
```

Parsing approach:
1. Split by newlines
2. For each line starting with `SERVER`:
   - Use simple state machine to extract quoted strings
   - Parse host:port
   - Parse BPM/BPI (set `is_lobby=true` when tempo is "lobby")
   - Parse `users/max:names` into `users`, `max_users`, `user_list`

### Phase 3: Update default URL
**File:** `src/ui/ui_state.h`

```cpp
// Before:
char server_list_url[256] = "http://ninbot.com/serverlist";

// After:
char server_list_url[256] = "http://autosong.ninjam.com/serverlist.php";
```

### Phase 4: Update request headers
**File:** `src/net/server_list.cpp`

```cpp
// Remove JSON header since we're fetching plain text
// Before:
http_.addheader("Accept: application/json");

// After:
http_.addheader("Accept: text/plain");
```

### Phase 5: Update UI to show usernames
**File:** `src/ui/ui_server_browser.cpp`

- Add column or tooltip showing usernames
- Show "Users: name1, name2, ..." when hovering or in details
- When `is_lobby == true`, display tempo as "Lobby" (avoid showing 0/0)

### Phase 6: Update poll logic
**File:** `src/ui/ui_server_browser.cpp`

Consider:
- Auto-refresh every 60 seconds while browser is open
- Manual refresh button
- "Last updated: X seconds ago" indicator

## UI Display Changes

### Current Display
```
| Server              | BPM | BPI | Users |
|---------------------|-----|-----|-------|
| ninbot.com:2052     | 95  | 16  | 6     |
```

### New Display Options

**Option A: Add usernames column**
```
| Server              | BPM | BPI | Users | Who's There                          |
|---------------------|-----|-----|-------|--------------------------------------|
| ninbot.com:2052     | 95  | 16  | 6/8   | DonPolettone, FoxIT, jhonep...       |
```

**Option B: Tooltip on hover**
- Hover over row shows: "Users: DonPolettone, FoxIT, jhonep, ninbot_, Sharonn_OzZ, Waii_D"

**Option C: Expandable row**
- Click to expand and see full user list

**Recommendation:** Option B (tooltip) - cleanest UI, doesn't clutter table

## Files to Modify

| File | Changes |
|------|---------|
| `src/ui/server_list_types.h` | Add `max_users`, `user_list`, `bpm`, `bpi`, `is_lobby` fields |
| `src/net/server_list.cpp` | Add plain-text parser, update headers |
| `src/ui/ui_state.h` | Update default URL |
| `src/ui/ui_server_browser.cpp` | Show max_users, add tooltip for usernames |

## Testing

1. Verify fetch from `autosong.ninjam.com/serverlist.php` works
2. Verify all servers parse correctly
3. Verify "lobby" servers are handled (special case)
4. Verify empty servers show correctly
5. Verify usernames display in UI

## Fallback Consideration

Keep ability to use ninbot.com JSON as fallback:
- Detect format by first character (`{` = JSON, `S` = plain text)
- Auto-switch parser based on response format
- This allows users to configure alternative list sources

## Estimated Effort

- Phase 1-2 (Parser): ~30 minutes
- Phase 3-4 (Integration): ~10 minutes
- Phase 5-6 (UI): ~20 minutes
- Testing: ~15 minutes

**Total: ~1-1.5 hours**
