/*
    NINJAM CLAP Plugin - ui_main.h
    Main UI render function
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#ifndef UI_MAIN_H
#define UI_MAIN_H

namespace jamwide {
struct JamWidePlugin;
}

/**
 * Main UI render function - called every frame.
 * Renders the entire NINJAM plugin interface.
 */
void ui_render_frame(jamwide::JamWidePlugin* plugin);

#endif // UI_MAIN_H
