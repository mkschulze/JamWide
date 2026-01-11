/*
    JamWide Plugin - ui_main.h
    Main UI render function
    
    Copyright (C) 2024 JamWide Contributors
    Licensed under GPLv2+
*/

#ifndef UI_MAIN_H
#define UI_MAIN_H

namespace jamwide {
struct JamWidePlugin;
}

/**
 * Main UI render function - called every frame.
 * Renders the entire JamWide plugin interface.
 */
void ui_render_frame(jamwide::JamWidePlugin* plugin);

#endif // UI_MAIN_H
