/*
    NINJAM CLAP Plugin - ui_event.h
    Event types for Run thread → UI thread communication
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#ifndef UI_EVENT_H
#define UI_EVENT_H

#include <string>
#include <variant>
#include <vector>
#include "ui/server_list_types.h"

namespace ninjam {

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
    ServerListEvent
>;

} // namespace ninjam

#endif // UI_EVENT_H
