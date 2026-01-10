# CLAP NINJAM Client - Technical Design Document (Part 3 of 3)

## Part 3: UI Implementation

---

## 1. Platform GUI Layers

### 1.1 Windows Implementation (`src/platform/gui_win32.cpp`)

```cpp
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

class GuiContextWin32 : public GuiContext {
public:
    explicit GuiContextWin32(JamWidePlugin* plugin)
        : hwnd_(nullptr)
        , parent_hwnd_(nullptr)
        , device_(nullptr)
        , device_context_(nullptr)
        , swap_chain_(nullptr)
        , render_target_view_(nullptr)
        , timer_id_(0)
    {
        plugin_ = plugin;
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
            0, 0, width_, height_,
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
        ImGui::CreateContext();

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
            SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
                         SWP_NOMOVE | SWP_NOZORDER);
            resize_buffers();
        }
    }

    void set_scale(double scale) override {
        scale_ = scale;
        if (ImGui::GetCurrentContext()) {
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

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render UI
        ui_render_frame(plugin_);

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

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (render_target_view_) render_target_view_->Release();
        if (swap_chain_) swap_chain_->Release();
        if (device_context_) device_context_->Release();
        if (device_) device_->Release();

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

GuiContext* create_gui_context_win32(JamWidePlugin* plugin) {
    return new GuiContextWin32(plugin);
}
```

### 1.2 macOS Implementation (`src/platform/gui_macos.mm`)

```objc
#include "gui_context.h"
#include "plugin/jamwide_plugin.h"
#include "ui/ui_main.h"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "imgui.h"
#include "imgui_impl_osx.h"
#include "imgui_impl_metal.h"

@interface NinjamView : MTKView
@property (nonatomic, assign) JamWidePlugin* plugin;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@end

@implementation NinjamView

- (instancetype)initWithFrame:(NSRect)frame plugin:(JamWidePlugin*)plugin {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    self = [super initWithFrame:frame device:device];

    if (self) {
        _plugin = plugin;
        _commandQueue = [device newCommandQueue];

        self.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        self.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
        self.sampleCount = 1;
        self.clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;

        ImGui::StyleColorsDark();
        [self setupStyle];

        ImGui_ImplOSX_Init(self);
        ImGui_ImplMetal_Init(device);
    }

    return self;
}

- (void)dealloc {
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext();
}

- (void)setupStyle {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 4);
}

- (void)drawRect:(NSRect)dirtyRect {
    @autoreleasepool {
        id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
        MTLRenderPassDescriptor* rpd = self.currentRenderPassDescriptor;

        if (rpd == nil) return;

        // Start ImGui frame
        ImGui_ImplMetal_NewFrame(rpd);
        ImGui_ImplOSX_NewFrame(self);
        ImGui::NewFrame();

        // Render UI
        ui_render_frame(_plugin);

        // Render ImGui
        ImGui::Render();

        id<MTLRenderCommandEncoder> encoder =
            [commandBuffer renderCommandEncoderWithDescriptor:rpd];

        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(),
                                        commandBuffer, encoder);

        [encoder endEncoding];
        [commandBuffer presentDrawable:self.currentDrawable];
        [commandBuffer commit];
    }
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];

    if (!self.window) {
        return;
    }

    // ImGui's OSX backend installs a hidden NSTextInputClient subview.
    // Ensure it becomes first responder so text input works.
    for (NSView* subview in self.subviews) {
        if ([subview conformsToProtocol:@protocol(NSTextInputClient)]) {
            [self.window makeFirstResponder:subview];
            break;
        }
    }
}

@end

class GuiContextMacOS : public GuiContext {
public:
    explicit GuiContextMacOS(JamWidePlugin* plugin)
        : view_(nil)
    {
        plugin_ = plugin;
    }

    ~GuiContextMacOS() override {
        if (view_) {
            [view_ removeFromSuperview];
            view_ = nil;
        }
    }

    bool set_parent(void* parent_handle) override {
        NSView* parent = (__bridge NSView*)parent_handle;

        NSRect frame = NSMakeRect(0, 0, width_, height_);
        view_ = [[NinjamView alloc] initWithFrame:frame plugin:plugin_];

        [parent addSubview:view_];

        return true;
    }

    void set_size(uint32_t width, uint32_t height) override {
        width_ = width;
        height_ = height;

        if (view_) {
            [view_ setFrameSize:NSMakeSize(width, height)];
        }
    }

    void set_scale(double scale) override {
        scale_ = scale;
        if (ImGui::GetCurrentContext()) {
            ImGuiIO& io = ImGui::GetIO();
            io.FontGlobalScale = static_cast<float>(scale);
        }
    }

    void show() override {
        if (view_) {
            [view_ setHidden:NO];
        }
    }

    void hide() override {
        if (view_) {
            [view_ setHidden:YES];
        }
    }

    void render() override {
        // MTKView handles rendering via drawRect:
    }

private:
    NinjamView* view_;
};

extern "C" GuiContext* create_gui_context_macos(JamWidePlugin* plugin) {
    return new GuiContextMacOS(plugin);
}
```

---

## 2. UI Main Layout

### 2.1 Header (`src/ui/ui_main.h`)

```cpp
#pragma once

struct JamWidePlugin;

// Main UI render function - called every frame
void ui_render_frame(JamWidePlugin* plugin);
```

### 2.2 UI Snapshot (`src/ui/ui_state.h`)

```cpp
#include <atomic>

// Atomic snapshot for high-frequency UI reads (no state_mutex)
struct UiAtomicSnapshot {
    std::atomic<float> bpm{0.0f};
    std::atomic<int>   bpi{0};
    std::atomic<int>   interval_position{0};
    std::atomic<int>   interval_length{0};
    std::atomic<int>   beat_position{0};

    // VU levels (audio thread writes)
    std::atomic<float> master_vu_left{0.0f};
    std::atomic<float> master_vu_right{0.0f};
    std::atomic<float> local_vu_left{0.0f};
    std::atomic<float> local_vu_right{0.0f};
};
```

### 2.3 Snapshot Updates (Run Thread)

Update the transport fields after `NJClient::Run()` while holding `client_mutex`,
then store to `ui_snapshot` after releasing `client_mutex`:

```cpp
int pos = 0;
int len = 0;
int bpi = 0;
float bpm = 0.0f;
int beat_pos = 0;

plugin->client_mutex.lock();
if (plugin->client->GetStatus() == NJC_STATUS_OK) {
    plugin->client->GetPosition(&pos, &len);
    bpi = plugin->client->GetBPI();
    bpm = plugin->client->GetActualBPM();
    if (len > 0 && bpi > 0) {
        beat_pos = (pos * bpi) / len;
    }
}
plugin->client_mutex.unlock();

plugin->ui_snapshot.bpm.store(bpm, std::memory_order_relaxed);
plugin->ui_snapshot.bpi.store(bpi, std::memory_order_relaxed);
plugin->ui_snapshot.interval_position.store(pos, std::memory_order_relaxed);
plugin->ui_snapshot.interval_length.store(len, std::memory_order_relaxed);
plugin->ui_snapshot.beat_position.store(beat_pos, std::memory_order_relaxed);
```

### 2.4 Implementation (`src/ui/ui_main.cpp`)

```cpp
#include "ui_main.h"
#include "plugin/jamwide_plugin.h"
#include "build_number.h"
#include "imgui.h"

// Forward declarations
void ui_render_status_bar(JamWidePlugin* plugin);
void ui_render_connection_panel(JamWidePlugin* plugin);
void ui_render_local_channel(JamWidePlugin* plugin);
void ui_render_master_panel(JamWidePlugin* plugin);
void ui_render_remote_channels(JamWidePlugin* plugin);
void ui_render_license_dialog(JamWidePlugin* plugin);

void ui_render_frame(JamWidePlugin* plugin) {
    // 1. Drain event queue (lock-free)
    plugin->ui_queue.drain([&](UiEvent&& event) {
        std::visit([&](auto&& e) {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, StatusChangedEvent>) {
                plugin->ui_state.status = e.status;
                plugin->ui_state.connection_error = e.error_msg;
            }
            // UserInfoChangedEvent unused (remote users are read directly under client_mutex)
            else if constexpr (std::is_same_v<T, TopicChangedEvent>) {
                // Could display topic somewhere
            }
            // ChatMessageEvent ignored (no chat in MVP)
        }, std::move(event));
    });

    // 2. Check for license prompt (dedicated slot)
    if (plugin->license_pending.load(std::memory_order_acquire)) {
        plugin->ui_state.show_license_dialog = true;
        {
            std::lock_guard<std::mutex> lock(plugin->license_mutex);
            plugin->ui_state.license_text = plugin->license_text;
        }
    }

    // 3. Update transport from atomic snapshot (lock-free reads)
    if (plugin->ui_state.status == NJC_STATUS_OK) {
        plugin->ui_state.bpm =
            plugin->ui_snapshot.bpm.load(std::memory_order_acquire);
        plugin->ui_state.bpi =
            plugin->ui_snapshot.bpi.load(std::memory_order_acquire);
        plugin->ui_state.interval_position =
            plugin->ui_snapshot.interval_position.load(std::memory_order_acquire);
        plugin->ui_state.interval_length =
            plugin->ui_snapshot.interval_length.load(std::memory_order_acquire);
        plugin->ui_state.beat_position =
            plugin->ui_snapshot.beat_position.load(std::memory_order_acquire);
    }

    // 4. Create main window (fills entire area)
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("NINJAM", nullptr, flags);

    // Render panels
    ui_render_status_bar(plugin);
    ImGui::Separator();

    ui_render_connection_panel(plugin);
    ImGui::Separator();

    ui_render_local_channel(plugin);
    ImGui::Separator();

    ui_render_master_panel(plugin);
    ImGui::Separator();

    ui_render_remote_channels(plugin);

    ImGui::End();

    // 5. License dialog (modal)
    if (plugin->ui_state.show_license_dialog) {
        ui_render_license_dialog(plugin);
    }
}
```

---

## 3. Status Bar

### 3.1 Implementation (`src/ui/ui_status.cpp`)

```cpp
#include "plugin/jamwide_plugin.h"
#include "threading/ui_command.h"
#include "core/njclient.h"
#include "ui_meters.h"
#include "ui_util.h"
#include "imgui.h"

void ui_render_status_bar(JamWidePlugin* plugin) {
    auto& state = plugin->ui_state;

    // Connection indicator
    ImVec4 color;
    const char* status_text;

    switch (state.status) {
        case NJC_STATUS_OK:
            color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
            status_text = "Connected";
            break;
        case NJC_STATUS_PRECONNECT:
            color = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
            status_text = "Connecting...";
            break;
        default:
            color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            status_text = "Disconnected";
            break;
    }

    // Status dot
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::Text(u8"●");
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::Text("%s", status_text);

    if (state.status == NJC_STATUS_OK) {
        ImGui::SameLine();
        ImGui::Text(" | ");
        ImGui::SameLine();
        ImGui::Text("%.1f BPM", state.bpm);

        ImGui::SameLine();
        ImGui::Text(" | ");
        ImGui::SameLine();
        ImGui::Text("%d BPI", state.bpi);

        // Beat counter
        ImGui::SameLine();
        ImGui::Text(" | Beat: ");
        ImGui::SameLine();

        // Visual beat indicator
        float progress = 0.0f;
        if (state.interval_length > 0) {
            progress = static_cast<float>(state.interval_position) /
                       static_cast<float>(state.interval_length);
        }

        ImGui::ProgressBar(progress, ImVec2(100, 0), "");

        ImGui::SameLine();
        ImGui::Text("%d/%d", state.beat_position + 1, state.bpi);
    }

    // Build label (top-right, e.g. r12)
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 30.0f);
    ImGui::TextDisabled("r%d", NINJAM_BUILD_NUMBER);
}
```

**Note:** The status bar shows a right-aligned build label (`rN`) sourced from
`src/build_number.h` to disambiguate installs during testing.

---

## 4. Connection Panel

### 4.1 Implementation (`src/ui/ui_connection.cpp`)

```cpp
#include "plugin/jamwide_plugin.h"
#include "threading/ui_command.h"
#include "core/njclient.h"
#include "imgui.h"

void ui_render_connection_panel(JamWidePlugin* plugin) {
    auto& state = plugin->ui_state;

    if (ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // Server input
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("Server", state.server_input,
                         sizeof(state.server_input));

        ImGui::SameLine();

        // Username input
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("Username", state.username_input,
                         sizeof(state.username_input));

        ImGui::SameLine();

        // Password input
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("Password", state.password_input,
                         sizeof(state.password_input),
                         ImGuiInputTextFlags_Password);

        ImGui::SameLine();

        // Connect/Disconnect buttons (UI → Run commands)
        const bool is_connected =
            (state.status == NJClient::NJC_STATUS_OK ||
             state.status == NJClient::NJC_STATUS_PRECONNECT);

        if (!is_connected) {
            if (ImGui::Button("Connect")) {
                ConnectCommand cmd;
                cmd.server = state.server_input;
                cmd.username = state.username_input;
                cmd.password = state.password_input;
                if (!plugin->cmd_queue.try_push(std::move(cmd))) {
                    state.connection_error = "Connect request queue full";
                } else {
                    state.connection_error.clear();
                }
            }
        } else {
            if (ImGui::Button("Disconnect")) {
                DisconnectCommand cmd;
                if (!plugin->cmd_queue.try_push(std::move(cmd))) {
                    state.connection_error = "Disconnect request queue full";
                }
            }
        }

        // Error message
        if (!state.connection_error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "%s", state.connection_error.c_str());
        }

        ImGui::Unindent();
    }
}
```

---

## 5. Local Channel Panel

### 5.1 Implementation (`src/ui/ui_local.cpp`)

```cpp
#include "plugin/jamwide_plugin.h"
#include "imgui.h"

static const char* bitrate_labels[] = {
    "32 kbps", "64 kbps", "96 kbps", "128 kbps", "192 kbps", "256 kbps"
};
static const int bitrate_values[] = { 32, 64, 96, 128, 192, 256 };

void ui_render_local_channel(JamWidePlugin* plugin) {
    auto& state = plugin->ui_state;

    if (!ImGui::CollapsingHeader("Local Channel", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::Indent();

    // Channel name
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputText("Name", state.local_name_input,
                         sizeof(state.local_name_input))) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            SetLocalChannelInfoCommand cmd;
            cmd.channel = 0;
            cmd.name = state.local_name_input;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SameLine();

    // Bitrate dropdown
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("Bitrate", &state.local_bitrate_index,
                     bitrate_labels, IM_ARRAYSIZE(bitrate_labels))) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            SetLocalChannelInfoCommand cmd;
            cmd.channel = 0;
            cmd.name = state.local_name_input;
            cmd.set_bitrate = true;
            cmd.bitrate = bitrate_values[state.local_bitrate_index];
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SameLine();

    // Transmit checkbox
    if (ImGui::Checkbox("Transmit", &state.local_transmit)) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            SetLocalChannelInfoCommand cmd;
            cmd.channel = 0;
            cmd.name = state.local_name_input;
            cmd.set_transmit = true;
            cmd.transmit = state.local_transmit;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    // Volume slider
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("Volume##local", &state.local_volume,
                           0.0f, 2.0f, "%.2f")) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            SetLocalChannelMonitoringCommand cmd;
            cmd.channel = 0;
            cmd.set_volume = true;
            cmd.volume = state.local_volume;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SameLine();

    // Pan slider
    ImGui::SetNextItemWidth(80);
    if (ImGui::SliderFloat("Pan##local", &state.local_pan,
                           -1.0f, 1.0f, "%.2f")) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            SetLocalChannelMonitoringCommand cmd;
            cmd.channel = 0;
            cmd.set_pan = true;
            cmd.pan = state.local_pan;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SameLine();

    // Mute button
    if (ImGui::Checkbox("M##local_mute", &state.local_mute)) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            SetLocalChannelMonitoringCommand cmd;
            cmd.channel = 0;
            cmd.set_mute = true;
            cmd.mute = state.local_mute;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SameLine();
    // Solo button
    if (ImGui::Checkbox("S##local_solo", &state.local_solo)) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            SetLocalChannelMonitoringCommand cmd;
            cmd.channel = 0;
            cmd.set_solo = true;
            cmd.solo = state.local_solo;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
        ui_update_solo_state(plugin);
    }

    // VU Meter
    ImGui::SameLine();
    float local_vu_l = plugin->ui_snapshot.local_vu_left.load(
        std::memory_order_relaxed);
    float local_vu_r = plugin->ui_snapshot.local_vu_right.load(
        std::memory_order_relaxed);
    render_vu_meter("##local_vu", local_vu_l, local_vu_r);

    ImGui::Unindent();
}

void ui_update_solo_state(JamWidePlugin* plugin) {
    bool any_solo_active = plugin->ui_state.local_solo;

    std::unique_lock<std::mutex> lock(plugin->client_mutex);
    NJClient* client = plugin->client.get();
    if (client) {
        const int num_users = client->GetNumUsers();
        for (int u = 0; u < num_users && !any_solo_active; ++u) {
            for (int i = 0; ; ++i) {
                const int ch = client->EnumUserChannels(u, i);
                if (ch < 0) break;
                bool solo = false;
                if (client->GetUserChannelState(u, ch, nullptr, nullptr,
                                                nullptr, nullptr, &solo,
                                                nullptr, nullptr)) {
                    if (solo) {
                        any_solo_active = true;
                        break;
                    }
                }
            }
        }
    }

    plugin->ui_state.any_solo_active = any_solo_active;
}
```

---

## 6. Master Panel

### 6.1 Implementation (`src/ui/ui_master.cpp`)

```cpp
#include "plugin/jamwide_plugin.h"
#include "imgui.h"

void ui_render_master_panel(JamWidePlugin* plugin) {
    auto& state = plugin->ui_state;

    if (ImGui::CollapsingHeader("Master", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // Master Volume
        float master_vol = plugin->param_master_volume.load(
            std::memory_order_relaxed);
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat("Master Volume", &master_vol, 0.0f, 2.0f, "%.2f")) {
            plugin->param_master_volume.store(master_vol,
                std::memory_order_relaxed);
            plugin->client->config_mastervolume.store(master_vol,
                std::memory_order_relaxed);
        }

        ImGui::SameLine();

        // Master Mute
        bool master_mute = plugin->param_master_mute.load(
            std::memory_order_relaxed);
        if (ImGui::Checkbox("M##master", &master_mute)) {
            plugin->param_master_mute.store(master_mute,
                std::memory_order_relaxed);
            plugin->client->config_mastermute.store(master_mute,
                std::memory_order_relaxed);
        }

        ImGui::SameLine();

        // Master VU
        float master_vu_l = plugin->ui_snapshot.master_vu_left.load(
            std::memory_order_relaxed);
        float master_vu_r = plugin->ui_snapshot.master_vu_right.load(
            std::memory_order_relaxed);
        render_vu_meter("##master_vu", master_vu_l, master_vu_r);

        ImGui::Spacing();

        // Metronome Volume
        float metro_vol = plugin->param_metro_volume.load(
            std::memory_order_relaxed);
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat("Metronome", &metro_vol, 0.0f, 2.0f, "%.2f")) {
            plugin->param_metro_volume.store(metro_vol,
                std::memory_order_relaxed);
            plugin->client->config_metronome.store(metro_vol,
                std::memory_order_relaxed);
        }

        ImGui::SameLine();

        // Metronome Mute
        bool metro_mute = plugin->param_metro_mute.load(
            std::memory_order_relaxed);
        if (ImGui::Checkbox("M##metro", &metro_mute)) {
            plugin->param_metro_mute.store(metro_mute,
                std::memory_order_relaxed);
            plugin->client->config_metronome_mute.store(metro_mute,
                std::memory_order_relaxed);
        }

        ImGui::Unindent();
    }
}
```

---

## 7. Remote Channels Panel

### 7.1 Implementation (`src/ui/ui_remote.cpp`)

Remote user data is read directly from NJClient under `client_mutex`
(ReaNINJAM-style). UI sends mutations back via `cmd_queue`.

```cpp
#include "plugin/jamwide_plugin.h"
#include "threading/ui_command.h"
#include "core/njclient.h"
#include "imgui.h"

void ui_render_remote_channels(JamWidePlugin* plugin) {
    if (!plugin) return;

    int status = plugin->ui_state.status;

    if (!ImGui::CollapsingHeader("Remote Users", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::Indent();

    if (status != NJClient::NJC_STATUS_OK) {
        ImGui::TextDisabled("Not connected");
        ImGui::Unindent();
        return;
    }

    std::unique_lock<std::mutex> lock(plugin->client_mutex);
    NJClient* client = plugin->client.get();
    if (!client) {
        ImGui::TextDisabled("Not connected");
        ImGui::Unindent();
        return;
    }

    const int num_users = client->GetNumUsers();
    if (num_users <= 0) {
        ImGui::TextDisabled("No remote users connected");
        ImGui::Unindent();
        return;
    }

    for (int u = 0; u < num_users; ++u) {
        float user_vol = 0.0f;
        float user_pan = 0.0f;
        bool user_mute = false;
        const char* user_name = client->GetUserState(u, &user_vol, &user_pan, &user_mute);
        const char* label = (user_name && *user_name) ? user_name : "User";

        ImGui::PushID(u);

        const bool user_open = ImGui::TreeNodeEx(
            label, ImGuiTreeNodeFlags_DefaultOpen);

        ImGui::SameLine();
        if (ImGui::Checkbox("M##user", &user_mute)) {
            SetUserStateCommand cmd;
            cmd.user_index = u;
            cmd.set_mute = true;
            cmd.mute = user_mute;
            plugin->cmd_queue.try_push(std::move(cmd));
        }

        if (user_open) {
            ImGui::Indent();
            for (int i = 0; ; ++i) {
                const int channel_index = client->EnumUserChannels(u, i);
                if (channel_index < 0) break;

                bool subscribed = false;
                float volume = 1.0f;
                float pan = 0.0f;
                bool mute = false;
                bool solo = false;
                int out_channel = 0;
                int flags = 0;
                const char* channel_name = client->GetUserChannelState(
                    u, channel_index, &subscribed, &volume, &pan,
                    &mute, &solo, &out_channel, &flags);
                if (!channel_name) continue;

                const char* channel_label = (*channel_name) ? channel_name : "Channel";
                ImGui::PushID(channel_index);

                if (ImGui::Checkbox("##sub", &subscribed)) {
                    SetUserChannelStateCommand cmd;
                    cmd.user_index = u;
                    cmd.channel_index = channel_index;
                    cmd.set_sub = true;
                    cmd.subscribed = subscribed;
                    plugin->cmd_queue.try_push(std::move(cmd));
                }

                // Sliders/checkboxes enqueue SetUserChannelStateCommand...
                ImGui::PopID();
            }
            ImGui::Unindent();
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    ImGui::Unindent();
}
```

---

## 8. License Dialog

### 8.1 Implementation (`src/ui/ui_license.cpp`)

```cpp
#include "plugin/jamwide_plugin.h"
#include "imgui.h"

void ui_render_license_dialog(JamWidePlugin* plugin) {
    auto& state = plugin->ui_state;

    ImGui::OpenPopup("Server License Agreement");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Server License Agreement", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Please read and accept the server license:");
        ImGui::Separator();

        // Scrollable license text
        ImGui::BeginChild("LicenseText", ImVec2(480, 280), true);
        ImGui::TextWrapped("%s", state.license_text.c_str());
        ImGui::EndChild();

        ImGui::Separator();

        // Buttons
        float button_width = 100;
        float spacing = 20;
        float total_width = button_width * 2 + spacing;
        float start_x = (ImGui::GetWindowWidth() - total_width) / 2;

        ImGui::SetCursorPosX(start_x);

        if (ImGui::Button("Accept", ImVec2(button_width, 0))) {
            plugin->license_response.store(1, std::memory_order_release);
            plugin->license_pending.store(false, std::memory_order_release);
            plugin->license_cv.notify_one();
            state.show_license_dialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine(0, spacing);

        if (ImGui::Button("Reject", ImVec2(button_width, 0))) {
            plugin->license_response.store(-1, std::memory_order_release);
            plugin->license_pending.store(false, std::memory_order_release);
            plugin->license_cv.notify_one();
            state.show_license_dialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
```

---

## 9. VU Meter Widget

### 9.1 Implementation (`src/ui/ui_meters.cpp`)

```cpp
#include "imgui.h"

void render_vu_meter(const char* id, float left, float right) {
    ImGui::PushID(id);

    ImVec2 size(60, 12);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Background
    draw_list->AddRectFilled(
        pos,
        ImVec2(pos.x + size.x, pos.y + size.y),
        IM_COL32(30, 30, 30, 255));

    // Left channel (top half)
    float left_width = size.x * std::min(1.0f, std::max(0.0f, left));
    ImU32 left_color = get_vu_color(left);
    draw_list->AddRectFilled(
        pos,
        ImVec2(pos.x + left_width, pos.y + size.y / 2 - 1),
        left_color);

    // Right channel (bottom half)
    float right_width = size.x * std::min(1.0f, std::max(0.0f, right));
    ImU32 right_color = get_vu_color(right);
    draw_list->AddRectFilled(
        ImVec2(pos.x, pos.y + size.y / 2 + 1),
        ImVec2(pos.x + right_width, pos.y + size.y),
        right_color);

    // Border
    draw_list->AddRect(
        pos,
        ImVec2(pos.x + size.x, pos.y + size.y),
        IM_COL32(80, 80, 80, 255));

    // Advance cursor
    ImGui::Dummy(size);

    ImGui::PopID();
}

static ImU32 get_vu_color(float level) {
    if (level < 0.6f) {
        // Green
        return IM_COL32(50, 200, 50, 255);
    } else if (level < 0.85f) {
        // Yellow
        return IM_COL32(200, 200, 50, 255);
    } else {
        // Red
        return IM_COL32(200, 50, 50, 255);
    }
}
```

---

## 10. VU Meter Data Collection

### 10.1 Audio Thread VU Updates

Add to audio processing to capture VU levels:

```cpp
// In plugin_process(), after AudioProc():

// Calculate VU levels from output
float peak_left = 0.0f;
float peak_right = 0.0f;

for (uint32_t i = 0; i < frames; ++i) {
    peak_left = std::max(peak_left, std::abs(out[0][i]));
    peak_right = std::max(peak_right, std::abs(out[1][i]));
}

// Store for UI snapshot (atomic writes)
// Using simple peak with decay would be better for production
plugin->ui_snapshot.master_vu_left.store(peak_left, std::memory_order_relaxed);
plugin->ui_snapshot.master_vu_right.store(peak_right, std::memory_order_relaxed);

// Local channel VU (from NJClient peak cache, channel 0)
plugin->ui_snapshot.local_vu_left.store(
    plugin->client->GetLocalChannelPeak(0, 0), std::memory_order_relaxed);
plugin->ui_snapshot.local_vu_right.store(
    plugin->client->GetLocalChannelPeak(0, 1), std::memory_order_relaxed);
```

Note: For production, implement proper peak hold with decay. Simple approach shown for MVP.

---

## 11. Summary

Part 3 covers:
- Platform-specific GUI layers (Win32/D3D11, macOS/Metal)
- Dear ImGui integration and render loop
- Main UI layout and window structure
- Status bar (connection, BPM, BPI, beat counter)
- Connection panel (server, username, password, connect/disconnect)
- Local channel panel (name, bitrate, transmit, volume, mute, solo)
- Master panel (volume, mute, metronome)
- Remote channels panel (users, channels, subscribe, volume, pan, mute, solo)
- License dialog (modal popup with accept/reject)
- VU meter widget

---

## 12. Complete File List

| File | Purpose |
|------|---------|
| `src/plugin/jamwide_plugin.h` | Plugin instance struct |
| `src/plugin/jamwide_plugin.cpp` | Plugin lifecycle |
| `src/plugin/clap_entry.cpp` | CLAP entry point |
| `src/plugin/clap_audio.cpp` | Audio ports extension |
| `src/plugin/clap_params.cpp` | Parameters extension |
| `src/plugin/clap_state.cpp` | State extension |
| `src/plugin/clap_gui.cpp` | GUI extension |
| `src/core/njclient.h` | Modified NJClient |
| `src/core/njclient.cpp` | Modified NJClient |
| `src/core/netmsg.h/cpp` | Network messages |
| `src/core/mpb.h/cpp` | Protocol builders |
| `src/core/njmisc.h/cpp` | Utilities |
| `src/threading/spsc_ring.h` | Lock-free queue |
| `src/threading/run_thread.h/cpp` | Network thread |
| `src/ui/ui_state.h` | UI state struct |
| `src/ui/ui_main.h/cpp` | Main layout |
| `src/ui/ui_status.cpp` | Status bar |
| `src/ui/ui_connection.cpp` | Connection panel |
| `src/ui/ui_local.cpp` | Local channel |
| `src/ui/ui_master.cpp` | Master controls |
| `src/ui/ui_remote.cpp` | Remote channels |
| `src/ui/ui_license.cpp` | License dialog |
| `src/ui/ui_meters.cpp` | VU meters |
| `src/platform/gui_context.h` | GUI interface |
| `src/platform/gui_win32.cpp` | Windows GUI |
| `src/platform/gui_macos.mm` | macOS GUI |

---

## 13. Build & Test

### 13.1 Build Commands

```bash
# Clone and setup
git clone --recursive https://github.com/your-org/JamWide.git
cd JamWide

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Output location
# Windows: build/Release/JamWide.clap
# macOS: build/JamWide.clap/
```

### 13.2 Testing

```bash
# Run clap-validator
clap-validator build/JamWide.clap

# Manual DAW testing
# 1. Copy JamWide.clap to DAW's CLAP folder
# 2. Scan for new plugins
# 3. Insert on track
# 4. Open UI
# 5. Connect to public NINJAM server
# 6. Verify audio flow
```

### 13.3 Tested DAWs

| DAW | Windows | macOS |
|-----|---------|-------|
| REAPER | ✓ | ✓ |
| Bitwig | ✓ | ✓ |
| FL Studio | ✓ | - |
| Logic Pro | - | ✓ |
