/*
    JamWide Plugin - ui_command.h
    Command types for UI thread → Run thread communication
*/

#ifndef UI_COMMAND_H
#define UI_COMMAND_H

#include <string>
#include <variant>

namespace jamwide {

struct ConnectCommand {
    std::string server;
    std::string username;
    std::string password;
};

struct DisconnectCommand {
};

struct SetLocalChannelInfoCommand {
    int channel = 0;
    std::string name;
    bool set_bitrate = false;
    int bitrate = 0;
    bool set_transmit = false;
    bool transmit = false;
    bool set_srcch = false;
    int srcch = 0;  // Input bus selector (stereo pair index with bit 10 set)
};

struct SetLocalChannelMonitoringCommand {
    int channel = 0;
    bool set_volume = false;
    float volume = 1.0f;
    bool set_pan = false;
    float pan = 0.0f;
    bool set_mute = false;
    bool mute = false;
    bool set_solo = false;
    bool solo = false;
};

struct SetUserStateCommand {
    int user_index = 0;
    bool set_vol = false;
    float volume = 1.0f;
    bool set_pan = false;
    float pan = 0.0f;
    bool set_mute = false;
    bool mute = false;
};

struct SetUserChannelStateCommand {
    int user_index = 0;
    int channel_index = 0;
    bool set_sub = false;
    bool subscribed = false;
    bool set_vol = false;
    float volume = 1.0f;
    bool set_pan = false;
    float pan = 0.0f;
    bool set_mute = false;
    bool mute = false;
    bool set_solo = false;
    bool solo = false;
    bool set_outch = false;
    int outchannel = 0;  // stereo pair channel index (0, 2, 4, ..., 30)
};

struct RequestServerListCommand {
    std::string url;
};

struct SendChatCommand {
    std::string type;   // "MSG", "PRIVMSG", "TOPIC"
    std::string target; // for PRIVMSG
    std::string text;
};

struct SetEncoderFormatCommand {
    unsigned int fourcc = 0;  // NJ_ENCODER_FMT_FLAC or MAKE_NJ_FOURCC('O','G','G','v')
};

struct SetRoutingModeCommand {
    int mode = 0;  // 0=manual, 1=by-channel, 2=by-user
};

// DAW Sync commands (Phase 7 — SYNC-01, SYNC-02)
struct SyncCommand {};           // User requests sync (IDLE -> WAITING)
struct SyncCancelCommand {};     // User cancels sync (WAITING -> IDLE)
struct SyncDisableCommand {};    // User disables sync (ACTIVE -> IDLE)

using UiCommand = std::variant<
    ConnectCommand,
    DisconnectCommand,
    SetLocalChannelInfoCommand,
    SetLocalChannelMonitoringCommand,
    SetUserStateCommand,
    SetUserChannelStateCommand,
    RequestServerListCommand,
    SendChatCommand,
    SetEncoderFormatCommand,
    SetRoutingModeCommand,
    SyncCommand,
    SyncCancelCommand,
    SyncDisableCommand
>;

} // namespace jamwide

#endif // UI_COMMAND_H
