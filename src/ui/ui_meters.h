/*
    JamWide Plugin - ui_meters.h
    VU meter widget helpers
*/

#ifndef UI_METERS_H
#define UI_METERS_H

#include "imgui.h"

// Render a stereo VU meter (left/right stacked).
void render_vu_meter(const char* label, float left, float right);

#endif // UI_METERS_H
