/*
    JamWide Plugin - ui_latency_guide.cpp
    Visual latency guide widget
    
    Shows when your playing lands relative to the beat.
    Green = on beat, Yellow = slightly off, Red = way off
*/

#include "ui_latency_guide.h"
#include "plugin/jamwide_plugin.h"
#include "imgui.h"
#include <cmath>

namespace {

constexpr float kOnBeatThresholdMs = 10.0f;
constexpr float kSlightlyOffThresholdMs = 25.0f;
constexpr float kGridHeight = 80.0f;
constexpr float kDotRadius = 5.0f;
constexpr float kConsistencyStddevMax = 0.1f;
constexpr float kDisplayRangeMs = 100.0f;  // Show +/- 100ms range

void push_transient(UiState& state, float offset) {
    state.latency_history[state.latency_history_index] = offset;
    state.latency_history_index =
        (state.latency_history_index + 1) % UiState::kLatencyHistorySize;
    if (state.latency_history_count < UiState::kLatencyHistorySize) {
        state.latency_history_count++;
    }
}

bool compute_stats(const UiState& state, float& mean, float& stddev) {
    if (state.latency_history_count <= 0) {
        mean = 0.0f;
        stddev = 0.0f;
        return false;
    }
    double sum = 0.0;
    for (int i = 0; i < state.latency_history_count; ++i) {
        sum += state.latency_history[i];
    }
    mean = static_cast<float>(sum / state.latency_history_count);
    double var_sum = 0.0;
    for (int i = 0; i < state.latency_history_count; ++i) {
        const double d = state.latency_history[i] - mean;
        var_sum += d * d;
    }
    stddev = static_cast<float>(std::sqrt(var_sum / state.latency_history_count));
    return true;
}

ImU32 dot_color_for_offset_ms(float offset_ms) {
    const float abs_ms = std::fabs(offset_ms);
    if (abs_ms <= kOnBeatThresholdMs) {
        return ImGui::GetColorU32(ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
    }
    if (abs_ms <= kSlightlyOffThresholdMs) {
        return ImGui::GetColorU32(ImVec4(0.9f, 0.8f, 0.2f, 1.0f));
    }
    return ImGui::GetColorU32(ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
}

int effective_bpi(const jamwide::JamWidePlugin* plugin) {
    if (!plugin) return 4;
    int bpi = plugin->ui_snapshot.bpi.load(std::memory_order_relaxed);
    if (bpi <= 0) {
        bpi = plugin->ui_state.bpi;
    }
    return (bpi > 0) ? bpi : 4;
}

float offset_to_ms(float offset_beats, float bpm) {
    if (bpm <= 0.0f) return 0.0f;
    return offset_beats * 60000.0f / bpm;
}

float ms_to_x(float ms, float center_x, float width) {
    // Map -100ms to +100ms to the full width
    const float normalized = ms / kDisplayRangeMs;  // -1 to +1
    return center_x + normalized * (width * 0.5f);
}

} // namespace

void ui_render_latency_guide(jamwide::JamWidePlugin* plugin) {
    if (!plugin) return;

    auto& state = plugin->ui_state;
    if (!ImGui::CollapsingHeader("Timing Guide",
                                 ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    // Help text
    ImGui::TextDisabled("Play along with the beat - dots show your timing");
    
    ImGui::Spacing();

    // Threshold control
    ImGui::SetNextItemWidth(150.0f);
    ImGui::SliderFloat("Sensitivity", &state.transient_threshold,
                       0.01f, 0.5f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Lower = detects quieter notes\nHigher = only loud hits");
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        state.latency_history.fill(0.0f);
        state.latency_history_index = 0;
        state.latency_history_count = 0;
    }

    if (plugin->ui_snapshot.transient_detected.load(std::memory_order_acquire)) {
        const float offset =
            plugin->ui_snapshot.last_transient_beat_offset.load(
                std::memory_order_relaxed);
        push_transient(state, offset);
        plugin->ui_snapshot.transient_detected.store(
            false, std::memory_order_release);
    }

    ImGui::Spacing();

    // === Draw the timing display ===
    const float height = kGridHeight;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 end = ImVec2(start.x + width, start.y + height);
    const float center_x = start.x + width * 0.5f;
    const float center_y = start.y + height * 0.5f;

    auto* draw_list = ImGui::GetWindowDrawList();
    
    // Background
    draw_list->AddRectFilled(start, end, 
        ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.10f, 1.0f)));
    
    // Green "good zone" in center (+/- 10ms visual)
    const float good_zone_width = width * (kOnBeatThresholdMs / kDisplayRangeMs);
    draw_list->AddRectFilled(
        ImVec2(center_x - good_zone_width, start.y),
        ImVec2(center_x + good_zone_width, end.y),
        ImGui::GetColorU32(ImVec4(0.1f, 0.25f, 0.1f, 1.0f)));
    
    // Yellow "okay zone" (+/- 25ms)
    const float okay_zone_width = width * (kSlightlyOffThresholdMs / kDisplayRangeMs);
    // Left yellow zone
    draw_list->AddRectFilled(
        ImVec2(center_x - okay_zone_width, start.y),
        ImVec2(center_x - good_zone_width, end.y),
        ImGui::GetColorU32(ImVec4(0.2f, 0.18f, 0.05f, 1.0f)));
    // Right yellow zone
    draw_list->AddRectFilled(
        ImVec2(center_x + good_zone_width, start.y),
        ImVec2(center_x + okay_zone_width, end.y),
        ImGui::GetColorU32(ImVec4(0.2f, 0.18f, 0.05f, 1.0f)));

    // Center line (the beat)
    draw_list->AddLine(
        ImVec2(center_x, start.y), ImVec2(center_x, end.y),
        ImGui::GetColorU32(ImVec4(0.4f, 0.9f, 0.4f, 1.0f)), 2.0f);
    
    // Border
    draw_list->AddRect(start, end, ImGui::GetColorU32(ImGuiCol_Border));

    // Labels
    const ImU32 label_col = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dim_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    
    // "EARLY" and "LATE" labels
    draw_list->AddText(ImVec2(start.x + 8.0f, center_y - 7.0f), dim_col, "EARLY");
    const char* late_label = "LATE";
    const ImVec2 late_size = ImGui::CalcTextSize(late_label);
    draw_list->AddText(ImVec2(end.x - late_size.x - 8.0f, center_y - 7.0f), dim_col, late_label);
    
    // Center label
    draw_list->AddText(ImVec2(center_x + 4.0f, end.y - 18.0f), 
        ImGui::GetColorU32(ImVec4(0.4f, 0.9f, 0.4f, 1.0f)), "BEAT");

    // Get BPM for offset calculations
    const float bpm = plugin->ui_snapshot.bpm.load(std::memory_order_relaxed);
    
    // Draw dots for each sample
    for (int i = 0; i < state.latency_history_count; ++i) {
        const float offset_ms = offset_to_ms(state.latency_history[i], bpm);
        const float x = ms_to_x(offset_ms, center_x, width);
        
        if (x >= start.x && x <= end.x) {
            const ImU32 dot_col = dot_color_for_offset_ms(offset_ms);
            draw_list->AddCircleFilled(ImVec2(x, center_y), kDotRadius, dot_col);
            draw_list->AddCircle(ImVec2(x, center_y), kDotRadius, 
                ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f)), 0, 1.0f);
        }
    }

    // Compute stats
    float mean = 0.0f;
    float stddev = 0.0f;
    const bool have_stats = compute_stats(state, mean, stddev);

    // Draw mean line
    if (have_stats && bpm > 0.0f) {
        const float mean_ms = offset_to_ms(mean, bpm);
        const float mean_x = ms_to_x(mean_ms, center_x, width);
        if (mean_x >= start.x && mean_x <= end.x) {
            draw_list->AddLine(
                ImVec2(mean_x, start.y + 2.0f),
                ImVec2(mean_x, end.y - 2.0f),
                ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.9f)), 2.0f);
        }
    }

    ImGui::Dummy(ImVec2(width, height));

    // Stats display
    if (have_stats && bpm > 0.0f) {
        const float mean_ms = offset_to_ms(mean, bpm);
        const float stddev_ms = offset_to_ms(stddev, bpm);
        const float abs_ms = std::fabs(mean_ms);
        
        ImVec4 color;
        const char* verdict;
        if (abs_ms <= kOnBeatThresholdMs) {
            color = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
            verdict = "On beat!";
        } else if (abs_ms <= kSlightlyOffThresholdMs) {
            color = ImVec4(0.9f, 0.8f, 0.2f, 1.0f);
            verdict = (mean_ms > 0.0f) ? "Slightly late" : "Slightly early";
        } else {
            color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
            verdict = (mean_ms > 0.0f) ? "Too late" : "Too early";
        }

        ImGui::TextColored(color, "%s", verdict);
        ImGui::SameLine();
        ImGui::TextDisabled("(avg: %+.0f ms, spread: %.0f ms)", mean_ms, stddev_ms);
        
        // Samples counter
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80.0f);
        ImGui::TextDisabled("%d samples", state.latency_history_count);
    } else {
        ImGui::TextDisabled("Play some notes to see your timing...");
    }
}
