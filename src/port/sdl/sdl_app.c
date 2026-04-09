#include "port/sdl/sdl_app.h"
#include "common.h"
#include "imgui/imgui_wrapper.h"
#include "port/config/config.h"
#include "port/config/keymap.h"
#include "port/host_context.h"
#include "port/input_backend.h"
#include "port/render_backend.h"
#include "port/sdl/netplay_screen.h"
#include "port/sdl/netstats_renderer.h"
#include "port/sdl/sdl_debug_text.h"
#include "port/sdl/sdl_message_renderer.h"
#include "port/sound/adx.h"
#include "sf33rd/AcrSDK/ps2/foundaps2.h"

#include <SDL3/SDL.h>

static const char* app_name = "Street Fighter III: 3rd Strike";
static const float display_target_ratio = 4.0f / 3.0f;
static const int window_min_width = 384;
static const int window_min_height = (int)(window_min_width / display_target_ratio);
static const Uint64 target_frame_time_ns = 1000000000.0 / TARGET_FPS;

SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static PlatformHostContext host_context = { 0 };

static Uint64 frame_deadline = 0;
static FrameMetrics frame_metrics = { 0 };
static Uint64 last_frame_end_time = 0;

static bool should_save_screenshot = false;
static Uint64 last_mouse_motion_time = 0;
static const int mouse_hide_delay_ms = 2000; // 2 seconds

static bool init_window() {
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (Config_GetBool(CFG_KEY_FULLSCREEN)) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }

    int window_width = Config_GetInt(CFG_KEY_WINDOW_WIDTH);

    if (window_width < window_min_width) {
        window_width = window_min_width;
    }

    int window_height = Config_GetInt(CFG_KEY_WINDOW_HEIGHT);

    if (window_height < window_min_height) {
        window_height = window_min_height;
    }

    if (!SDL_CreateWindowAndRenderer(app_name, window_width, window_height, window_flags, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    host_context.backend_kind = PLATFORM_HOST_BACKEND_SDL;
    host_context.window = window;
    host_context.renderer = renderer;
    return true;
}

int SDLApp_PreInit() {
    SDL_SetAppMetadata(app_name, "0.1", NULL);
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return 1;
    }

    return 0;
}

int SDLApp_FullInit() {
    Config_Init();
    Keymap_Init();

    if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return 1;
    }

    if (!init_window()) {
        SDL_Log("Couldn't initialize SDL window: %s", SDL_GetError());
        return 1;
    }

    // Initialize rendering subsystems
    SDLMessageRenderer_Initialize(renderer);
    g_render_backend.init(&host_context);

#if DEBUG
    SDLDebugText_Initialize(renderer);
#endif

    // Initialize pads
    InputBackend_Init();

#if DEBUG
    ImGuiW_Init(window, renderer);
#endif

    return 0;
}

void SDLApp_Quit() {
    Config_Destroy();
    g_render_backend.shutdown();
    SDLMessageRenderer_Shutdown();

#if DEBUG
    ImGuiW_Finish();
#endif

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    renderer = NULL;
    window = NULL;
    host_context.backend_kind = PLATFORM_HOST_BACKEND_NONE;
    host_context.window = NULL;
    host_context.renderer = NULL;
    SDL_Quit();
}

// static void set_screenshot_flag_if_needed(SDL_KeyboardEvent* event) {
//     if ((event->key == SDLK_GRAVE) && event->down && !event->repeat) {
//         should_save_screenshot = true;
//     }
// }

#if DEBUG
static void toggle_debug_window_visibility(SDL_KeyboardEvent* event) {
    if ((event->key == SDLK_GRAVE) && event->down && !event->repeat) {
        ImGuiW_ToggleVisivility();
    }
}
#endif

static void handle_fullscreen_toggle(SDL_KeyboardEvent* event) {
    const bool is_alt_enter = (event->key == SDLK_RETURN) && (event->mod & SDL_KMOD_ALT);
    const bool is_f11 = (event->key == SDLK_F11);
    const bool correct_key = (is_alt_enter || is_f11);

    if (!correct_key || !event->down || event->repeat) {
        return;
    }

    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);

    if (flags & SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(window, false);
    } else {
        SDL_SetWindowFullscreen(window, true);
    }
}

static void handle_mouse_motion() {
    last_mouse_motion_time = SDL_GetTicks();
    SDL_ShowCursor();
}

static void hide_cursor_if_needed() {
    const Uint64 now = SDL_GetTicks();

    if ((last_mouse_motion_time > 0) && ((now - last_mouse_motion_time) > mouse_hide_delay_ms)) {
        SDL_HideCursor();
    }
}

bool SDLApp_PollEvents() {
    SDL_Event event;
    bool continue_running = true;

    while (SDL_PollEvent(&event)) {
#if DEBUG
        ImGuiW_ProcessEvent(&event);
#endif

        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            InputBackend_HandleGamepadDeviceEvent(&event.gdevice);
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            // set_screenshot_flag_if_needed(&event.key);

#if DEBUG
            toggle_debug_window_visibility(&event.key);
#endif

            handle_fullscreen_toggle(&event.key);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            handle_mouse_motion();
            break;

        case SDL_EVENT_QUIT:
            continue_running = false;
            break;
        }
    }

    return continue_running;
}

void SDLApp_BeginFrame() {
    // Clear window
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_RenderClear(renderer);

    SDLMessageRenderer_BeginFrame();
    g_render_backend.begin_frame();

#if DEBUG
    ImGuiW_BeginFrame();
#endif
}

static void update_metrics(Uint64 sleep_time) {
    const Uint64 new_frame_end_time = SDL_GetTicksNS();
    const Uint64 frame_time = new_frame_end_time - last_frame_end_time;
    const float frame_time_ms = (float)frame_time / 1e6;

    frame_metrics.frame_time[frame_metrics.head] = frame_time_ms;
    frame_metrics.idle_time[frame_metrics.head] = (float)sleep_time / 1e6;
    frame_metrics.fps[frame_metrics.head] = 1000 / frame_time_ms;

    frame_metrics.head = (frame_metrics.head + 1) % SDL_arraysize(frame_metrics.frame_time);
    last_frame_end_time = new_frame_end_time;
}

static void save_texture(SDL_Texture* texture, const char* filename) {
    SDL_SetRenderTarget(renderer, texture);
    const SDL_Surface* rendered_surface = SDL_RenderReadPixels(renderer, NULL);
    SDL_SaveBMP(rendered_surface, filename);
    SDL_DestroySurface(rendered_surface);
}

void SDLApp_EndFrame() {
    const RenderBackendCapabilities* render_caps = g_render_backend.capabilities;

    // Run sound processing
    ADX_ProcessTracks();

    // Render

    // This should come before SDLGameRenderer_RenderFrame,
    // because NetstatsRenderer uses the existing SFIII rendering pipeline
    NetplayScreen_Render();
    NetstatsRenderer_Render();
    g_render_backend.render_frame();

    if (should_save_screenshot && render_caps != NULL && render_caps->has_canvas_handle) {
        SDL_Texture* scene_canvas = g_render_backend.get_canvas_handle();

        if (scene_canvas != NULL) {
            save_texture(scene_canvas, "screenshot_cps3.bmp");
        }
    }

    g_render_backend.end_frame();
    g_render_backend.present();

    if (should_save_screenshot && renderer != NULL) {
        const SDL_Surface* rendered_surface = SDL_RenderReadPixels(renderer, NULL);
        SDL_SaveBMP(rendered_surface, "screenshot_screen.bmp");
        SDL_DestroySurface((SDL_Surface*)rendered_surface);
    }

#if DEBUG
    // Render debug text
    SDLDebugText_Render();

    ImGuiW_EndFrame(renderer);
#endif

    SDL_RenderPresent(renderer);

    should_save_screenshot = false;

    // Handle cursor hiding
    hide_cursor_if_needed();

    // Do frame pacing
    Uint64 now = SDL_GetTicksNS();

    if (frame_deadline == 0) {
        frame_deadline = now + target_frame_time_ns;
    }

    Uint64 sleep_time = 0;

    if (now < frame_deadline) {
        sleep_time = frame_deadline - now;
        SDL_DelayNS(sleep_time);
        now = SDL_GetTicksNS();
    }

    frame_deadline += target_frame_time_ns;

    // If we fell behind by more than one frame, resync to avoid spiraling
    if (now > frame_deadline + target_frame_time_ns) {
        frame_deadline = now + target_frame_time_ns;
    }

    // Measure
    update_metrics(sleep_time);
}

void SDLApp_Exit() {
    SDL_Event quit_event;
    quit_event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_event);
}

const FrameMetrics* SDLApp_GetFrameMetrics() {
    return &frame_metrics;
}
