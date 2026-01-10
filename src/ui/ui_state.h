#ifndef UI_STATE_H
#define UI_STATE_H

#include <array>
#include <atomic>
#include <string>
#include <vector>
#include "ui/server_list_types.h"

struct UiRemoteChannel {
    std::string name;
    int channel_index = -1;
    bool subscribed = true;
    float volume = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    float vu_left = 0.0f;
    float vu_right = 0.0f;
};

struct UiRemoteUser {
    std::string name;
    std::string address;
    bool mute = false;
    std::vector<UiRemoteChannel> channels;
};

enum class ChatMessageType {
    Message,
    PrivateMessage,
    Topic,
    Join,
    Part,
    Action,
    System
};

struct ChatMessage {
    ChatMessageType type{ChatMessageType::Message};
    std::string sender;
    std::string content;
    std::string timestamp;
};

struct UiState {
    static constexpr int kLatencyHistorySize = 16;
    static constexpr int kChatHistorySize = 100;
    // Connection
    char server_input[256] = "";
    char username_input[64] = "";
    char password_input[64] = "";
    std::string connection_error;
    std::string server_topic;
    bool connecting = false;

    // Status
    int status = -1;  // NJC_STATUS_DISCONNECTED
    float bpm = 0.0f;
    int bpi = 0;
    int beat_position = 0;
    int interval_position = 0;
    int interval_length = 0;

    // Local channel
    char local_name_input[64] = "Channel";
    int local_bitrate_index = 1;  // 64 kbps
    bool local_transmit = true;
    float local_volume = 1.0f;
    float local_pan = 0.0f;
    bool local_mute = false;
    bool local_solo = false;
    float local_vu_left = 0.0f;
    float local_vu_right = 0.0f;
    float transient_threshold = 0.12f;
    bool show_latency_guide = true;
    std::array<float, kLatencyHistorySize> latency_history{};
    int latency_history_index = 0;
    int latency_history_count = 0;

    std::array<ChatMessage, kChatHistorySize> chat_history{};
    int chat_history_index = 0;
    int chat_history_count = 0;
    char chat_input[512] = "";
    bool chat_scroll_to_bottom = false;
    bool show_chat = true;

    // Master
    float master_vu_left = 0.0f;
    float master_vu_right = 0.0f;

    // Remote users
    std::vector<UiRemoteUser> remote_users;
    bool users_dirty = false;

    // License dialog
    bool show_license_dialog = false;
    std::string license_text;

    // Public server list
    char server_list_url[256] = "http://ninbot.com/serverlist";
    std::vector<ServerListEntry> server_list;
    bool server_list_loading = false;
    std::string server_list_error;

    // Solo state
    bool any_solo_active = false;
};

// Atomic snapshot for high-frequency UI reads (no state_mutex)
struct UiAtomicSnapshot {
    std::atomic<float> bpm{0.0f};
    std::atomic<int>   bpi{0};
    std::atomic<int>   interval_position{0};
    std::atomic<int>   interval_length{0};
    std::atomic<int>   beat_position{0};
    std::atomic<float> last_transient_beat_offset{0.0f};
    std::atomic<bool>  transient_detected{false};
    std::atomic<float> transient_threshold{0.12f};

    // VU levels (audio thread writes)
    std::atomic<float> master_vu_left{0.0f};
    std::atomic<float> master_vu_right{0.0f};
    std::atomic<float> local_vu_left{0.0f};
    std::atomic<float> local_vu_right{0.0f};
};

#endif // UI_STATE_H
