/*
    NINJAM CLAP Plugin - gui_context.h
    Abstract GUI context interface for platform-specific rendering
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#ifndef GUI_CONTEXT_H
#define GUI_CONTEXT_H

#include <cstdint>
#include <memory>

namespace jamwide {

struct JamWidePlugin;

/**
 * Abstract GUI context interface.
 * Platform-specific implementations handle window creation,
 * graphics initialization, and ImGui rendering.
 */
struct GuiContext {
    virtual ~GuiContext() = default;

    /**
     * Set the parent window handle.
     * Called by CLAP gui_set_parent.
     * @param parent_handle Platform-specific parent (HWND on Win32, NSView* on macOS)
     * @return true on success
     */
    virtual bool set_parent(void* parent_handle) = 0;

    /**
     * Set the GUI size.
     * @param width Width in pixels
     * @param height Height in pixels
     */
    virtual void set_size(uint32_t width, uint32_t height) = 0;

    /**
     * Set the GUI scale factor for HiDPI.
     * @param scale Scale factor (1.0 = 100%)
     */
    virtual void set_scale(double scale) = 0;

    /**
     * Show the GUI window.
     */
    virtual void show() = 0;

    /**
     * Hide the GUI window.
     */
    virtual void hide() = 0;

    /**
     * Render a frame.
     * Called periodically by the platform layer or host.
     */
    virtual void render() = 0;

protected:
    std::shared_ptr<JamWidePlugin> plugin_;
    double scale_ = 1.0;
    uint32_t width_ = 600;
    uint32_t height_ = 400;
};

// Platform-specific factory functions
#ifdef _WIN32
GuiContext* create_gui_context_win32(std::shared_ptr<JamWidePlugin> plugin);
#endif

#ifdef __APPLE__
GuiContext* create_gui_context_macos(std::shared_ptr<JamWidePlugin> plugin);
#endif

} // namespace jamwide

#endif // GUI_CONTEXT_H
