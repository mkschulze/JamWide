// CLAP Entry Point - Phase 2 Full Implementation
// NINJAM CLAP Plugin

#include <clap/clap.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "ninjam_plugin.h"
#include "core/njclient.h"
#include "threading/run_thread.h"
#include "third_party/picojson.h"

using namespace ninjam;

//------------------------------------------------------------------------------
// Forward Declarations
//------------------------------------------------------------------------------

static bool plugin_init(const clap_plugin_t* plugin);
static void plugin_destroy(const clap_plugin_t* plugin);
static bool plugin_activate(const clap_plugin_t* plugin, double sample_rate,
                            uint32_t min_frames, uint32_t max_frames);
static void plugin_deactivate(const clap_plugin_t* plugin);
static bool plugin_start_processing(const clap_plugin_t* plugin);
static void plugin_stop_processing(const clap_plugin_t* plugin);
static void plugin_reset(const clap_plugin_t* plugin);
static clap_process_status plugin_process(const clap_plugin_t* plugin,
                                          const clap_process_t* process);
static const void* plugin_get_extension(const clap_plugin_t* plugin, const char* id);
static void plugin_on_main_thread(const clap_plugin_t* plugin);

//------------------------------------------------------------------------------
// Plugin Descriptor
//------------------------------------------------------------------------------

static const char* s_features[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_UTILITY,
    CLAP_PLUGIN_FEATURE_MIXING,
    nullptr
};

static const clap_plugin_descriptor_t s_descriptor = {
    .clap_version = CLAP_VERSION,
    .id = "com.ninjam.clap-client",
    .name = "NINJAM",
    .vendor = "NINJAM",
    .url = "https://www.cockos.com/ninjam/",
    .manual_url = "https://www.cockos.com/ninjam/",
    .support_url = "https://www.cockos.com/ninjam/",
    .version = "1.0.0",
    .description = "Real-time online music collaboration",
    .features = s_features
};

//------------------------------------------------------------------------------
// Parameter IDs
//------------------------------------------------------------------------------

enum ParamId : clap_id {
    PARAM_MASTER_VOLUME = 0,
    PARAM_MASTER_MUTE = 1,
    PARAM_METRO_VOLUME = 2,
    PARAM_METRO_MUTE = 3,
    PARAM_COUNT = 4
};

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------

static NinjamPlugin* get_plugin(const clap_plugin_t* plugin) {
    return static_cast<NinjamPlugin*>(plugin->plugin_data);
}

static void process_param_events(NinjamPlugin* plugin,
                                 const clap_input_events_t* in_events) {
    if (!in_events) return;

    const uint32_t count = in_events->size(in_events);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* hdr = in_events->get(in_events, i);
        if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;

        if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(hdr);
            switch (ev->param_id) {
                case PARAM_MASTER_VOLUME:
                    plugin->param_master_volume.store(
                        static_cast<float>(ev->value), std::memory_order_relaxed);
                    break;
                case PARAM_MASTER_MUTE:
                    plugin->param_master_mute.store(
                        ev->value >= 0.5, std::memory_order_relaxed);
                    break;
                case PARAM_METRO_VOLUME:
                    plugin->param_metro_volume.store(
                        static_cast<float>(ev->value), std::memory_order_relaxed);
                    break;
                case PARAM_METRO_MUTE:
                    plugin->param_metro_mute.store(
                        ev->value >= 0.5, std::memory_order_relaxed);
                    break;
            }
        }
    }
}

//------------------------------------------------------------------------------
// Plugin Lifecycle
//------------------------------------------------------------------------------

static bool plugin_init(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);

    // Initialize UI state defaults
    snprintf(plugin->ui_state.server_input,
             sizeof(plugin->ui_state.server_input), "%s", "ninbot.com");
    snprintf(plugin->ui_state.username_input,
             sizeof(plugin->ui_state.username_input), "%s", "anonymous");

    return true;
}

static void plugin_destroy(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);

    // Ensure teardown even if host skips deactivate()
    if (plugin->client) {
        plugin_deactivate(clap_plugin);
    }

    // Clear sensitive data
    plugin->password.clear();
    memset(plugin->ui_state.password_input, 0,
           sizeof(plugin->ui_state.password_input));

    delete plugin;
    delete clap_plugin;
}

static bool plugin_activate(const clap_plugin_t* clap_plugin,
                            double sample_rate,
                            uint32_t min_frames,
                            uint32_t max_frames) {
    auto* plugin = get_plugin(clap_plugin);

    plugin->sample_rate = sample_rate;
    plugin->max_frames = max_frames;

    // Create NJClient instance
    plugin->client = std::make_unique<NJClient>();

    // Start Run thread (which sets up callbacks)
    run_thread_start(plugin);

    return true;
}

static void plugin_deactivate(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);

    if (!plugin->client) return;

    // Disconnect if connected
    {
        std::lock_guard<std::mutex> lock(plugin->state_mutex);
        int status = plugin->client->GetStatus();
        if (status >= 0 && status != NJClient::NJC_STATUS_DISCONNECTED) {
            plugin->client->Disconnect();
        }
    }

    // Stop Run thread
    run_thread_stop(plugin);

    // Destroy NJClient
    plugin->client.reset();
}

static bool plugin_start_processing(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);
    plugin->audio_active.store(true, std::memory_order_release);
    return true;
}

static void plugin_stop_processing(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);
    plugin->audio_active.store(false, std::memory_order_release);
}

static void plugin_reset(const clap_plugin_t* clap_plugin) {
    // Nothing to reset for NINJAM
}

static void plugin_on_main_thread(const clap_plugin_t* clap_plugin) {
    // Called when host requests main thread callback
}

//------------------------------------------------------------------------------
// Audio Processing
//------------------------------------------------------------------------------

static clap_process_status plugin_process(const clap_plugin_t* clap_plugin,
                                          const clap_process_t* process) {
    auto* plugin = get_plugin(clap_plugin);

    // Handle parameter events
    process_param_events(plugin, process->in_events);

    // Check if we have valid audio buffers
    if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& in_port = process->audio_inputs[0];
    const auto& out_port = process->audio_outputs[0];

    if (in_port.channel_count < 2 || out_port.channel_count < 2) {
        return CLAP_PROCESS_CONTINUE;
    }

    if (!in_port.data32 || !out_port.data32) {
        return CLAP_PROCESS_ERROR;
    }

    // Get buffer pointers
    float* in[2] = { in_port.data32[0], in_port.data32[1] };
    float* out[2] = { out_port.data32[0], out_port.data32[1] };
    uint32_t frames = process->frames_count;

    // Read transport state
    bool is_playing = false;
    bool is_seek = false;
    double cursor_pos = -1.0;

    if (process->transport) {
        is_playing = (process->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
    }

    // Sync CLAP params to NJClient atomics
    if (plugin->client) {
        plugin->client->config_mastervolume.store(
            plugin->param_master_volume.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        plugin->client->config_mastermute.store(
            plugin->param_master_mute.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        plugin->client->config_metronome.store(
            plugin->param_metro_volume.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        plugin->client->config_metronome_mute.store(
            plugin->param_metro_mute.load(std::memory_order_relaxed),
            std::memory_order_relaxed);

        // Check connection status (lock-free)
        int status = plugin->client->cached_status.load(std::memory_order_acquire);

        if (status == NJClient::NJC_STATUS_OK) {
            bool just_monitor = !is_playing;
            plugin->client->AudioProc(
                in, 2, out, 2,
                static_cast<int>(frames),
                static_cast<int>(plugin->sample_rate),
                just_monitor, is_playing, is_seek, cursor_pos
            );

            // Update VU snapshot for UI
            plugin->ui_snapshot.master_vu_left.store(
                plugin->client->GetOutputPeak(0), std::memory_order_relaxed);
            plugin->ui_snapshot.master_vu_right.store(
                plugin->client->GetOutputPeak(1), std::memory_order_relaxed);
            plugin->ui_snapshot.local_vu_left.store(
                plugin->client->GetLocalChannelPeak(0, 0), std::memory_order_relaxed);
            plugin->ui_snapshot.local_vu_right.store(
                plugin->client->GetLocalChannelPeak(0, 1), std::memory_order_relaxed);

            return CLAP_PROCESS_CONTINUE;
        }
    }

    // Not connected or no client: pass-through audio
    if (in[0] != out[0]) {
        memcpy(out[0], in[0], frames * sizeof(float));
    }
    if (in[1] != out[1]) {
        memcpy(out[1], in[1], frames * sizeof(float));
    }

    return CLAP_PROCESS_CONTINUE;
}

//------------------------------------------------------------------------------
// Audio Ports Extension
//------------------------------------------------------------------------------

static uint32_t audio_ports_count(const clap_plugin_t* plugin, bool is_input) {
    return 1;
}

static bool audio_ports_get(const clap_plugin_t* plugin, uint32_t index,
                            bool is_input, clap_audio_port_info_t* info) {
    if (index != 0) return false;

    info->id = 0;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = 0;

    if (is_input) {
        strncpy(info->name, "Audio In", sizeof(info->name));
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    } else {
        strncpy(info->name, "Audio Out", sizeof(info->name));
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    }

    return true;
}

static const clap_plugin_audio_ports_t s_audio_ports = {
    .count = audio_ports_count,
    .get = audio_ports_get
};

//------------------------------------------------------------------------------
// Parameters Extension
//------------------------------------------------------------------------------

static uint32_t params_count(const clap_plugin_t* plugin) {
    return PARAM_COUNT;
}

static bool params_get_info(const clap_plugin_t* plugin, uint32_t index,
                            clap_param_info_t* info) {
    switch (index) {
        case PARAM_MASTER_VOLUME:
            info->id = PARAM_MASTER_VOLUME;
            strncpy(info->name, "Master Volume", sizeof(info->name));
            strncpy(info->module, "Master", sizeof(info->module));
            info->min_value = 0.0;
            info->max_value = 2.0;
            info->default_value = 1.0;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            return true;

        case PARAM_MASTER_MUTE:
            info->id = PARAM_MASTER_MUTE;
            strncpy(info->name, "Master Mute", sizeof(info->name));
            strncpy(info->module, "Master", sizeof(info->module));
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.0;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            return true;

        case PARAM_METRO_VOLUME:
            info->id = PARAM_METRO_VOLUME;
            strncpy(info->name, "Metronome Volume", sizeof(info->name));
            strncpy(info->module, "Metronome", sizeof(info->module));
            info->min_value = 0.0;
            info->max_value = 2.0;
            info->default_value = 0.5;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            return true;

        case PARAM_METRO_MUTE:
            info->id = PARAM_METRO_MUTE;
            strncpy(info->name, "Metronome Mute", sizeof(info->name));
            strncpy(info->module, "Metronome", sizeof(info->module));
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.0;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            return true;

        default:
            return false;
    }
}

static bool params_get_value(const clap_plugin_t* clap_plugin,
                             clap_id param_id, double* value) {
    auto* plugin = get_plugin(clap_plugin);
    switch (param_id) {
        case PARAM_MASTER_VOLUME:
            *value = plugin->param_master_volume.load(std::memory_order_relaxed);
            return true;
        case PARAM_MASTER_MUTE:
            *value = plugin->param_master_mute.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            return true;
        case PARAM_METRO_VOLUME:
            *value = plugin->param_metro_volume.load(std::memory_order_relaxed);
            return true;
        case PARAM_METRO_MUTE:
            *value = plugin->param_metro_mute.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            return true;
        default:
            return false;
    }
}

static bool params_value_to_text(const clap_plugin_t* plugin, clap_id param_id,
                                 double value, char* display, uint32_t size) {
    switch (param_id) {
        case PARAM_MASTER_VOLUME:
        case PARAM_METRO_VOLUME:
            if (value <= 0.0) {
                snprintf(display, size, "-inf dB");
            } else {
                double db = 20.0 * log10(value);
                snprintf(display, size, "%.1f dB", db);
            }
            return true;
        case PARAM_MASTER_MUTE:
        case PARAM_METRO_MUTE:
            snprintf(display, size, "%s", value >= 0.5 ? "Muted" : "Active");
            return true;
        default:
            return false;
    }
}

static bool params_text_to_value(const clap_plugin_t* plugin, clap_id param_id,
                                 const char* display, double* value) {
    switch (param_id) {
        case PARAM_MASTER_VOLUME:
        case PARAM_METRO_VOLUME: {
            double db;
            if (sscanf(display, "%lf", &db) == 1) {
                *value = pow(10.0, db / 20.0);
                return true;
            }
            return false;
        }
        case PARAM_MASTER_MUTE:
        case PARAM_METRO_MUTE:
            if (strstr(display, "Mute") || strcmp(display, "1") == 0) {
                *value = 1.0;
                return true;
            }
            *value = 0.0;
            return true;
        default:
            return false;
    }
}

static void params_flush(const clap_plugin_t* clap_plugin,
                         const clap_input_events_t* in,
                         const clap_output_events_t* out) {
    auto* plugin = get_plugin(clap_plugin);
    process_param_events(plugin, in);
}

static const clap_plugin_params_t s_params = {
    .count = params_count,
    .get_info = params_get_info,
    .get_value = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush = params_flush
};

//------------------------------------------------------------------------------
// State Extension
//------------------------------------------------------------------------------

static bool state_save(const clap_plugin_t* clap_plugin,
                       const clap_ostream_t* stream) {
    auto* plugin = get_plugin(clap_plugin);

    std::string server;
    std::string username;
    std::string local_name;
    int local_bitrate_index = 0;
    bool local_transmit = false;

    {
        std::lock_guard<std::mutex> lock(plugin->state_mutex);
        server = plugin->server;
        username = plugin->username;
        local_name = plugin->ui_state.local_name_input;
        local_bitrate_index = plugin->ui_state.local_bitrate_index;
        local_transmit = plugin->ui_state.local_transmit;
    }

    picojson::object root;
    root["version"] = picojson::value(1.0);
    root["server"] = picojson::value(server);
    root["username"] = picojson::value(username);

    picojson::object master;
    master["volume"] = picojson::value(
        static_cast<double>(plugin->param_master_volume.load(std::memory_order_relaxed)));
    master["mute"] = picojson::value(
        plugin->param_master_mute.load(std::memory_order_relaxed));
    root["master"] = picojson::value(master);

    picojson::object metronome;
    metronome["volume"] = picojson::value(
        static_cast<double>(plugin->param_metro_volume.load(std::memory_order_relaxed)));
    metronome["mute"] = picojson::value(
        plugin->param_metro_mute.load(std::memory_order_relaxed));
    root["metronome"] = picojson::value(metronome);

    picojson::object local;
    local["name"] = picojson::value(local_name);
    local["bitrate"] = picojson::value(static_cast<double>(local_bitrate_index));
    local["transmit"] = picojson::value(local_transmit);
    root["localChannel"] = picojson::value(local);

    std::string data = picojson::value(root).serialize();
    const char* ptr = data.c_str();
    size_t remaining = data.size();
    while (remaining > 0) {
        int64_t written = stream->write(stream, ptr, remaining);
        if (written <= 0) {
            return false;
        }
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
}

static bool state_load(const clap_plugin_t* clap_plugin,
                       const clap_istream_t* stream) {
    auto* plugin = get_plugin(clap_plugin);

    // Read all data
    std::string data;
    char buffer[1024];
    while (true) {
        int64_t read_bytes = stream->read(stream, buffer, sizeof(buffer));
        if (read_bytes <= 0) break;
        data.append(buffer, static_cast<size_t>(read_bytes));
    }

    if (data.empty()) return false;

    picojson::value root_val;
    std::string err = picojson::parse(root_val, data);
    if (!err.empty() || !root_val.is<picojson::object>()) return false;

    const auto& root = root_val.get<picojson::object>();

    // Check version
    auto version_it = root.find("version");
    if (version_it != root.end() && version_it->second.is<double>()) {
        int version = static_cast<int>(version_it->second.get<double>());
        if (version > 1) return false;
    }

    bool has_server = false;
    std::string server;
    bool has_username = false;
    std::string username;
    bool has_master_volume = false;
    float master_volume = 0.0f;
    bool has_master_mute = false;
    bool master_mute = false;
    bool has_metro_volume = false;
    float metro_volume = 0.0f;
    bool has_metro_mute = false;
    bool metro_mute = false;
    bool has_local_name = false;
    std::string local_name;
    bool has_local_bitrate = false;
    int local_bitrate_index = 0;
    bool has_local_transmit = false;
    bool local_transmit = false;

    // Load server/username
    auto server_it = root.find("server");
    if (server_it != root.end() && server_it->second.is<std::string>()) {
        server = server_it->second.get<std::string>();
        has_server = true;
    }

    auto username_it = root.find("username");
    if (username_it != root.end() && username_it->second.is<std::string>()) {
        username = username_it->second.get<std::string>();
        has_username = true;
    }

    // Load master params
    auto master_it = root.find("master");
    if (master_it != root.end() && master_it->second.is<picojson::object>()) {
        const auto& master = master_it->second.get<picojson::object>();
        auto vol_it = master.find("volume");
        if (vol_it != master.end() && vol_it->second.is<double>()) {
            master_volume = static_cast<float>(vol_it->second.get<double>());
            has_master_volume = true;
        }
        auto mute_it = master.find("mute");
        if (mute_it != master.end() && mute_it->second.is<bool>()) {
            master_mute = mute_it->second.get<bool>();
            has_master_mute = true;
        }
    }

    // Load metronome params
    auto metro_it = root.find("metronome");
    if (metro_it != root.end() && metro_it->second.is<picojson::object>()) {
        const auto& metro = metro_it->second.get<picojson::object>();
        auto vol_it = metro.find("volume");
        if (vol_it != metro.end() && vol_it->second.is<double>()) {
            metro_volume = static_cast<float>(vol_it->second.get<double>());
            has_metro_volume = true;
        }
        auto mute_it = metro.find("mute");
        if (mute_it != metro.end() && mute_it->second.is<bool>()) {
            metro_mute = mute_it->second.get<bool>();
            has_metro_mute = true;
        }
    }

    // Load local channel
    auto local_it = root.find("localChannel");
    if (local_it != root.end() && local_it->second.is<picojson::object>()) {
        const auto& local = local_it->second.get<picojson::object>();
        auto name_it = local.find("name");
        if (name_it != local.end() && name_it->second.is<std::string>()) {
            local_name = name_it->second.get<std::string>();
            has_local_name = true;
        }
        auto bitrate_it = local.find("bitrate");
        if (bitrate_it != local.end() && bitrate_it->second.is<double>()) {
            local_bitrate_index = static_cast<int>(bitrate_it->second.get<double>());
            has_local_bitrate = true;
        }
        auto transmit_it = local.find("transmit");
        if (transmit_it != local.end() && transmit_it->second.is<bool>()) {
            local_transmit = transmit_it->second.get<bool>();
            has_local_transmit = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock(plugin->state_mutex);
        if (has_server) {
            plugin->server = server;
            snprintf(plugin->ui_state.server_input,
                     sizeof(plugin->ui_state.server_input), "%s", server.c_str());
        }
        if (has_username) {
            plugin->username = username;
            snprintf(plugin->ui_state.username_input,
                     sizeof(plugin->ui_state.username_input), "%s", username.c_str());
        }
        if (has_master_volume) {
            plugin->param_master_volume.store(master_volume, std::memory_order_relaxed);
        }
        if (has_master_mute) {
            plugin->param_master_mute.store(master_mute, std::memory_order_relaxed);
        }
        if (has_metro_volume) {
            plugin->param_metro_volume.store(metro_volume, std::memory_order_relaxed);
        }
        if (has_metro_mute) {
            plugin->param_metro_mute.store(metro_mute, std::memory_order_relaxed);
        }
        if (has_local_name) {
            snprintf(plugin->ui_state.local_name_input,
                     sizeof(plugin->ui_state.local_name_input),
                     "%s", local_name.c_str());
        }
        if (has_local_bitrate) {
            plugin->ui_state.local_bitrate_index = local_bitrate_index;
        }
        if (has_local_transmit) {
            plugin->ui_state.local_transmit = local_transmit;
        }
    }

    return true;
}

static const clap_plugin_state_t s_state = {
    .save = state_save,
    .load = state_load
};

//------------------------------------------------------------------------------
// Extension Query
//------------------------------------------------------------------------------

static const void* plugin_get_extension(const clap_plugin_t* plugin,
                                        const char* id) {
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports;
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) return &s_params;
    if (strcmp(id, CLAP_EXT_STATE) == 0) return &s_state;
    // GUI extension will be added in Phase 3
    return nullptr;
}

//------------------------------------------------------------------------------
// Plugin Instance Template
//------------------------------------------------------------------------------

static const clap_plugin_t s_plugin_template = {
    .desc = &s_descriptor,
    .plugin_data = nullptr,
    .init = plugin_init,
    .destroy = plugin_destroy,
    .activate = plugin_activate,
    .deactivate = plugin_deactivate,
    .start_processing = plugin_start_processing,
    .stop_processing = plugin_stop_processing,
    .reset = plugin_reset,
    .process = plugin_process,
    .get_extension = plugin_get_extension,
    .on_main_thread = plugin_on_main_thread
};

//------------------------------------------------------------------------------
// Factory
//------------------------------------------------------------------------------

static uint32_t factory_get_plugin_count(const clap_plugin_factory_t* factory) {
    return 1;
}

static const clap_plugin_descriptor_t* factory_get_plugin_descriptor(
    const clap_plugin_factory_t* factory, uint32_t index) {
    return index == 0 ? &s_descriptor : nullptr;
}

static const clap_plugin_t* factory_create_plugin(
    const clap_plugin_factory_t* factory,
    const clap_host_t* host,
    const char* plugin_id) {

    if (!clap_version_is_compatible(host->clap_version)) return nullptr;
    if (strcmp(plugin_id, s_descriptor.id) != 0) return nullptr;

    // Allocate plugin instance
    auto* clap_plugin = new clap_plugin_t(s_plugin_template);
    auto* plugin = new NinjamPlugin();

    plugin->clap_plugin = clap_plugin;
    plugin->host = host;
    clap_plugin->plugin_data = plugin;

    return clap_plugin;
}

static const clap_plugin_factory_t s_factory = {
    .get_plugin_count = factory_get_plugin_count,
    .get_plugin_descriptor = factory_get_plugin_descriptor,
    .create_plugin = factory_create_plugin
};

//------------------------------------------------------------------------------
// Entry Point
//------------------------------------------------------------------------------

static bool entry_init(const char* path) {
    return true;
}

static void entry_deinit(void) {
}

static const void* entry_get_factory(const char* factory_id) {
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &s_factory;
    return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = entry_init,
    .deinit = entry_deinit,
    .get_factory = entry_get_factory
};
