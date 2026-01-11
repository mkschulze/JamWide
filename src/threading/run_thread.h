/*
    JamWide Plugin - run_thread.h
    Network thread management
    
    Copyright (C) 2024 JamWide Contributors
    Licensed under GPLv2+
*/

#ifndef RUN_THREAD_H
#define RUN_THREAD_H

#include <memory>

namespace jamwide {

struct JamWidePlugin;

/**
 * Start the Run thread.
 * Called from plugin activate.
 * 
 * @param plugin Plugin instance
 */
void run_thread_start(JamWidePlugin* plugin,
                      std::shared_ptr<JamWidePlugin> keepalive);

/**
 * Stop the Run thread.
 * Called from plugin deactivate.
 * Blocks until thread terminates.
 * 
 * @param plugin Plugin instance
 */
void run_thread_stop(JamWidePlugin* plugin);

} // namespace jamwide

#endif // RUN_THREAD_H
