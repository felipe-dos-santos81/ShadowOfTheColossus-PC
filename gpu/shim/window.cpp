// bbport: SDL3 window for the Vulkan swapchain (X11, Wayland, Win32 or a CAMetalLayer on macOS).
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <SDL3/SDL_metal.h>
#endif
#include "common/assert.h"
#include "common/logging/log.h"
#include "sdl_window.h"
#include "window_scale.h"
#include "bbport_overlay.h"
#include "video_core/renderdoc.h"

namespace Frontend {

// sotc: mouse cursor is never locked or grabbed to the window.
namespace {
std::atomic<float> g_mouse_dx{0}, g_mouse_dy{0};
std::atomic<u32> g_mouse_buttons{0};
std::atomic<bool> g_mouse_captured{false};
} // namespace

WindowSDL::WindowSDL(s32 width_, s32 height_, const char* title) : width{width_}, height{height_} {
    // Gamepads are sampled by runtime_pad.c; their events are pumped here with the window's.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        UNREACHABLE_MSG("Failed to initialize SDL video: {}", SDL_GetError());
    }
    const char* fullscreen = std::getenv("BB_FULLSCREEN");
    const bool is_fullscreen = fullscreen && fullscreen[0] == '1';
    if (!is_fullscreen) {
        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)) {
            const WindowSize scaled = ScaleWindowToDisplay({width_, height_},
                                                           {usable.w, usable.h});
            width_ = scaled.width;
            height_ = scaled.height;
        }
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width_);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height_);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
#ifdef __APPLE__
    // The Vulkan surface comes from the window's CAMetalLayer (VK_EXT_metal_surface, below), not
    // from SDL's Vulkan support, which the Cocoa driver may lack.
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN, true);
#else
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
#endif
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, is_fullscreen);
    base_title = title;
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    ASSERT_MSG(window, "Failed to create window: {}", SDL_GetError());

    // Ensure the mouse cursor is never locked or grabbed to the application window.
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_SetWindowMouseGrab(window, false);
    SDL_ShowCursor();
    // The acceptance gate (tools/macos_check.py) reads this line. It is written straight to
    // stderr, not through the logging system: an INFO record is filtered by the GPU log's
    // threshold (BB_GPU_LOG, `warning` by default), so writing it here keeps the gate reading it
    // whatever threshold the run uses. This reports the state SDL actually holds, so "windowed
    // with the cursor free" is checked on the live window.
    std::fprintf(stderr, "Window %dx%d %s: relative mouse %s, grab %s\n", width_, height_,
                 (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? "fullscreen" : "windowed",
                 SDL_GetWindowRelativeMouseMode(window) ? "on" : "off",
                 SDL_GetWindowMouseGrab(window) ? "on" : "off");

    const char* driver = SDL_GetCurrentVideoDriver();
    const SDL_PropertiesID wp = SDL_GetWindowProperties(window);
#ifdef _WIN32
    if (driver && !std::strcmp(driver, "windows")) {
        window_info.type = WindowSystemType::Windows;
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    } else
#elif defined(__APPLE__)
    if (driver && !std::strcmp(driver, "cocoa")) {
        // vk_platform.cpp builds the VK_EXT_metal_surface from this layer.
        metal_view = SDL_Metal_CreateView(window);
        ASSERT_MSG(metal_view, "Failed to create the Metal view: {}", SDL_GetError());
        window_info.type = WindowSystemType::Metal;
        window_info.render_surface = SDL_Metal_GetLayer(metal_view);
    } else
#endif
    if (driver && !std::strcmp(driver, "x11")) {
        window_info.type = WindowSystemType::X11;
        window_info.display_connection = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        window_info.render_surface = reinterpret_cast<void*>(SDL_GetNumberProperty(wp, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    } else if (driver && !std::strcmp(driver, "wayland")) {
        window_info.type = WindowSystemType::Wayland;
        window_info.display_connection = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        UNREACHABLE_MSG("Unsupported SDL video driver {}", driver ? driver : "(none)");
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    width = w;
    height = h;
    LOG_INFO(Frontend, "Window {}x{} on {}", w, h, driver);
}

WindowSDL::~WindowSDL() {
#ifdef __APPLE__
    if (metal_view) SDL_Metal_DestroyView(metal_view);
#endif
    SDL_DestroyWindow(window);
}

void WindowSDL::BeginTextInput(const std::string& initial, const std::string& prompt) {
    std::scoped_lock lock{text_mutex};
    text = initial;
    text_prompt = prompt;
    text_state = 0;
    text_requested = true;
}

int WindowSDL::PollTextInput(std::string& out) {
    std::scoped_lock lock{text_mutex};
    out = text;
    return text_state;
}

void WindowSDL::UpdateTextTitle() {
    const std::string title = text_active ? base_title + " \u2014 " + text_prompt + ": " + text + "_  (Enter = OK, Esc = cancel)"
                                          : base_title;
    SDL_SetWindowTitle(window, title.c_str());
    BbOverlay::SetTextEntry(text_active, text_prompt, text);
}

bool WindowSDL::PollEvents() {
    {
        std::scoped_lock lock{text_mutex};
        if (text_requested) { // SDL text input must be toggled from the window thread
            text_requested = false;
            text_active = true;
            SDL_StartTextInput(window);
            UpdateTextTitle();
        }
    }
    if (!text_active) {
        BbOverlay::UpdateTextInput(window);
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (text_active && (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_KEY_DOWN)) {
            std::scoped_lock lock{text_mutex};
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                text += event.text.text;
            } else if (event.key.key == SDLK_BACKSPACE && !text.empty()) {
                size_t cut = text.size() - 1; // drop one UTF-8 code point
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
                text.erase(cut);
            } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_ESCAPE) {
                text_state = event.key.key == SDLK_ESCAPE ? 2 : 1;
                text_active = false;
                SDL_StopTextInput(window);
            }
            UpdateTextTitle();
            continue;
        }
        // The controller finishes the text dialog too: Cross (A) accepts, Circle (B) cancels.
        if (text_active && event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
             event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)) {
            std::scoped_lock lock{text_mutex};
            text_state = event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ? 1 : 2;
            text_active = false;
            SDL_StopTextInput(window);
            UpdateTextTitle();
            continue;
        }
        if (BbOverlay::HandleEvent(event)) {
            continue;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F12 && !event.key.repeat) {
            VideoCore::TriggerCapture(); // only does something when running under RenderDoc
        }
        switch (event.type) {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            width = w;
            height = h;
            break;
        }
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            is_open = false;
            break;
        default:
            break;
        }
    }
    return is_open;
}

} // namespace Frontend

// sotc: motion since the last call, the held buttons (bit = SDL button number); 0 while the
// mouse is not captured.
extern "C" int bbgpu_mouse_take(float* dx, float* dy, unsigned* buttons) {
    *dx = Frontend::g_mouse_dx.exchange(0, std::memory_order_relaxed);
    *dy = Frontend::g_mouse_dy.exchange(0, std::memory_order_relaxed);
    *buttons = Frontend::g_mouse_buttons.load(std::memory_order_relaxed);
    return Frontend::g_mouse_captured.load(std::memory_order_relaxed) ? 1 : 0;
}
