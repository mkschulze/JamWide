# CLAP NINJAM Client - Technical Design Document (Part 2 of 3)

## Part 2: CLAP Integration

---

## 1. CLAP Entry Point

### 1.1 Plugin Descriptor (`src/plugin/clap_entry.cpp`)

```cpp
#include <clap/clap.h>
#include "ninjam_plugin.h"

// Plugin descriptor
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
    .features = (const char*[]){
        CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
        CLAP_PLUGIN_FEATURE_UTILITY,
        CLAP_PLUGIN_FEATURE_MIXING,
        nullptr
    }
};
```

### 1.2 Plugin Factory

```cpp
// Forward declarations
static const clap_plugin_t* create_plugin(const clap_plugin_factory_t* factory,
                                          const clap_host_t* host,
                                          const char* plugin_id);

static uint32_t get_plugin_count(const clap_plugin_factory_t* factory) {
    return 1;
}

static const clap_plugin_descriptor_t* get_plugin_descriptor(
    const clap_plugin_factory_t* factory,
    uint32_t index) {
    return (index == 0) ? &s_descriptor : nullptr;
}

static const clap_plugin_factory_t s_factory = {
    .get_plugin_count = get_plugin_count,
    .get_plugin_descriptor = get_plugin_descriptor,
    .create_plugin = create_plugin
};
```

### 1.3 Entry Point

```cpp
static bool entry_init(const char* path) {
    // Initialize any global resources here
    // For NINJAM: nothing needed (per-instance resources only)
    return true;
}

static void entry_deinit(void) {
    // Cleanup global resources
}

static const void* entry_get_factory(const char* factory_id) {
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &s_factory;
    }
    return nullptr;
}

// Export the entry point
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = entry_init,
    .deinit = entry_deinit,
    .get_factory = entry_get_factory
};
```

---

## 2. Plugin Implementation

### 2.1 Plugin Creation (`src/plugin/ninjam_plugin.cpp`)

```cpp
#include "ninjam_plugin.h"
#include "clap_audio.h"
#include "clap_params.h"
#include "clap_state.h"
#include "clap_gui.h"
#include "threading/run_thread.h"

// Forward declarations for clap_plugin_t callbacks
static bool plugin_init(const clap_plugin_t* plugin);
static void plugin_destroy(const clap_plugin_t* plugin);
static bool plugin_activate(const clap_plugin_t* plugin,
                            double sample_rate,
                            uint32_t min_frames,
                            uint32_t max_frames);
static void plugin_deactivate(const clap_plugin_t* plugin);
static bool plugin_start_processing(const clap_plugin_t* plugin);
static void plugin_stop_processing(const clap_plugin_t* plugin);
static void plugin_reset(const clap_plugin_t* plugin);
static clap_process_status plugin_process(const clap_plugin_t* plugin,
                                          const clap_process_t* process);
static const void* plugin_get_extension(const clap_plugin_t* plugin,
                                        const char* id);
static void plugin_on_main_thread(const clap_plugin_t* plugin);

// Plugin vtable
static const clap_plugin_t s_plugin_class = {
    .desc = &s_descriptor,
    .plugin_data = nullptr,  // Set per instance
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
```

### 2.2 Plugin Factory Create

```cpp
static const clap_plugin_t* create_plugin(const clap_plugin_factory_t* factory,
                                          const clap_host_t* host,
                                          const char* plugin_id) {
    if (strcmp(plugin_id, s_descriptor.id) != 0) {
        return nullptr;
    }

    // Check host version
    if (!clap_version_is_compatible(host->clap_version)) {
        return nullptr;
    }

    // Allocate plugin instance
    auto* clap_plugin = new clap_plugin_t(s_plugin_class);
    auto* plugin = new NinjamPlugin();

    plugin->clap_plugin = clap_plugin;
    plugin->clap_host = host;
    clap_plugin->plugin_data = plugin;

    return clap_plugin;
}
```

### 2.3 Plugin Lifecycle

```cpp
static bool plugin_init(const clap_plugin_t* clap_plugin) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    // Initialize UI state defaults
    snprintf(plugin->ui_state.server_input,
             sizeof(plugin->ui_state.server_input),
             "%s", "ninjam.com:2049");
    snprintf(plugin->ui_state.username_input,
             sizeof(plugin->ui_state.username_input),
             "%s", "player");

    return true;
}

static void plugin_destroy(const clap_plugin_t* clap_plugin) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    // Ensure teardown even if host skips deactivate()
    if (plugin->client) {
        plugin_deactivate(clap_plugin);
    }

    // Clear sensitive data
    plugin->password.clear();

    delete plugin;
    delete clap_plugin;
}

static bool plugin_activate(const clap_plugin_t* clap_plugin,
                            double sample_rate,
                            uint32_t min_frames,
                            uint32_t max_frames) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    plugin->sample_rate = sample_rate;
    plugin->max_frames = max_frames;

    // Create NJClient instance (serialize with client_mutex)
    {
        std::lock_guard<std::mutex> client_lock(plugin->client_mutex);
        std::lock_guard<std::mutex> state_lock(plugin->state_mutex);
        plugin->client = std::make_unique<NJClient>();
    }

    // Setup callbacks
    setup_callbacks(plugin);

    // Start Run thread
    run_thread_start(plugin);

    return true;
}

static void plugin_deactivate(const clap_plugin_t* clap_plugin) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    // Stop Run thread
    run_thread_stop(plugin);

    // Disconnect + destroy NJClient
    {
        std::lock_guard<std::mutex> client_lock(plugin->client_mutex);
        if (plugin->client && plugin->client->GetStatus() >= 0) {
            plugin->client->Disconnect();
        }
    }
    {
        std::lock_guard<std::mutex> state_lock(plugin->state_mutex);
        plugin->client.reset();
    }
}

static bool plugin_start_processing(const clap_plugin_t* clap_plugin) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);
    plugin->audio_active.store(true, std::memory_order_release);
    return true;
}

static void plugin_stop_processing(const clap_plugin_t* clap_plugin) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);
    plugin->audio_active.store(false, std::memory_order_release);
}

static void plugin_reset(const clap_plugin_t* clap_plugin) {
    // Nothing to reset for NINJAM
}

static void plugin_on_main_thread(const clap_plugin_t* clap_plugin) {
    // Called when host requests main thread callback
    // Can be used for deferred UI updates
}
```

### 2.4 Extension Query

```cpp
static const void* plugin_get_extension(const clap_plugin_t* clap_plugin,
                                        const char* id) {
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &s_audio_ports;
    }
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &s_params;
    }
    if (strcmp(id, CLAP_EXT_STATE) == 0) {
        return &s_state;
    }
    if (strcmp(id, CLAP_EXT_GUI) == 0) {
        return &s_gui;
    }
    return nullptr;
}
```

---

## 3. Audio Ports Extension

### 3.1 Implementation (`src/plugin/clap_audio.cpp`)

```cpp
#include <clap/clap.h>
#include "ninjam_plugin.h"

static uint32_t audio_ports_count(const clap_plugin_t* plugin, bool is_input) {
    return 1;  // Single stereo port for both input and output
}

static bool audio_ports_get(const clap_plugin_t* plugin,
                            uint32_t index,
                            bool is_input,
                            clap_audio_port_info_t* info) {
    if (index != 0) return false;

    if (is_input) {
        info->id = 0;
        strncpy(info->name, "Audio In", sizeof(info->name));
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = 0;  // Can process in-place with output 0
    } else {
        info->id = 0;
        strncpy(info->name, "Audio Out", sizeof(info->name));
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = 0;
    }

    return true;
}

const clap_plugin_audio_ports_t s_audio_ports = {
    .count = audio_ports_count,
    .get = audio_ports_get
};
```

---

## 4. Audio Processing

### 4.1 Process Implementation (`src/plugin/ninjam_plugin.cpp`)

```cpp
static clap_process_status plugin_process(const clap_plugin_t* clap_plugin,
                                          const clap_process_t* process) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    // Handle parameter events
    process_param_events(plugin, process->in_events);

    // Check if we have valid audio buffers
    if (process->audio_inputs_count == 0 ||
        process->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& in_port = process->audio_inputs[0];
    const auto& out_port = process->audio_outputs[0];

    if (in_port.channel_count < 2 || out_port.channel_count < 2) {
        return CLAP_PROCESS_CONTINUE;
    }

    // Get buffer pointers
    float* in[2] = {
        in_port.data32[0],
        in_port.data32[1]
    };
    float* out[2] = {
        out_port.data32[0],
        out_port.data32[1]
    };

    uint32_t frames = process->frames_count;

    // Read transport state (lock-free)
    bool is_playing = false;
    bool is_seek = false;
    double cursor_pos = -1.0;

    if (process->transport) {
        is_playing = (process->transport->flags &
                      CLAP_TRANSPORT_IS_PLAYING) != 0;

        if (process->transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) {
            // Convert beat position to seconds if needed
            // cursor_pos = process->transport->song_pos_beats / ...
        }
    }

    // Sync CLAP params to NJClient atomics
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

    if (status == NJC_STATUS_OK) {
        // ReaNINJAM-style: call AudioProc, use just_monitor when not playing
        bool just_monitor = !is_playing;

        plugin->client->AudioProc(
            in, 2,
            out, 2,
            static_cast<int>(frames),
            static_cast<int>(plugin->sample_rate),
            just_monitor,
            is_playing,
            is_seek,
            cursor_pos
        );
    } else {
        // Not connected: pass-through audio
        if (in[0] != out[0]) {
            memcpy(out[0], in[0], frames * sizeof(float));
        }
        if (in[1] != out[1]) {
            memcpy(out[1], in[1], frames * sizeof(float));
        }
    }

    return CLAP_PROCESS_CONTINUE;
}
```

### 4.2 Parameter Event Processing

```cpp
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
                        static_cast<float>(ev->value),
                        std::memory_order_relaxed);
                    break;
                case PARAM_MASTER_MUTE:
                    plugin->param_master_mute.store(
                        ev->value >= 0.5,
                        std::memory_order_relaxed);
                    break;
                case PARAM_METRO_VOLUME:
                    plugin->param_metro_volume.store(
                        static_cast<float>(ev->value),
                        std::memory_order_relaxed);
                    break;
                case PARAM_METRO_MUTE:
                    plugin->param_metro_mute.store(
                        ev->value >= 0.5,
                        std::memory_order_relaxed);
                    break;
            }
        }
    }
}
```

---

## 5. Parameters Extension

### 5.1 Parameter Definitions (`src/plugin/clap_params.cpp`)

```cpp
#include <clap/clap.h>
#include "ninjam_plugin.h"
#include <cctype>
#include <cstring>
#include <cstdio>

// Parameter IDs
enum ParamId : clap_id {
    PARAM_MASTER_VOLUME = 0,
    PARAM_MASTER_MUTE = 1,
    PARAM_METRO_VOLUME = 2,
    PARAM_METRO_MUTE = 3,
    PARAM_COUNT = 4
};

static uint32_t params_count(const clap_plugin_t* plugin) {
    return PARAM_COUNT;
}

static bool params_get_info(const clap_plugin_t* plugin,
                            uint32_t index,
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
                             clap_id param_id,
                             double* value) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    switch (param_id) {
        case PARAM_MASTER_VOLUME:
            *value = plugin->param_master_volume.load(std::memory_order_relaxed);
            return true;
        case PARAM_MASTER_MUTE:
            *value = plugin->param_master_mute.load(std::memory_order_relaxed)
                ? 1.0 : 0.0;
            return true;
        case PARAM_METRO_VOLUME:
            *value = plugin->param_metro_volume.load(std::memory_order_relaxed);
            return true;
        case PARAM_METRO_MUTE:
            *value = plugin->param_metro_mute.load(std::memory_order_relaxed)
                ? 1.0 : 0.0;
            return true;
        default:
            return false;
    }
}

static bool params_value_to_text(const clap_plugin_t* plugin,
                                 clap_id param_id,
                                 double value,
                                 char* display,
                                 uint32_t size) {
    switch (param_id) {
        case PARAM_MASTER_VOLUME:
        case PARAM_METRO_VOLUME: {
            // Convert to dB
            if (value <= 0.0) {
                snprintf(display, size, "-inf dB");
            } else {
                double db = 20.0 * log10(value);
                snprintf(display, size, "%.1f dB", db);
            }
            return true;
        }
        case PARAM_MASTER_MUTE:
        case PARAM_METRO_MUTE:
            snprintf(display, size, value >= 0.5 ? "Muted" : "Active");
            return true;
        default:
            return false;
    }
}

static bool iequals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool params_text_to_value(const clap_plugin_t* plugin,
                                 clap_id param_id,
                                 const char* display,
                                 double* value) {
    // Parse dB values or "Muted"/"Active"
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
            if (iequals(display, "Muted") ||
                iequals(display, "1")) {
                *value = 1.0;
                return true;
            }
            if (iequals(display, "Active") ||
                iequals(display, "0")) {
                *value = 0.0;
                return true;
            }
            return false;
        default:
            return false;
    }
}

static void params_flush(const clap_plugin_t* clap_plugin,
                         const clap_input_events_t* in,
                         const clap_output_events_t* out) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);
    process_param_events(plugin, in);
}

const clap_plugin_params_t s_params = {
    .count = params_count,
    .get_info = params_get_info,
    .get_value = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush = params_flush
};
```

---

## 6. State Extension

### 6.1 State Format

JSON format with version field:

```json
{
    "version": 1,
    "server": "ninjam.com:2049",
    "username": "player",
    "master": {
        "volume": 1.0,
        "mute": false
    },
    "metronome": {
        "volume": 0.5,
        "mute": false
    },
    "localChannel": {
        "name": "Channel",
        "bitrate": 64,
        "transmit": true
    }
}
```

### 6.2 Implementation (`src/plugin/clap_state.cpp`)

Uses a small single-header JSON parser (`picojson.h`) for strict parsing and type checks.

```cpp
#include <clap/clap.h>
#include "ninjam_plugin.h"
#include "third_party/picojson.h"
#include <string>

static int get_int(const picojson::object& obj,
                   const char* key,
                   int fallback) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->second.is<double>()) return fallback;
    return static_cast<int>(it->second.get<double>());
}

static double get_double(const picojson::object& obj,
                         const char* key,
                         double fallback) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->second.is<double>()) return fallback;
    return it->second.get<double>();
}

static bool get_bool(const picojson::object& obj,
                     const char* key,
                     bool fallback) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->second.is<bool>()) return fallback;
    return it->second.get<bool>();
}

static std::string get_string(const picojson::object& obj,
                              const char* key,
                              const std::string& fallback) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->second.is<std::string>()) return fallback;
    return it->second.get<std::string>();
}

static bool state_save(const clap_plugin_t* clap_plugin,
                       const clap_ostream_t* stream) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    picojson::object root;
    root["version"] = picojson::value(1.0);
    root["server"] = picojson::value(plugin->server);
    root["username"] = picojson::value(plugin->username);

    picojson::object master;
    master["volume"] = picojson::value(
        plugin->param_master_volume.load(std::memory_order_relaxed));
    master["mute"] = picojson::value(
        plugin->param_master_mute.load(std::memory_order_relaxed));
    root["master"] = picojson::value(master);

    picojson::object metronome;
    metronome["volume"] = picojson::value(
        plugin->param_metro_volume.load(std::memory_order_relaxed));
    metronome["mute"] = picojson::value(
        plugin->param_metro_mute.load(std::memory_order_relaxed));
    root["metronome"] = picojson::value(metronome);

    picojson::object local;
    local["name"] = picojson::value(plugin->local_channel_name);
    local["bitrate"] = picojson::value(
        static_cast<double>(plugin->local_channel_bitrate));
    local["transmit"] = picojson::value(plugin->local_channel_transmit);
    root["localChannel"] = picojson::value(local);

    std::string data = picojson::value(root).serialize();
    int64_t written = stream->write(stream, data.c_str(), data.size());
    return written == static_cast<int64_t>(data.size());
}

static bool state_load(const clap_plugin_t* clap_plugin,
                       const clap_istream_t* stream) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    // Read all data
    std::string data;
    char buffer[1024];
    while (true) {
        int64_t read = stream->read(stream, buffer, sizeof(buffer));
        if (read <= 0) break;
        data.append(buffer, read);
    }

    if (data.empty()) return false;

    picojson::value root_val;
    std::string err = picojson::parse(root_val, data);
    if (!err.empty() || !root_val.is<picojson::object>()) return false;

    const auto& root = root_val.get<picojson::object>();
    int version = get_int(root, "version", 1);
    if (version > 1) return false;

    plugin->server = get_string(root, "server", plugin->server);
    snprintf(plugin->ui_state.server_input,
             sizeof(plugin->ui_state.server_input),
             "%s", plugin->server.c_str());

    plugin->username = get_string(root, "username", plugin->username);
    snprintf(plugin->ui_state.username_input,
             sizeof(plugin->ui_state.username_input),
             "%s", plugin->username.c_str());

    auto master_it = root.find("master");
    if (master_it != root.end() && master_it->second.is<picojson::object>()) {
        const auto& master = master_it->second.get<picojson::object>();
        plugin->param_master_volume.store(
            static_cast<float>(get_double(master, "volume", 1.0)),
            std::memory_order_relaxed);
        plugin->param_master_mute.store(
            get_bool(master, "mute", false),
            std::memory_order_relaxed);
    }

    auto metro_it = root.find("metronome");
    if (metro_it != root.end() && metro_it->second.is<picojson::object>()) {
        const auto& metro = metro_it->second.get<picojson::object>();
        plugin->param_metro_volume.store(
            static_cast<float>(get_double(metro, "volume", 0.5)),
            std::memory_order_relaxed);
        plugin->param_metro_mute.store(
            get_bool(metro, "mute", false),
            std::memory_order_relaxed);
    }

    auto local_it = root.find("localChannel");
    if (local_it != root.end() && local_it->second.is<picojson::object>()) {
        const auto& local = local_it->second.get<picojson::object>();
        plugin->local_channel_name = get_string(local, "name", "Channel");
        snprintf(plugin->ui_state.local_name_input,
                 sizeof(plugin->ui_state.local_name_input),
                 "%s", plugin->local_channel_name.c_str());
        plugin->local_channel_bitrate = get_int(local, "bitrate", 64);
        plugin->local_channel_transmit = get_bool(local, "transmit", true);
        plugin->ui_state.local_transmit = plugin->local_channel_transmit;
    }

    return true;
}

const clap_plugin_state_t s_state = {
    .save = state_save,
    .load = state_load
};
```

---

## 7. GUI Extension

### 7.1 GUI Interface (`src/plugin/clap_gui.cpp`)

```cpp
#include <clap/clap.h>
#include "ninjam_plugin.h"
#include "platform/gui_context.h"

#ifdef _WIN32
extern GuiContext* create_gui_context_win32(NinjamPlugin* plugin);
#elif __APPLE__
extern GuiContext* create_gui_context_macos(NinjamPlugin* plugin);
#endif

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
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    if (plugin->gui_created) return true;

#ifdef _WIN32
    if (strcmp(api, CLAP_WINDOW_API_WIN32) != 0) return false;
    plugin->gui_context = create_gui_context_win32(plugin);
#elif __APPLE__
    if (strcmp(api, CLAP_WINDOW_API_COCOA) != 0) return false;
    plugin->gui_context = create_gui_context_macos(plugin);
#endif

    if (!plugin->gui_context) return false;

    plugin->gui_created = true;
    return true;
}

static void gui_destroy(const clap_plugin_t* clap_plugin) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    if (plugin->gui_context) {
        delete plugin->gui_context;
        plugin->gui_context = nullptr;
    }

    plugin->gui_created = false;
    plugin->gui_visible = false;
}

static bool gui_set_scale(const clap_plugin_t* clap_plugin, double scale) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    if (plugin->gui_context) {
        plugin->gui_context->set_scale(scale);
    }
    return true;
}

static bool gui_get_size(const clap_plugin_t* clap_plugin,
                         uint32_t* width,
                         uint32_t* height) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

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
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    plugin->gui_width = width;
    plugin->gui_height = height;

    if (plugin->gui_context) {
        plugin->gui_context->set_size(width, height);
    }

    return true;
}

static bool gui_set_parent(const clap_plugin_t* clap_plugin,
                           const clap_window_t* window) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

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
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    if (!plugin->gui_context) return false;

    plugin->gui_context->show();
    plugin->gui_visible = true;
    return true;
}

static bool gui_hide(const clap_plugin_t* clap_plugin) {
    auto* plugin = static_cast<NinjamPlugin*>(clap_plugin->plugin_data);

    if (!plugin->gui_context) return false;

    plugin->gui_context->hide();
    plugin->gui_visible = false;
    return true;
}

const clap_plugin_gui_t s_gui = {
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
```

### 7.2 GUI Context Interface (`src/platform/gui_context.h`)

```cpp
#pragma once

#include <cstdint>

struct NinjamPlugin;

// Abstract GUI context interface
struct GuiContext {
    virtual ~GuiContext() = default;

    virtual bool set_parent(void* parent_handle) = 0;
    virtual void set_size(uint32_t width, uint32_t height) = 0;
    virtual void set_scale(double scale) = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void render() = 0;

protected:
    NinjamPlugin* plugin_ = nullptr;
    double scale_ = 1.0;
    uint32_t width_ = 600;
    uint32_t height_ = 400;
};
```

---

## 8. Summary

Part 2 covers:
- CLAP entry point and plugin factory
- Plugin lifecycle (init, destroy, activate, deactivate, start/stop processing)
- Audio ports extension (stereo I/O)
- Audio processing with transport handling
- Parameters extension (4 automatable params)
- State extension (JSON save/load, no password)
- GUI extension interface

**Next: Part 3 - UI Implementation** (Dear ImGui integration, platform layers, UI panels)
