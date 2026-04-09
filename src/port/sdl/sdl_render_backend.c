#include "port/render_backend.h"

#include "port/config/config.h"
#include "port/sdl/scanline_renderer.h"
#include "port/sdl/sdl_game_renderer.h"
#include "port/sdl/sdl_message_renderer.h"

#include <SDL3/SDL.h>

typedef enum ScaleMode {
    SCALEMODE_NEAREST,
    SCALEMODE_LINEAR,
    SCALEMODE_SOFT_LINEAR,
    SCALEMODE_SQUARE_PIXELS,
    SCALEMODE_INTEGER,
} ScaleMode;

static const float display_target_ratio = 4.0f / 3.0f;

static SDL_Renderer* renderer = NULL;
static SDL_Texture* screen_texture = NULL;
static ScaleMode scale_mode = SCALEMODE_SOFT_LINEAR;

static const RenderBackendCapabilities render_backend_capabilities = {
    .backend_kind = PLATFORM_HOST_BACKEND_SDL,
    .native_present = false,
    .has_canvas_handle = true,
};

static SDL_ScaleMode screen_texture_scale_mode() {
    switch (scale_mode) {
    case SCALEMODE_LINEAR:
    case SCALEMODE_SOFT_LINEAR:
        return SDL_SCALEMODE_LINEAR;

    case SCALEMODE_NEAREST:
    case SCALEMODE_SQUARE_PIXELS:
    case SCALEMODE_INTEGER:
        return SDL_SCALEMODE_NEAREST;
    default:
        return SDL_SCALEMODE_INVALID;
    }
}

static SDL_Point screen_texture_size() {
    SDL_Point size = { 0 };
    SDL_GetRenderOutputSize(renderer, &size.x, &size.y);

    if (scale_mode == SCALEMODE_SOFT_LINEAR) {
        size.x *= 2;
        size.y *= 2;
    }

    return size;
}

static void create_screen_texture() {
    const SDL_Point size = screen_texture_size();

    if (screen_texture != NULL && screen_texture->w == size.x && screen_texture->h == size.y) {
        return;
    }

    SDL_DestroyTexture(screen_texture);
    screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB32, SDL_TEXTUREACCESS_TARGET, size.x, size.y);
    SDL_SetTextureScaleMode(screen_texture, screen_texture_scale_mode());
}

static void init_scalemode() {
    const char* raw_scalemode = Config_GetString(CFG_KEY_SCALEMODE);

    if (raw_scalemode == NULL) {
        return;
    }

    if (SDL_strcmp(raw_scalemode, "nearest") == 0) {
        scale_mode = SCALEMODE_NEAREST;
    } else if (SDL_strcmp(raw_scalemode, "linear") == 0) {
        scale_mode = SCALEMODE_LINEAR;
    } else if (SDL_strcmp(raw_scalemode, "soft-linear") == 0) {
        scale_mode = SCALEMODE_SOFT_LINEAR;
    } else if (SDL_strcmp(raw_scalemode, "square-pixels") == 0) {
        scale_mode = SCALEMODE_SQUARE_PIXELS;
    } else if (SDL_strcmp(raw_scalemode, "integer") == 0) {
        scale_mode = SCALEMODE_INTEGER;
    }
}

static void center_rect(SDL_FRect* rect, int win_w, int win_h) {
    rect->x = (win_w - rect->w) / 2;
    rect->y = (win_h - rect->h) / 2;
}

static SDL_FRect fit_4_by_3_rect(int win_w, int win_h) {
    SDL_FRect rect;
    rect.w = win_w;
    rect.h = win_w / display_target_ratio;

    if (rect.h > win_h) {
        rect.h = win_h;
        rect.w = win_h * display_target_ratio;
    }

    center_rect(&rect, win_w, win_h);
    return rect;
}

static SDL_FRect fit_integer_rect(int win_w, int win_h, int pixel_w, int pixel_h) {
    const int virtual_w = win_w / pixel_w;
    const int virtual_h = win_h / pixel_h;
    const int scale_w = virtual_w / 384;
    const int scale_h = virtual_h / 224;
    int scale = (scale_h < scale_w) ? scale_h : scale_w;

    if (scale < 1) {
        scale = 1;
    }

    SDL_FRect rect;
    rect.w = scale * 384 * pixel_w;
    rect.h = scale * 224 * pixel_h;
    center_rect(&rect, win_w, win_h);
    return rect;
}

static SDL_FRect get_letterbox_rect(int win_w, int win_h) {
    switch (scale_mode) {
    case SCALEMODE_NEAREST:
    case SCALEMODE_LINEAR:
    case SCALEMODE_SOFT_LINEAR:
        return fit_4_by_3_rect(win_w, win_h);

    case SCALEMODE_INTEGER:
        return fit_integer_rect(win_w, win_h, 7, 9);

    case SCALEMODE_SQUARE_PIXELS:
        return fit_integer_rect(win_w, win_h, 1, 1);

    default:
        return fit_4_by_3_rect(win_w, win_h);
    }
}

static void SDLRenderBackend_Init(const PlatformHostContext* host_context) {
    renderer = NULL;

    if (host_context != NULL) {
        renderer = host_context->renderer;
    }

    init_scalemode();
    create_screen_texture();
    ScanlineRenderer_Init(renderer);
    SDLGameRenderer_Init(host_context);
}

static void SDLRenderBackend_Shutdown() {
    SDLGameRenderer_Shutdown();
    ScanlineRenderer_Destroy();
    SDL_DestroyTexture(screen_texture);
    screen_texture = NULL;
    renderer = NULL;
}

static void SDLRenderBackend_Present() {
    if (renderer == NULL) {
        return;
    }

    create_screen_texture();

    SDL_Texture* scene_canvas = SDLGameRenderer_GetCanvasHandle();

    if (screen_texture == NULL || scene_canvas == NULL) {
        return;
    }

    SDL_SetRenderTarget(renderer, screen_texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    const SDL_FRect dst_rect = get_letterbox_rect(screen_texture->w, screen_texture->h);
    SDL_RenderTexture(renderer, scene_canvas, NULL, &dst_rect);

    if (message_canvas != NULL) {
        SDL_RenderTexture(renderer, message_canvas, NULL, &dst_rect);
    }

    SDL_SetRenderTarget(renderer, NULL);
    SDL_RenderTexture(renderer, screen_texture, NULL, NULL);

    int win_w = 0;
    int win_h = 0;
    SDL_GetRenderOutputSize(renderer, &win_w, &win_h);
    const SDL_FRect game_rect = get_letterbox_rect(win_w, win_h);
    ScanlineRenderer_Render(&game_rect);
}

const RenderBackendOps g_render_backend = {
    .capabilities = &render_backend_capabilities,
    .init = SDLRenderBackend_Init,
    .shutdown = SDLRenderBackend_Shutdown,
    .begin_frame = SDLGameRenderer_BeginFrame,
    .render_frame = SDLGameRenderer_RenderFrame,
    .end_frame = SDLGameRenderer_EndFrame,
    .present = SDLRenderBackend_Present,
    .get_canvas_handle = SDLGameRenderer_GetCanvasHandle,
    .create_texture = SDLGameRenderer_CreateTexture,
    .destroy_texture = SDLGameRenderer_DestroyTexture,
    .unlock_texture = SDLGameRenderer_UnlockTexture,
    .create_palette = SDLGameRenderer_CreatePalette,
    .destroy_palette = SDLGameRenderer_DestroyPalette,
    .unlock_palette = SDLGameRenderer_UnlockPalette,
    .set_texture = SDLGameRenderer_SetTexture,
    .draw_textured_quad = SDLGameRenderer_DrawTexturedQuad,
    .draw_sprite = SDLGameRenderer_DrawSprite,
    .draw_sprite2 = SDLGameRenderer_DrawSprite2,
    .draw_solid_quad = SDLGameRenderer_DrawSolidQuad,
};
