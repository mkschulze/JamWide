/*
    NINJAM CLAP Plugin - ninjam_plugin.h
    Main plugin instance structure
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#ifndef NINJAM_PLUGIN_H
#define NINJAM_PLUGIN_H

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <memory>

#include "clap/clap.h"
#include "threading/spsc_ring.h"
#include "threading/ui_event.h"
#include "ui/ui_state.h"

// Forward declarations
class NJClient;

namespace ninjam {

struct GuiContext;

/**
 * Main plugin instance structure.
 * One instance per CLAP plugin instance.
 */
struct NinjamPlugin {
    // CLAP references
    const clap_plugin_t* clap_plugin{nullptr};
    const clap_host_t* host{nullptr};
    
    // NJClient instance
    std::unique_ptr<NJClient> client;
    
    // =========== Threading Primitives ===========
    
    // Main state mutex (protects NJClient API calls except AudioProc)
    std::mutex state_mutex;
    
    // Run thread
    std::thread run_thread;
    std::atomic<bool> shutdown{false};
    
    // UI event queue (Run → UI)
    SpscRing<UiEvent, 256> ui_queue;
    
    // License dialog synchronization
    std::mutex license_mutex;
    std::condition_variable license_cv;
    std::atomic<bool> license_pending{false};
    std::atomic<int> license_response{0};  // 0=pending, 1=accept, -1=reject
    std::string license_text;
    
    // =========== Audio State ===========
    
    std::atomic<bool> audio_active{false};
    double sample_rate{48000.0};
    uint32_t max_frames{512};
    
    // =========== Connection Settings ===========
    
    std::string server;
    std::string username;
    std::string password;  // In memory only, not saved to state
    
    // =========== GUI ===========
    
    GuiContext* gui_context{nullptr};
    UiState ui_state;
    UiAtomicSnapshot ui_snapshot;
    bool gui_created{false};
    bool gui_visible{false};
    uint32_t gui_width{600};
    uint32_t gui_height{400};
    
    // =========== Parameters ===========
    
    // Cached parameter values (synced with NJClient atomics)
    std::atomic<float> param_master_volume{1.0f};
    std::atomic<bool> param_master_mute{false};
    std::atomic<float> param_metro_volume{0.5f};
    std::atomic<bool> param_metro_mute{false};
};

// =========== Plugin Lifecycle Functions ===========

/**
 * Create a new plugin instance.
 */
NinjamPlugin* ninjam_plugin_create(const clap_plugin_t* clap_plugin,
                                   const clap_host_t* host);

/**
 * Destroy a plugin instance.
 */
void ninjam_plugin_destroy(NinjamPlugin* plugin);

/**
 * Activate the plugin (called when added to audio graph).
 */
bool ninjam_plugin_activate(NinjamPlugin* plugin, double sample_rate,
                            uint32_t min_frames, uint32_t max_frames);

/**
 * Deactivate the plugin.
 */
void ninjam_plugin_deactivate(NinjamPlugin* plugin);

/**
 * Start audio processing.
 */
bool ninjam_plugin_start_processing(NinjamPlugin* plugin);

/**
 * Stop audio processing.
 */
void ninjam_plugin_stop_processing(NinjamPlugin* plugin);

/**
 * Process audio.
 */
clap_process_status ninjam_plugin_process(NinjamPlugin* plugin,
                                          const clap_process_t* process);

} // namespace ninjam

#endif // NINJAM_PLUGIN_H
