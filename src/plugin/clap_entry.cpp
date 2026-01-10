// CLAP Entry Point - Phase 2 Full Implementation
// NINJAM CLAP Plugin

#include <clap/clap.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <mutex>

#include "ninjam_plugin.h"
#include "core/njclient.h"
#include "debug/logging.h"
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
static void gui_destroy(const clap_plugin_t* clap_plugin);

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

struct PluginInstance {
    std::shared_ptr<NinjamPlugin> plugin;
};

static PluginInstance* get_instance(const clap_plugin_t* plugin) {
    return static_cast<PluginInstance*>(plugin->plugin_data);
}

static NinjamPlugin* get_plugin(const clap_plugin_t* plugin) {
    auto* instance = get_instance(plugin);
    return instance ? instance->plugin.get() : nullptr;
}

static std::shared_ptr<NinjamPlugin> get_plugin_shared(
        const clap_plugin_t* plugin) {
    auto* instance = get_instance(plugin);
    return instance ? instance->plugin : nullptr;
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
    if (!plugin) return false;

    // Initialize UI state defaults
    snprintf(plugin->ui_state.server_input,
             sizeof(plugin->ui_state.server_input), "%s", "ninbot.com");
    snprintf(plugin->ui_state.username_input,
             sizeof(plugin->ui_state.username_input), "%s", "anonymous");
#ifdef NINJAM_DEV_BUILD
    const char* serialize_env = std::getenv("NINJAM_CLAP_SERIALIZE_AUDIOPROC");
    plugin->serialize_audio_proc = (serialize_env && *serialize_env &&
                                    std::strcmp(serialize_env, "0") != 0);
    if (plugin->serialize_audio_proc) {
        NLOG("[Init] AudioProc serialization enabled (NINJAM_CLAP_SERIALIZE_AUDIOPROC)\n");
    }
#else
    plugin->serialize_audio_proc = false;
#endif

    return true;
}

static void plugin_destroy(const clap_plugin_t* clap_plugin) {
    auto* instance = get_instance(clap_plugin);
    if (!instance) {
        delete clap_plugin;
        return;
    }

    auto plugin = instance->plugin;
    if (!plugin) {
        delete instance;
        delete clap_plugin;
        return;
    }

    // Ensure teardown even if host skips deactivate()
    if (plugin->client) {
        plugin_deactivate(clap_plugin);
    }

    if (plugin->gui_created || plugin->gui_context) {
        gui_destroy(clap_plugin);
    }

    // Clear sensitive data
    plugin->password.clear();
    memset(plugin->ui_state.password_input, 0,
           sizeof(plugin->ui_state.password_input));

    instance->plugin.reset();
    delete instance;
    delete clap_plugin;
}

static bool plugin_activate(const clap_plugin_t* clap_plugin,
                            double sample_rate,
                            uint32_t min_frames,
                            uint32_t max_frames) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return false;

    plugin->sample_rate = sample_rate;
    plugin->max_frames = max_frames;

    // Create NJClient instance
    {
        std::lock_guard<std::mutex> client_lock(plugin->client_mutex);
        plugin->client = std::make_unique<NJClient>();
    }

    // Start Run thread (which sets up callbacks)
    auto plugin_shared = get_plugin_shared(clap_plugin);
    if (!plugin_shared) return false;
    run_thread_start(plugin, std::move(plugin_shared));

    return true;
}

static void plugin_deactivate(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return;

    // Stop Run thread
    run_thread_stop(plugin);

    // Disconnect and destroy NJClient
    {
        std::lock_guard<std::mutex> client_lock(plugin->client_mutex);
        if (plugin->client) {
            plugin->client->Disconnect();
            plugin->client.reset();
        }
    }
}

static bool plugin_start_processing(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return false;
    plugin->audio_active.store(true, std::memory_order_release);
    return true;
}

static void plugin_stop_processing(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return;
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
    if (!plugin) return CLAP_PROCESS_ERROR;

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
    std::unique_lock<std::mutex> client_lock;
    NJClient* client = plugin->client.get();
    if (plugin->serialize_audio_proc) {
        client_lock = std::unique_lock<std::mutex>(plugin->client_mutex);
        client = plugin->client.get();
    }

    if (client) {
        client->config_mastervolume.store(
            plugin->param_master_volume.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        client->config_mastermute.store(
            plugin->param_master_mute.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        client->config_metronome.store(
            plugin->param_metro_volume.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        client->config_metronome_mute.store(
            plugin->param_metro_mute.load(std::memory_order_relaxed),
            std::memory_order_relaxed);

        // Check connection status (lock-free)
        int status = client->cached_status.load(std::memory_order_acquire);

        if (status == NJClient::NJC_STATUS_OK) {
            if (is_playing && plugin->sample_rate > 0.0) {
                const float threshold =
                    plugin->ui_snapshot.transient_threshold.load(
                        std::memory_order_relaxed);

                if (threshold > 0.0f) {
                    constexpr float kReleaseCoeff = 0.985f;
                    constexpr float kHysteresisRatio = 0.6f;
                    constexpr float kEdgeRatio = 0.7f;
                    constexpr float kDriftSnapThreshold = 0.08f;
                    constexpr float kDriftTauMs = 120.0f;
                    constexpr float kMinGapMs = 40.0f;

                    const int min_gap_samples =
                        static_cast<int>(plugin->sample_rate * kMinGapMs / 1000.0);

                    const float bpm = plugin->ui_snapshot.bpm.load(
                        std::memory_order_relaxed);
                    const int bpi = plugin->ui_snapshot.bpi.load(
                        std::memory_order_relaxed);
                    const int pos = plugin->ui_snapshot.interval_position.load(
                        std::memory_order_relaxed);
                    const int len = plugin->ui_snapshot.interval_length.load(
                        std::memory_order_relaxed);

                    if (bpm > 1.0f) {
                        plugin->transient.samples_per_beat =
                            (plugin->sample_rate * 60.0) / bpm;
                    }

                    if (len > 0 && bpi > 0) {
                        const double interval_phase =
                            static_cast<double>(pos) / static_cast<double>(len);
                        double snapshot_phase = std::fmod(interval_phase * bpi, 1.0);

                        auto wrap = [](double x) {
                            while (x > 0.5) x -= 1.0;
                            while (x < -0.5) x += 1.0;
                            return x;
                        };

                        const double drift =
                            wrap(snapshot_phase - plugin->transient.beat_phase);
                        if (std::fabs(drift) > kDriftSnapThreshold) {
                            plugin->transient.beat_phase = snapshot_phase;
                        } else {
                            const double block_ms =
                                (static_cast<double>(frames) * 1000.0) /
                                plugin->sample_rate;
                            const double correction =
                                1.0 - std::exp(-block_ms / kDriftTauMs);
                            plugin->transient.beat_phase += drift * correction;
                        }
                    }

                    const double samples_per_beat =
                        plugin->transient.samples_per_beat;
                    const double phase_per_sample =
                        samples_per_beat > 0.0 ? (1.0 / samples_per_beat) : 0.0;

                    for (uint32_t i = 0; i < frames; ++i) {
                        const float mono = std::max(
                            std::fabs(in[0][i]), std::fabs(in[1][i]));

                        const float prev_env = plugin->transient.env;
                        plugin->transient.env = std::max(
                            mono, plugin->transient.env * kReleaseCoeff);

                        if (plugin->transient.gate_open &&
                            plugin->transient.env > threshold &&
                            prev_env < threshold * kEdgeRatio &&
                            plugin->transient.samples_since_trigger > min_gap_samples) {
                            const float offset =
                                static_cast<float>(plugin->transient.beat_phase - 0.5);
                            plugin->ui_snapshot.last_transient_beat_offset.store(
                                offset, std::memory_order_relaxed);
                            plugin->ui_snapshot.transient_detected.store(
                                true, std::memory_order_release);

                            plugin->transient.gate_open = false;
                            plugin->transient.samples_since_trigger = 0;
                        }

                        if (!plugin->transient.gate_open &&
                            plugin->transient.env < threshold * kHysteresisRatio) {
                            plugin->transient.gate_open = true;
                        }

                        plugin->transient.beat_phase += phase_per_sample;
                        if (plugin->transient.beat_phase >= 1.0) {
                            plugin->transient.beat_phase -= 1.0;
                        } else if (plugin->transient.beat_phase < 0.0) {
                            plugin->transient.beat_phase += 1.0;
                        }

                        plugin->transient.samples_since_trigger++;
                    }
                }
            }

            bool just_monitor = !is_playing;
            client->AudioProc(
                in, 2, out, 2,
                static_cast<int>(frames),
                static_cast<int>(plugin->sample_rate),
                just_monitor, is_playing, is_seek, cursor_pos
            );

            // Update VU snapshot for UI
            plugin->ui_snapshot.master_vu_left.store(
                client->GetOutputPeak(0), std::memory_order_relaxed);
            plugin->ui_snapshot.master_vu_right.store(
                client->GetOutputPeak(1), std::memory_order_relaxed);
            plugin->ui_snapshot.local_vu_left.store(
                client->GetLocalChannelPeak(0, 0), std::memory_order_relaxed);
            plugin->ui_snapshot.local_vu_right.store(
                client->GetLocalChannelPeak(0, 1), std::memory_order_relaxed);

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
// GUI Extension
//------------------------------------------------------------------------------

#include "platform/gui_context.h"

static bool gui_is_api_supported(const clap_plugin_t* plugin,
                                 const char* api,
                                 bool is_floating) {
#ifdef _WIN32
    return strcmp(api, CLAP_WINDOW_API_WIN32) == 0 && !is_floating;
#elif __APPLE__
    return strcmp(api, CLAP_WINDOW_API_COCOA) == 0 && !is_floating;
#else
    return false;
#endif
}

static bool gui_get_preferred_api(const clap_plugin_t* plugin,
                                  const char** api,
                                  bool* is_floating) {
#ifdef _WIN32
    *api = CLAP_WINDOW_API_WIN32;
#elif __APPLE__
    *api = CLAP_WINDOW_API_COCOA;
#endif
    *is_floating = false;
    return true;
}

static bool gui_create(const clap_plugin_t* clap_plugin,
                       const char* api,
                       bool is_floating) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return false;
    auto plugin_shared = get_plugin_shared(clap_plugin);
    if (!plugin_shared) return false;

    if (plugin->gui_created) return true;

#ifdef _WIN32
    if (strcmp(api, CLAP_WINDOW_API_WIN32) != 0) return false;
    plugin->gui_context = create_gui_context_win32(plugin_shared);
#elif __APPLE__
    if (strcmp(api, CLAP_WINDOW_API_COCOA) != 0) return false;
    plugin->gui_context = create_gui_context_macos(plugin_shared);
#else
    return false;
#endif

    if (!plugin->gui_context) return false;

    plugin->gui_created = true;
    return true;
}

static void gui_destroy(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return;

    if (plugin->gui_context) {
        delete plugin->gui_context;
        plugin->gui_context = nullptr;
    }

    plugin->gui_created = false;
    plugin->gui_visible = false;
}

static bool gui_set_scale(const clap_plugin_t* clap_plugin, double scale) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return false;

    if (plugin->gui_context) {
        plugin->gui_context->set_scale(scale);
    }
    return true;
}

static bool gui_get_size(const clap_plugin_t* clap_plugin,
                         uint32_t* width,
                         uint32_t* height) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return false;

    *width = plugin->gui_width;
    *height = plugin->gui_height;
    return true;
}

static bool gui_can_resize(const clap_plugin_t* plugin) {
    return true;
}

static bool gui_get_resize_hints(const clap_plugin_t* plugin,
                                 clap_gui_resize_hints_t* hints) {
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = false;
    hints->aspect_ratio_width = 0;
    hints->aspect_ratio_height = 0;
    return true;
}

static bool gui_adjust_size(const clap_plugin_t* plugin,
                            uint32_t* width,
                            uint32_t* height) {
    // Enforce minimum size
    if (*width < 400) *width = 400;
    if (*height < 300) *height = 300;
    return true;
}

static bool gui_set_size(const clap_plugin_t* clap_plugin,
                         uint32_t width,
                         uint32_t height) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return false;

    plugin->gui_width = width;
    plugin->gui_height = height;

    if (plugin->gui_context) {
        plugin->gui_context->set_size(width, height);
    }

    return true;
}

static bool gui_set_parent(const clap_plugin_t* clap_plugin,
                           const clap_window_t* window) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return false;

    if (!plugin->gui_context) return false;

#ifdef _WIN32
    return plugin->gui_context->set_parent(window->win32);
#elif __APPLE__
    return plugin->gui_context->set_parent(window->cocoa);
#else
    return false;
#endif
}

static bool gui_set_transient(const clap_plugin_t* plugin,
                              const clap_window_t* window) {
    return false;  // Not supported for embedded GUI
}

static void gui_suggest_title(const clap_plugin_t* plugin, const char* title) {
    // Ignored for embedded GUI
}

static bool gui_show(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return false;

    if (!plugin->gui_context) return false;

    plugin->gui_context->show();
    plugin->gui_visible = true;
    return true;
}

static bool gui_hide(const clap_plugin_t* clap_plugin) {
    auto* plugin = get_plugin(clap_plugin);
    if (!plugin) return false;

    if (!plugin->gui_context) return false;

    plugin->gui_context->hide();
    plugin->gui_visible = false;
    return true;
}

static const clap_plugin_gui_t s_gui = {
    .is_api_supported = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create = gui_create,
    .destroy = gui_destroy,
    .set_scale = gui_set_scale,
    .get_size = gui_get_size,
    .can_resize = gui_can_resize,
    .get_resize_hints = gui_get_resize_hints,
    .adjust_size = gui_adjust_size,
    .set_size = gui_set_size,
    .set_parent = gui_set_parent,
    .set_transient = gui_set_transient,
    .suggest_title = gui_suggest_title,
    .show = gui_show,
    .hide = gui_hide
};

//------------------------------------------------------------------------------
// Extension Query
//------------------------------------------------------------------------------

static const void* plugin_get_extension(const clap_plugin_t* plugin,
                                        const char* id) {
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports;
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) return &s_params;
    if (strcmp(id, CLAP_EXT_STATE) == 0) return &s_state;
    if (strcmp(id, CLAP_EXT_GUI) == 0) return &s_gui;
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
    auto* instance = new PluginInstance();
    instance->plugin = std::make_shared<NinjamPlugin>();

    auto* plugin = instance->plugin.get();
    plugin->clap_plugin = clap_plugin;
    plugin->host = host;
    clap_plugin->plugin_data = instance;

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

bool ninjam_entry_init(const char* path) {
    return true;
}

void ninjam_entry_deinit(void) {
}

const void* ninjam_entry_get_factory(const char* factory_id) {
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &s_factory;
    return nullptr;
}
