#ifndef UI_STATE_H
#define UI_STATE_H

#include <atomic>
#include <string>
#include <vector>

struct RemoteChannel {
    std::string name;
    bool subscribed = true;
    float volume = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    float vu_left = 0.0f;
    float vu_right = 0.0f;
};

struct RemoteUser {
    std::string name;
    std::string address;
    bool mute = false;
    std::vector<RemoteChannel> channels;
};

struct UiState {
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

    // Master
    float master_vu_left = 0.0f;
    float master_vu_right = 0.0f;

    // Remote users
    std::vector<RemoteUser> remote_users;
    bool users_dirty = false;

    // License dialog
    bool show_license_dialog = false;
    std::string license_text;

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

    // VU levels (audio thread writes)
    std::atomic<float> master_vu_left{0.0f};
    std::atomic<float> master_vu_right{0.0f};
    std::atomic<float> local_vu_left{0.0f};
    std::atomic<float> local_vu_right{0.0f};
};

#endif // UI_STATE_H
