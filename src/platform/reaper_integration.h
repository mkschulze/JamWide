/*
    JamWide Plugin - reaper_integration.h
    REAPER extension API integration for keyboard focus handling
    
    Copyright (C) 2024-2026 JamWide Contributors
    Licensed under GPLv2+
*/

#ifndef JAMWIDE_REAPER_INTEGRATION_H
#define JAMWIDE_REAPER_INTEGRATION_H

#ifdef __APPLE__

#include <clap/clap.h>

namespace jamwide {

// Forward declaration
struct JamWidePlugin;

/**
 * Initialize REAPER integration for a plugin instance.
 * Queries the CLAP host for REAPER extension and registers hwnd_info callback.
 * 
 * @param host The CLAP host pointer
 * @return true if REAPER extension was found and registered
 */
bool reaper_integration_init(const clap_host_t* host);

/**
 * Shutdown REAPER integration.
 * Unregisters the hwnd_info callback.
 */
void reaper_integration_shutdown();

/**
 * Register a view as active (for hwnd_info callback to track).
 * Called when GUI is created.
 * 
 * @param view The NSView* (as void*) to register
 * @param get_wants_keyboard Function to query if ImGui wants keyboard input
 */
void reaper_register_view(void* view, bool (*get_wants_keyboard)(void* view));

/**
 * Unregister a view.
 * Called when GUI is destroyed.
 * 
 * @param view The NSView* (as void*) to unregister
 */
void reaper_unregister_view(void* view);

/**
 * Check if we're running in REAPER (and have the extension API).
 */
bool reaper_is_active();

} // namespace jamwide

#endif // __APPLE__

#endif // JAMWIDE_REAPER_INTEGRATION_H
