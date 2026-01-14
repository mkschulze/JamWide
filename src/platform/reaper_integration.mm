/*
    JamWide Plugin - reaper_integration.mm
    REAPER extension API integration for keyboard focus handling
    
    This file integrates with REAPER's extension API to tell REAPER
    when our plugin window is handling text input, so REAPER won't
    intercept transport shortcuts like spacebar.
    
    Copyright (C) 2024-2026 JamWide Contributors
    Licensed under GPLv2+
*/

#ifdef __APPLE__

#include "reaper_integration.h"

#import <Cocoa/Cocoa.h>

#include <map>
#include <mutex>

//------------------------------------------------------------------------------
// REAPER plugin info type (minimal definition to avoid header dependency)
//------------------------------------------------------------------------------

// HWND on macOS with SWELL is typically an NSView*
typedef void* HWND;
typedef long long INT_PTR;
typedef void* MSG;

struct reaper_plugin_info_t {
    int caller_version;
    HWND hwnd_main;
    int (*Register)(const char *name, void *infostruct);
    void* (*GetFunc)(const char *name);
};

namespace jamwide {

//------------------------------------------------------------------------------
// Global state
//------------------------------------------------------------------------------

static reaper_plugin_info_t* g_reaper_info = nullptr;
static std::mutex g_views_mutex;
static std::map<void*, bool (*)(void*)> g_active_views; // view -> get_wants_keyboard callback
static bool g_hwnd_info_registered = false;

//------------------------------------------------------------------------------
// hwnd_info callback
//------------------------------------------------------------------------------

// Called by REAPER to query if a window should be treated as a text field
static int hwnd_info_callback(HWND hwnd, INT_PTR info_type) {
    // info_type 0: query if hwnd is a text field
    // info_type 1: query if global hotkeys should be skipped
    
    if (info_type != 0 && info_type != 1) {
        return 0; // Unknown info_type
    }
    
    std::lock_guard<std::mutex> lock(g_views_mutex);
    
    // Check if this hwnd is one of our views
    auto it = g_active_views.find((void*)hwnd);
    if (it == g_active_views.end()) {
        return 0; // Not our window
    }
    
    // Query if ImGui wants keyboard input
    if (it->second) {
        bool wants_keyboard = it->second((void*)hwnd);
        if (wants_keyboard) {
            NSLog(@"[JamWide REAPER] hwnd_info: view=%p info_type=%lld -> 1 (text field)", hwnd, info_type);
            return 1; // This is a text field / skip global hotkeys
        }
    }
    
    NSLog(@"[JamWide REAPER] hwnd_info: view=%p info_type=%lld -> 0 (not text field)", hwnd, info_type);
    return 0; // Not currently handling text input
}

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------

bool reaper_integration_init(const clap_host_t* host) {
    if (!host || !host->get_extension) {
        return false;
    }
    
    // Query for REAPER extension
    const void* ext = host->get_extension(host, "cockos.reaper_extension");
    if (!ext) {
        NSLog(@"[JamWide REAPER] Not running in REAPER (no cockos.reaper_extension)");
        return false;
    }
    
    g_reaper_info = (reaper_plugin_info_t*)ext;
    NSLog(@"[JamWide REAPER] Found REAPER extension API (version 0x%x)", g_reaper_info->caller_version);
    
    // Register our hwnd_info callback
    if (g_reaper_info->Register) {
        int result = g_reaper_info->Register("hwnd_info", (void*)hwnd_info_callback);
        if (result == 1) {
            g_hwnd_info_registered = true;
            NSLog(@"[JamWide REAPER] Registered hwnd_info callback");
            return true;
        } else {
            NSLog(@"[JamWide REAPER] Failed to register hwnd_info callback (result=%d)", result);
        }
    }
    
    return false;
}

void reaper_integration_shutdown() {
    if (g_reaper_info && g_hwnd_info_registered) {
        // Unregister callback
        g_reaper_info->Register("-hwnd_info", (void*)hwnd_info_callback);
        NSLog(@"[JamWide REAPER] Unregistered hwnd_info callback");
        g_hwnd_info_registered = false;
    }
    g_reaper_info = nullptr;
}

void reaper_register_view(void* view, bool (*get_wants_keyboard)(void* view)) {
    std::lock_guard<std::mutex> lock(g_views_mutex);
    g_active_views[view] = get_wants_keyboard;
    NSLog(@"[JamWide REAPER] Registered view %p", view);
}

void reaper_unregister_view(void* view) {
    std::lock_guard<std::mutex> lock(g_views_mutex);
    g_active_views.erase(view);
    NSLog(@"[JamWide REAPER] Unregistered view %p", view);
}

bool reaper_is_active() {
    return g_reaper_info != nullptr && g_hwnd_info_registered;
}

} // namespace jamwide

#endif // __APPLE__
