/*
    NINJAM CLAP Plugin - gui_win32.cpp
    Windows GUI implementation using Win32 + D3D11 + ImGui
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#ifdef _WIN32

#include "gui_context.h"
#include "plugin/jamwide_plugin.h"
#include "ui/ui_main.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace jamwide {

class GuiContextWin32 : public GuiContext {
public:
    explicit GuiContextWin32(std::shared_ptr<JamWidePlugin> plugin)
        : hwnd_(nullptr)
        , parent_hwnd_(nullptr)
        , device_(nullptr)
        , device_context_(nullptr)
        , swap_chain_(nullptr)
        , render_target_view_(nullptr)
        , timer_id_(0)
        , imgui_ctx_(nullptr)
    {
        plugin_ = std::move(plugin);
    }

    ~GuiContextWin32() override {
        cleanup();
    }

    bool set_parent(void* parent_handle) override {
        parent_hwnd_ = static_cast<HWND>(parent_handle);

        // Register window class
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wnd_proc_static;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"NinjamClapGui";
        RegisterClassExW(&wc);

        // Create child window
        hwnd_ = CreateWindowExW(
            0,
            L"NinjamClapGui",
            L"NINJAM",
            WS_CHILD | WS_VISIBLE,
            0, 0, static_cast<int>(width_), static_cast<int>(height_),
            parent_hwnd_,
            nullptr,
            GetModuleHandle(nullptr),
            this
        );

        if (!hwnd_) return false;

        // Initialize D3D11
        if (!create_device()) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
            return false;
        }

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        imgui_ctx_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(imgui_ctx_);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;  // Don't save imgui.ini

        // Set style
        ImGui::StyleColorsDark();
        setup_style();

        // Initialize platform/renderer
        ImGui_ImplWin32_Init(hwnd_);
        ImGui_ImplDX11_Init(device_, device_context_);

        // Start render timer (60 FPS)
        timer_id_ = SetTimer(hwnd_, 1, 16, nullptr);

        return true;
    }

    void set_size(uint32_t width, uint32_t height) override {
        width_ = width;
        height_ = height;

        if (hwnd_) {
            SetWindowPos(hwnd_, nullptr, 0, 0,
                         static_cast<int>(width), static_cast<int>(height),
                         SWP_NOMOVE | SWP_NOZORDER);
            resize_buffers();
        }
    }

    void set_scale(double scale) override {
        scale_ = scale;
        if (imgui_ctx_) {
            ImGui::SetCurrentContext(imgui_ctx_);
            ImGuiIO& io = ImGui::GetIO();
            io.FontGlobalScale = static_cast<float>(scale);
        }
    }

    void show() override {
        if (hwnd_) {
            ShowWindow(hwnd_, SW_SHOW);
        }
    }

    void hide() override {
        if (hwnd_) {
            ShowWindow(hwnd_, SW_HIDE);
        }
    }

    void render() override {
        if (!hwnd_ || !device_) return;

        if (imgui_ctx_) {
            ImGui::SetCurrentContext(imgui_ctx_);
        }

        auto plugin = plugin_;
        if (!plugin) {
            return;
        }

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render UI
        ui_render_frame(plugin.get());

        // Render ImGui
        ImGui::Render();

        // Clear and draw
        float clear_color[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        device_context_->OMSetRenderTargets(1, &render_target_view_, nullptr);
        device_context_->ClearRenderTargetView(render_target_view_, clear_color);

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present
        swap_chain_->Present(1, 0);
    }

private:
    HWND hwnd_;
    HWND parent_hwnd_;
    ID3D11Device* device_;
    ID3D11DeviceContext* device_context_;
    IDXGISwapChain* swap_chain_;
    ID3D11RenderTargetView* render_target_view_;
    UINT_PTR timer_id_;
    ImGuiContext* imgui_ctx_;

    bool create_device() {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = width_;
        sd.BufferDesc.Height = height_;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd_;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        D3D_FEATURE_LEVEL level;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            flags, nullptr, 0, D3D11_SDK_VERSION,
            &sd, &swap_chain_, &device_, &level, &device_context_);

        if (FAILED(hr)) return false;

        create_render_target();
        return true;
    }

    void create_render_target() {
        ID3D11Texture2D* back_buffer;
        swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_view_);
        back_buffer->Release();
    }

    void resize_buffers() {
        if (!swap_chain_) return;

        if (render_target_view_) {
            render_target_view_->Release();
            render_target_view_ = nullptr;
        }

        swap_chain_->ResizeBuffers(0, width_, height_,
            DXGI_FORMAT_UNKNOWN, 0);
        create_render_target();
    }

    void cleanup() {
        if (timer_id_) {
            KillTimer(hwnd_, timer_id_);
            timer_id_ = 0;
        }

        if (imgui_ctx_) {
            ImGui::SetCurrentContext(imgui_ctx_);
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext(imgui_ctx_);
            imgui_ctx_ = nullptr;
            ImGui::SetCurrentContext(nullptr);
        }

        if (render_target_view_) { render_target_view_->Release(); render_target_view_ = nullptr; }
        if (swap_chain_) { swap_chain_->Release(); swap_chain_ = nullptr; }
        if (device_context_) { device_context_->Release(); device_context_ = nullptr; }
        if (device_) { device_->Release(); device_ = nullptr; }

        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    void setup_style() {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4.0f;
        style.FrameRounding = 2.0f;
        style.ScrollbarRounding = 2.0f;
        style.FramePadding = ImVec2(6, 4);
        style.ItemSpacing = ImVec2(8, 4);

        // NINJAM color scheme
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.35f, 0.40f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.30f, 0.35f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.40f, 0.60f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.50f, 0.70f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.45f, 0.65f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.50f, 0.70f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.60f, 0.80f, 1.00f);
    }

    static LRESULT CALLBACK wnd_proc_static(HWND hwnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam) {
        GuiContextWin32* ctx = nullptr;

        if (msg == WM_CREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            ctx = static_cast<GuiContextWin32*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA,
                             reinterpret_cast<LONG_PTR>(ctx));
        } else {
            ctx = reinterpret_cast<GuiContextWin32*>(
                GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        if (ctx && ctx->imgui_ctx_) {
            ImGui::SetCurrentContext(ctx->imgui_ctx_);
        }

        // Forward to ImGui
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return 1;

        if (ctx) {
            return ctx->wnd_proc(hwnd, msg, wParam, lParam);
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    LRESULT wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_SIZE:
                if (device_ && wParam != SIZE_MINIMIZED) {
                    width_ = LOWORD(lParam);
                    height_ = HIWORD(lParam);
                    resize_buffers();
                }
                return 0;

            case WM_TIMER:
                if (wParam == 1) {
                    render();
                }
                return 0;

            case WM_DESTROY:
                return 0;
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
};

GuiContext* create_gui_context_win32(std::shared_ptr<JamWidePlugin> plugin) {
    return new GuiContextWin32(std::move(plugin));
}

} // namespace jamwide

#endif // _WIN32
