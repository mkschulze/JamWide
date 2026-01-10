/*
    NINJAM CLAP Plugin - ui_util.h
    Shared UI helper functions
*/

#ifndef UI_UTIL_H
#define UI_UTIL_H

namespace jamwide {
struct JamWidePlugin;
}

// Recompute any_solo_active based on local + remote channel flags.
void ui_update_solo_state(jamwide::JamWidePlugin* plugin);

#endif // UI_UTIL_H
