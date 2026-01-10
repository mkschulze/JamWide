# Chat Room Implementation Plan

## Overview

Add a real-time chat room to the NINJAM CLAP plugin, allowing users to communicate with other users on the same server. This is a core NINJAM feature used for coordination, greetings, and musical discussion during jam sessions.

## Reference Implementation Analysis

### ReaNINJAM Chat Architecture (`/Users/cell/dev/ninjam/jmde/fx/reaninjam/chat.cpp`)

The existing NINJAM clients implement chat through:

1. **NJClient API** (already in our codebase):
   - `ChatMessage_Send(parm1, parm2, ...)` - Send messages to server
   - `ChatMessage_Callback` - Function pointer for receiving messages
   - `ChatMessage_User` - User data for callback

2. **Message Types** (`mpb.h` MESSAGE_CHAT_MESSAGE = 0xC0):
   ```
   Client → Server:
   - MSG <text>           - Broadcast message to all
   - PRIVMSG <user> <text> - Private message to specific user
   - TOPIC <text>         - Set server topic (if permitted)
   
   Server → Client:
   - MSG <username> <text> - Message from user
   - PRIVMSG <username> <text> - Private message received
   - TOPIC <username> <text> - Topic changed
   - JOIN <username>       - User joined
   - PART <username>       - User left
   ```

3. **Callback Pattern** (from ReaNINJAM):
   ```cpp
   void chatmsg_cb(void *userData, NJClient *inst, const char **parms, int nparms) {
       if (!strcmp(parms[0], "MSG"))      // Regular message
       if (!strcmp(parms[0], "PRIVMSG"))  // Private message
       if (!strcmp(parms[0], "TOPIC"))    // Topic change
       if (!strcmp(parms[0], "JOIN"))     // User joined
       if (!strcmp(parms[0], "PART"))     // User left
   }
   ```

4. **Special Commands**:
   - `/me <action>` - Emote (displays as "* username action")
   - `/topic <text>` - Set topic
   - `/msg <user> <text>` - Private message

---

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Location | New collapsible section | Keep UI organized, chat is optional |
| Thread safety | Queue incoming messages | Callback runs on network thread |
| History | Ring buffer, ~100 messages | Memory bounded, scrollable |
| Commands | Support /me, basic parsing | Match NINJAM conventions |
| Private messages | Display inline with prefix | Simple, no separate UI |

---

## Architecture

### Data Flow

```
┌─────────────────┐         ┌───────────────────┐         ┌─────────────────┐
│  Network Thread │ ──────► │  ChatMessage      │ ──────► │  UI Thread      │
│  (NJClient)     │ callback│  Queue (lock-free)│  poll   │  (ImGui render) │
└─────────────────┘         └───────────────────┘         └─────────────────┘
                                                                   │
                                                                   ▼
┌─────────────────┐         ┌───────────────────┐         ┌─────────────────┐
│  User Input     │ ──────► │  ChatMessage_Send │ ──────► │  Server         │
│  (ImGui text)   │  direct │  (via NJClient)   │         │                 │
└─────────────────┘         └───────────────────┘         └─────────────────┘
```

### Threading Model

1. **Incoming messages**: NJClient callback (network thread) → Queue → UI poll
2. **Outgoing messages**: UI thread → UiCommand → Run thread → NJClient::ChatMessage_Send
3. **History buffer**: Only accessed from UI thread (after dequeue)

---

## Data Structures

### ChatMessage Struct
```cpp
struct ChatMessage {
    enum Type { MSG, PRIVMSG, TOPIC, JOIN, PART, SYSTEM };
    Type type;
    std::string sender;     // Username or empty for system
    std::string content;    // Message text
    std::string timestamp;  // HH:MM format
};
```

### UiState Additions
```cpp
// Chat state
static constexpr int kChatHistorySize = 100;
std::vector<ChatMessage> chat_history;  // Ring buffer
char chat_input[512] = "";              // Input field
bool chat_scroll_to_bottom = false;     // Auto-scroll flag
bool show_chat = true;                  // Visibility toggle
// Reuse UiState::server_topic for topic display
```

### Lock-Free Queue
```cpp
// In NinjamPlugin
SpscRing<ChatMessage, 64> chat_queue;  // Network → UI (reuse existing lock-free ring)
```

---

## File Changes

| File | Change | Description |
|------|--------|-------------|
| `src/ui/ui_state.h` | Modify | Add ChatMessage struct, chat state to UiState |
| `src/ui/ui_chat.h` | **New** | Header for chat widget |
| `src/ui/ui_chat.cpp` | **New** | Chat UI implementation (~200 lines) |
| `src/ui/ui_main.cpp` | Modify | Poll queue, render chat (callback already set in run thread) |
| `src/plugin/ninjam_plugin.h` | Modify | Add chat_queue |
| `src/threading/run_thread.cpp` | Modify | Set up ChatMessage_Callback |
| `src/threading/ui_command.h` | Modify | Add SendChatCommand variant |
| `CMakeLists.txt` | Modify | Add new source files |

---

## Implementation Steps

### Step 1: Data Structures
- Define `ChatMessage` struct
- Add chat state to `UiState`
- Add `chat_queue` to `NinjamPlugin`

### Step 2: Callback Registration
- Extend existing `setup_callbacks()` in `run_thread.cpp` to wire chat callback
- Callback parses message type and pushes formatted `ChatMessage` to `chat_queue`
- Clear callback on disconnect (if needed)

### Step 3: Basic UI Widget
- Create `ui_chat.h` / `ui_chat.cpp`
- Collapsible header "Chat"
- Scrollable text region for history
- Input field with Enter to send

### Step 4: Message Rendering
- Poll `chat_queue` each frame
- Add to history vector
- Format based on message type:
  - `<username> message`
  - `* username action` (for /me)
  - `*** user has joined/left`
  - `[PM from user] message`

### Step 5: Sending Messages
- Parse input for commands (/me, /topic, /msg)
- Send `SendChatCommand` via UiCommand to run thread
- Run thread calls `ChatMessage_Send` under `client_mutex`
- Clear input field

### Step 6: Polish
- Add timestamps
- Topic display in header
- Auto-scroll to bottom on new messages
- Color coding (system messages, PMs, etc.)
- Handle long messages (word wrap)

---

## UI Mockup

```
┌─────────────────────────────────────────────────────────────────────┐
│ ▼ Chat                                              Topic: Jam! 🎸 │
├─────────────────────────────────────────────────────────────────────┤
│ ┌─────────────────────────────────────────────────────────────────┐ │
│ │ 14:32 *** player1 has joined the server                        │ │
│ │ 14:33 <player1> Hey everyone!                                   │ │
│ │ 14:33 <you> Welcome! We're in G minor                           │ │
│ │ 14:34 * player1 starts warming up                               │ │
│ │ 14:35 <player2> Nice groove!                                    │ │
│ │                                                                 │ │
│ └─────────────────────────────────────────────────────────────────┘ │
│ ┌─────────────────────────────────────────────────────────────┬───┐ │
│ │ Type message...                                             │Send│ │
│ └─────────────────────────────────────────────────────────────┴───┘ │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Message Type Formatting

| Type | Format | Color |
|------|--------|-------|
| MSG | `HH:MM <user> text` | White |
| PRIVMSG | `HH:MM [PM from user] text` | Cyan |
| TOPIC | `HH:MM *** user sets topic: text` | Yellow |
| JOIN | `HH:MM *** user has joined` | Green |
| PART | `HH:MM *** user has left` | Gray |
| /me | `HH:MM * user action` | Magenta |
| Error | `*** error message` | Red |

---

## Command Parsing

```cpp
void send_chat_message(NJClient* client, const char* input) {
    if (input[0] == '/') {
        if (strncmp(input, "/me ", 4) == 0) {
            // Send as MSG but server will relay with /me intact
            client->ChatMessage_Send("MSG", input);
        }
        else if (strncmp(input, "/topic ", 7) == 0) {
            client->ChatMessage_Send("TOPIC", input + 7);
        }
        else if (strncmp(input, "/msg ", 5) == 0) {
            // Parse: /msg username message
            const char* rest = input + 5;
            // Extract username and message...
            client->ChatMessage_Send("PRIVMSG", username, message);
        }
        else {
            // Unknown command - send as regular message
            client->ChatMessage_Send("MSG", input);
        }
    } else {
        client->ChatMessage_Send("MSG", input);
    }
}
```

**Threading note:** Use a dedicated `SendChatCommand` processed by the run thread to align with the existing NJClient serialization model.

---

## SendChatCommand (UiCommand Variant)

Add a concrete command variant to the existing UiCommand union:

```cpp
struct SendChatCommand {
    std::string type;   // "MSG", "PRIVMSG", "TOPIC", "ADMIN"
    std::string target; // for PRIVMSG, empty otherwise
    std::string text;   // message body
};
```

**Files touched:**
- `src/threading/ui_command.h` (define variant)
- `src/threading/run_thread.cpp` (handle variant and call `ChatMessage_Send`)
- `src/ui/ui_chat.cpp` (emit SendChatCommand from UI)

---

## ReaNINJAM Behavioral Parity

Mirror the ReaNINJAM callback formatting before UI display:

- `/me` is detected in incoming MSG and rendered as `* user action`
- `JOIN/PART` become `*** user has joined/left the server`
- `TOPIC` becomes `*** user sets topic: ...` and updates `ui_state.server_topic`
- `PRIVMSG` is displayed with `*user*` style (or `[PM from user]`)

This formatting happens in the callback (network thread) before enqueuing, so the UI only renders formatted lines.

---

## Estimated Effort

| Step | Time | Notes |
|------|------|-------|
| Step 1 | 30 min | Data structures |
| Step 2 | 45 min | Callback setup, threading |
| Step 3 | 1.5 hr | Basic UI widget |
| Step 4 | 1 hr | Message rendering & formatting |
| Step 5 | 45 min | Input parsing & sending |
| Step 6 | 1 hr | Polish & testing |

**Total: ~5.5 hours**

---

## Testing Checklist

- [ ] Chat section appears when connected
- [ ] Incoming messages display correctly
- [ ] Outgoing messages sent and echoed
- [ ] JOIN/PART notifications appear
- [ ] Topic displayed and updated
- [ ] /me command works
- [ ] Private messages displayed
- [ ] Auto-scroll on new messages
- [ ] No crashes on disconnect
- [ ] No memory leaks (ring buffer bounded)
- [ ] Thread-safe (no races between callback and UI)

---

## Future Enhancements (Out of Scope)

- [ ] Notification sound on new message
- [ ] Mute/ignore specific users
- [ ] Clickable usernames
- [ ] Message history persistence
- [ ] Emoji support
- [ ] Markdown/formatting
