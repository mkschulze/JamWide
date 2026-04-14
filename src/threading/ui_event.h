/*
    JamWide Plugin - ui_event.h
    Event types for Run thread → UI thread communication
    
    Copyright (C) 2024 JamWide Contributors
    Licensed under GPLv2+
*/

#ifndef UI_EVENT_H
#define UI_EVENT_H

#include <string>
#include <variant>
#include <vector>
#include "ui/server_list_types.h"

namespace jamwide {

/**
 * Chat message received from server or other users.
 */
struct ChatMessageEvent {
    std::string type;   // "MSG", "PRIVMSG", "JOIN", "PART", "TOPIC", etc.
    std::string user;   // Username (empty for server messages)
    std::string text;   // Message content
};

/**
 * Connection status changed.
 */
struct StatusChangedEvent {
    int status;             // NJC_STATUS_* value
    std::string error_msg;  // Error description (if any)
};

/**
 * User/channel information changed.
 * Signals UI to refresh the user and channel lists.
 */
struct UserInfoChangedEvent {
    // No fields - just a signal to refresh
};

/**
 * Server topic changed.
 */
struct TopicChangedEvent {
    std::string topic;
};

/**
 * Public server list update.
 */
struct ServerListEvent {
    std::vector<ServerListEntry> servers;
    std::string error;
};

// DAW Sync reason codes (Phase 7 — addresses review consensus #2: event model lacks reason payloads)
enum class SyncReason : int {
    UserRequest = 0,        // User clicked sync button
    UserCancel = 1,         // User cancelled while WAITING
    UserDisable = 2,        // User disabled while ACTIVE
    TransportStarted = 3,   // DAW transport started (WAITING -> ACTIVE)
    ServerBpmChanged = 4,   // Server BPM changed, auto-disabled (D-05)
    TransportSeek = 5,      // Transport seek/loop detected, auto-disabled
    HostTimingUnavailable = 6  // Host does not provide PPQ data
};

/**
 * Server BPM changed (detected by run thread).
 */
struct BpmChangedEvent {
    float oldBpm;
    float newBpm;
};

/**
 * Server BPI changed (detected by run thread).
 */
struct BpiChangedEvent {
    int oldBpi;
    int newBpi;
};

/**
 * Sync state machine transitioned.
 * Includes reason payload for UI context (why the state changed).
 */
struct SyncStateChangedEvent {
    int newState;       // 0=IDLE, 1=WAITING, 2=ACTIVE
    SyncReason reason;  // Why the state changed
};

// Prelisten connection lifecycle status (Phase 14.1 — BROWSE-01)
// Addresses review: single bool is too weak for connection state tracking.
enum class PrelistenStatus : int {
    Connecting = 0,   // Connect initiated, waiting for auth
    Connected = 1,    // NJC_STATUS_OK received, audio flowing
    Stopped = 2,      // Disconnected (user action or error)
    Failed = 3        // Connection failed (unreachable, auth error)
};

struct PrelistenStateEvent {
    PrelistenStatus status = PrelistenStatus::Stopped;
    std::string host;         // Server host for UI reconciliation
    int port = 0;             // Server port for UI reconciliation
    std::string server_name;  // Display name (from server list or host)
};

/**
 * Variant type for all UI events.
 *
 * Note: License handling uses a dedicated atomic slot (license_pending,
 * license_response, license_text, license_cv) rather than the event queue,
 * to support blocking wait in the Run thread callback.
 */
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

} // namespace jamwide

#endif // UI_EVENT_H
