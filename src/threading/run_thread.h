/*
    NINJAM CLAP Plugin - run_thread.h
    Network thread management
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#ifndef RUN_THREAD_H
#define RUN_THREAD_H

namespace ninjam {

struct NinjamPlugin;

/**
 * Start the Run thread.
 * Called from plugin activate.
 * 
 * @param plugin Plugin instance
 */
void run_thread_start(NinjamPlugin* plugin);

/**
 * Stop the Run thread.
 * Called from plugin deactivate.
 * Blocks until thread terminates.
 * 
 * @param plugin Plugin instance
 */
void run_thread_stop(NinjamPlugin* plugin);

} // namespace ninjam

#endif // RUN_THREAD_H
