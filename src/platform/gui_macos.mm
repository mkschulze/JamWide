/*
    NINJAM CLAP Plugin - gui_macos.mm
    macOS GUI implementation using Cocoa + Metal + ImGui
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#include "gui_context.h"
#include "plugin/jamwide_plugin.h"
#include "ui/ui_main.h"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <memory>

#include "imgui.h"
#include "imgui_impl_osx.h"
#include "imgui_impl_metal.h"

using namespace jamwide;

//------------------------------------------------------------------------------
// NinjamView - MTKView subclass for Metal rendering
//------------------------------------------------------------------------------

@interface NinjamView : MTKView {
@public
    std::shared_ptr<JamWidePlugin> plugin_;
}
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, assign) ImGuiContext* imguiContext;
- (instancetype)initWithFrame:(NSRect)frame
                        plugin:(std::shared_ptr<JamWidePlugin>)plugin;
- (void)clearPlugin;
@end

@implementation NinjamView

- (NSView*)findTextInputView {
    for (NSView* subview in self.subviews) {
        if ([subview conformsToProtocol:@protocol(NSTextInputClient)]) {
            return subview;
        }
    }
    return nil;
}

- (instancetype)initWithFrame:(NSRect)frame
                        plugin:(std::shared_ptr<JamWidePlugin>)plugin {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    self = [super initWithFrame:frame device:device];

    if (self) {
        plugin_ = std::move(plugin);
        _commandQueue = [device newCommandQueue];

        self.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        self.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
        self.sampleCount = 1;
        self.clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
        self.enableSetNeedsDisplay = NO;
        self.preferredFramesPerSecond = 60;

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        _imguiContext = ImGui::CreateContext();
        ImGui::SetCurrentContext(_imguiContext);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;  // Don't save imgui.ini

        ImGui::StyleColorsDark();
        [self setupStyle];

        ImGui_ImplOSX_Init(self);
        ImGui_ImplMetal_Init(device);
    }

    return self;
}

- (void)clearPlugin {
    plugin_.reset();
}

- (void)dealloc {
    if (_imguiContext) {
        ImGui::SetCurrentContext(_imguiContext);
        ImGui_ImplMetal_Shutdown();
        ImGui_ImplOSX_Shutdown();
        ImGui::DestroyContext(_imguiContext);
        _imguiContext = nullptr;
    }
#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

- (void)setupStyle {
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

- (void)drawRect:(NSRect)dirtyRect {
    @autoreleasepool {
        // Guard: Must have valid ImGui context and plugin
        if (!_imguiContext || !plugin_) {
            fprintf(stderr, "[NinjamView] drawRect: early return - context=%p plugin=%p\n",
                    (void*)_imguiContext, (void*)plugin_.get());
            return;
        }
        ImGui::SetCurrentContext(_imguiContext);
        
        // Verify context was set
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        if (!ctx) {
            fprintf(stderr, "[NinjamView] drawRect: GetCurrentContext returned NULL!\n");
            return;
        }

        id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
        if (!commandBuffer) return;
        
        MTLRenderPassDescriptor* rpd = self.currentRenderPassDescriptor;
        if (rpd == nil) return;

        // Start ImGui frame
        ImGui_ImplMetal_NewFrame(rpd);
        ImGui_ImplOSX_NewFrame(self);
        ImGui::NewFrame();

        // Render UI
        ui_render_frame(plugin_.get());

        // Render ImGui
        ImGui::Render();

        id<MTLRenderCommandEncoder> encoder =
            [commandBuffer renderCommandEncoderWithDescriptor:rpd];
        if (!encoder) {
            [commandBuffer commit];
            return;
        }

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

- (BOOL)canBecomeKeyView {
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    return YES;
}

- (BOOL)becomeFirstResponder {
    NSView* input_view = [self findTextInputView];
    if (input_view && self.window) {
        [self.window makeFirstResponder:input_view];
        return YES;
    }
    return [super becomeFirstResponder];
}

- (void)mouseDown:(NSEvent*)event {
    NSView* input_view = [self findTextInputView];
    if (input_view && self.window) {
        [self.window makeFirstResponder:input_view];
    }
    [super mouseDown:event];
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];

    if (!self.window) {
        return;
    }

    // ImGui's OSX backend installs a hidden NSTextInputClient subview.
    // Make sure it becomes first responder so text input works.
    NSView* input_view = [self findTextInputView];
    if (input_view) {
        [self.window makeFirstResponder:input_view];
    }
}

@end

//------------------------------------------------------------------------------
// GuiContextMacOS - Platform implementation
//------------------------------------------------------------------------------

namespace jamwide {

class GuiContextMacOS : public GuiContext {
public:
    explicit GuiContextMacOS(std::shared_ptr<JamWidePlugin> plugin)
        : view_(nil)
    {
        plugin_ = std::move(plugin);
    }

    ~GuiContextMacOS() override {
        if (view_) {
            [view_ clearPlugin];
            [view_ setPaused:YES];
            [view_ removeFromSuperview];
            view_ = nil;
        }
    }

    bool set_parent(void* parent_handle) override {
        NSView* parent = (__bridge NSView*)parent_handle;
        if (!parent) return false;

        NSRect frame = NSMakeRect(0, 0, width_, height_);
        view_ = [[NinjamView alloc] initWithFrame:frame plugin:plugin_];

        if (!view_) return false;

        [view_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
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
        if (view_ && [view_ imguiContext]) {
            ImGui::SetCurrentContext([view_ imguiContext]);
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
        // MTKView handles rendering via drawRect: at preferredFramesPerSecond
    }

private:
    NinjamView* view_;
};

GuiContext* create_gui_context_macos(std::shared_ptr<JamWidePlugin> plugin) {
    return new GuiContextMacOS(std::move(plugin));
}

} // namespace jamwide
