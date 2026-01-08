/*
    NINJAM CLAP Plugin - ui_command.h
    Command types for UI thread → Run thread communication
*/

#ifndef UI_COMMAND_H
#define UI_COMMAND_H

#include <string>
#include <variant>

namespace ninjam {

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
};

struct RequestServerListCommand {
    std::string url;
};

using UiCommand = std::variant<
    ConnectCommand,
    DisconnectCommand,
    SetLocalChannelInfoCommand,
    SetLocalChannelMonitoringCommand,
    SetUserStateCommand,
    SetUserChannelStateCommand,
    RequestServerListCommand
>;

} // namespace ninjam

#endif // UI_COMMAND_H
