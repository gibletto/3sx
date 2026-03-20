/**
 * renderer_hd.dll — HD sprite/tile override renderer plugin for 3SX.
 *
 * Implements the renderer_export_t interface. Loads PNG sprite overrides
 * from a configurable directory and renders them at high resolution.
 */

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#include "port/renderer_plugin.h"

#include <stb_image.h>

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Plugin state
 * ================================================================ */

static const renderer_import_t* g_import = NULL;
static SDL_Renderer* g_renderer = NULL;
static char g_sprites_path[512] = { 0 };
static int g_render_scale = 4;
static int g_sprite_scale = 4;
static float g_sprite_ratio = 1.0f; /* render_scale / sprite_scale */
static renderer_export_t g_exports;

#define TEXTURE_SCALE g_render_scale

/* ================================================================
 * Texture loading helper — loads PNG and downscales if render_scale < sprite_scale
 * ================================================================ */

static SDL_Texture* load_png_texture(const char* path) {
    int w, h, channels;
    unsigned char* pixels = stbi_load(path, &w, &h, &channels, 4);
    if (pixels == NULL)
        return NULL;

    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);
    if (surface == NULL) {
        stbi_image_free(pixels);
        return NULL;
    }

    /* Downscale at load time if render_scale < sprite_scale to save GPU memory */
    if (g_sprite_ratio < 1.0f && w > 1 && h > 1) {
        int new_w = (int)(w * g_sprite_ratio + 0.5f);
        int new_h = (int)(h * g_sprite_ratio + 0.5f);
        if (new_w < 1)
            new_w = 1;
        if (new_h < 1)
            new_h = 1;

        SDL_Surface* scaled = SDL_CreateSurface(new_w, new_h, SDL_PIXELFORMAT_RGBA32);
        if (scaled != NULL) {
            SDL_BlitSurfaceScaled(surface, NULL, scaled, NULL, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(surface);
            surface = scaled;
        }
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(g_renderer, surface);
    SDL_DestroySurface(surface);
    stbi_image_free(pixels);

    if (tex != NULL) {
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }

    return tex;
}

/* ================================================================
 * Full-sprite override cache
 * ================================================================ */

#define FULL_SPRITE_CACHE_MAX 4096
#define FULL_SPRITE_CACHE_MASK (FULL_SPRITE_CACHE_MAX - 1)

typedef struct {
    uint32_t key;
    SDL_Texture* texture;
    bool checked;
} FullSpriteCacheEntry;

static FullSpriteCacheEntry full_sprite_cache[FULL_SPRITE_CACHE_MAX] = { { 0 } };
static int full_sprite_cache_count = 0;

static SDL_Texture* hd_LoadFullSpriteOverride(int group_index, int cg_number) {
    if (g_sprites_path[0] == '\0')
        return NULL;
    const uint32_t key = (uint32_t)(group_index << 16) | (uint32_t)(cg_number & 0xFFFF);
    uint32_t slot = key & FULL_SPRITE_CACHE_MASK;

    for (int i = 0; i < 32; i++) {
        FullSpriteCacheEntry* entry = &full_sprite_cache[slot];

        if (!entry->checked) {
            break;
        }

        if (entry->key == key) {
            return entry->texture;
        }

        slot = (slot + 1) & FULL_SPRITE_CACHE_MASK;
    }

    /* Try loading: sprite_<group>_<cg>.png, then sprite_<cg>.png */
    char path[512];
    snprintf(path, sizeof(path), "%s/sprite_%d_%d.png", g_sprites_path, group_index, cg_number);

    SDL_Texture* tex = load_png_texture(path);

    if (tex == NULL) {
        snprintf(path, sizeof(path), "%s/sprite_%d.png", g_sprites_path, cg_number);
        tex = load_png_texture(path);
    }

    /* Cache */
    if (full_sprite_cache_count < FULL_SPRITE_CACHE_MAX / 2) {
        slot = key & FULL_SPRITE_CACHE_MASK;

        for (int i = 0; i < 32; i++) {
            FullSpriteCacheEntry* entry = &full_sprite_cache[slot];

            if (!entry->checked) {
                entry->key = key;
                entry->texture = tex;
                entry->checked = true;
                full_sprite_cache_count++;
                break;
            }

            slot = (slot + 1) & FULL_SPRITE_CACHE_MASK;
        }
    }

    return tex;
}

/* ================================================================
 * TryRenderSprite — load override + push to render queue in one call
 * ================================================================ */

static bool hd_TryRenderSprite(int group_index, int cg_number, float screen_x, float screen_y, float z, int flip_x,
                               unsigned int color) {
    SDL_Texture* texture = hd_LoadFullSpriteOverride(group_index, cg_number);
    if (texture == NULL)
        return false;

    float tex_w, tex_h;
    SDL_GetTextureSize(texture, &tex_w, &tex_h);

    float origin_x = screen_x * TEXTURE_SCALE;
    float origin_y = screen_y * TEXTURE_SCALE;

    float sx0 = origin_x;
    float sx1 = origin_x + tex_w;
    float sy0 = origin_y;
    float sy1 = origin_y + tex_h;

    float u0 = 0.0f, u1 = 1.0f;
    if (flip_x) {
        float tmp = u0;
        u0 = u1;
        u1 = tmp;
    }

    const SDL_FColor fcolor = { .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f };

    SDL_Vertex vertices[4];
    SDL_zeroa(vertices);

    vertices[0].position.x = sx0;
    vertices[0].position.y = sy0;
    vertices[0].tex_coord.x = u0;
    vertices[0].tex_coord.y = 0.0f;
    vertices[0].color = fcolor;

    vertices[1].position.x = sx1;
    vertices[1].position.y = sy0;
    vertices[1].tex_coord.x = u1;
    vertices[1].tex_coord.y = 0.0f;
    vertices[1].color = fcolor;

    vertices[2].position.x = sx0;
    vertices[2].position.y = sy1;
    vertices[2].tex_coord.x = u0;
    vertices[2].tex_coord.y = 1.0f;
    vertices[2].color = fcolor;

    vertices[3].position.x = sx1;
    vertices[3].position.y = sy1;
    vertices[3].tex_coord.x = u1;
    vertices[3].tex_coord.y = 1.0f;
    vertices[3].color = fcolor;

    g_import->PushRenderTask(texture, vertices, g_import->ConvScreenFZ(z));
    return true;
}

/* ================================================================
 * Background tile cache
 * ================================================================ */

#define BG_TILE_CACHE_MAX 1024
#define BG_TILE_CACHE_MASK (BG_TILE_CACHE_MAX - 1)

typedef struct {
    int gbix;
    SDL_Texture* texture;
    bool checked;
} BGTileCacheEntry;

static BGTileCacheEntry bg_tile_cache[BG_TILE_CACHE_MAX] = { { 0 } };

static void hd_ClearBGTileCache(void) {
    for (int i = 0; i < BG_TILE_CACHE_MAX; i++) {
        bg_tile_cache[i].checked = false;
    }
}

static SDL_Texture* hd_LoadBGTileOverride(int gbix) {
    uint32_t slot = (uint32_t)gbix & BG_TILE_CACHE_MASK;

    for (int i = 0; i < 16; i++) {
        BGTileCacheEntry* entry = &bg_tile_cache[slot];

        if (!entry->checked)
            break;
        if (entry->gbix == gbix)
            return entry->texture;
        slot = (slot + 1) & BG_TILE_CACHE_MASK;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/bg_%d.png", g_sprites_path, gbix);

    SDL_Texture* tex = load_png_texture(path);

    slot = (uint32_t)gbix & BG_TILE_CACHE_MASK;

    for (int i = 0; i < 16; i++) {
        BGTileCacheEntry* entry = &bg_tile_cache[slot];

        if (!entry->checked) {
            entry->gbix = gbix;
            entry->texture = tex;
            entry->checked = true;
            break;
        }

        slot = (slot + 1) & BG_TILE_CACHE_MASK;
    }

    return tex;
}

static void hd_DrawBGTile(SDL_Texture* texture, const void* scrDrawPos_raw, unsigned int vtxCol) {
    const float* pos = (const float*)scrDrawPos_raw;

    const float x0 = pos[0];
    const float y0 = pos[1];
    const float x1 = pos[5 * 3];
    const float y1 = pos[5 * 3 + 1];

    const float scale = TEXTURE_SCALE;
    SDL_FRect dst;
    dst.x = x0 * scale;
    dst.y = y0 * scale;
    dst.w = (x1 - x0) * scale;
    dst.h = (y1 - y0) * scale;

    if (vtxCol != 0 && vtxCol != 0xFFFFFFFF) {
        Uint8 r = (vtxCol >> 16) & 0xFF;
        Uint8 g_c = (vtxCol >> 8) & 0xFF;
        Uint8 b = vtxCol & 0xFF;
        SDL_SetTextureColorMod(texture, r, g_c, b);
    } else {
        SDL_SetTextureColorMod(texture, 255, 255, 255);
    }

    SDL_RenderTexture(g_renderer, texture, NULL, &dst);
}

/* ================================================================
 * Lifecycle
 * ================================================================ */

static bool hd_Init(SDL_Renderer* renderer, int argc, const char** argv) {
    g_renderer = renderer;

    int render_scale = 4;
    int sprite_scale = 0; /* 0 = auto (match render_scale) */

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--sprites-path") == 0) {
            snprintf(g_sprites_path, sizeof(g_sprites_path), "%s", argv[i + 1]);
        } else if (strcmp(argv[i], "--render-scale") == 0) {
            render_scale = atoi(argv[i + 1]);
            if (render_scale < 1)
                render_scale = 1;
            if (render_scale > 8)
                render_scale = 8;
        } else if (strcmp(argv[i], "--sprite-scale") == 0) {
            sprite_scale = atoi(argv[i + 1]);
            if (sprite_scale < 1)
                sprite_scale = 1;
            if (sprite_scale > 8)
                sprite_scale = 8;
        }
    }

    if (g_sprites_path[0] == '\0') {
        g_import->Log("No --sprites-path specified");
        return false;
    }

    if (sprite_scale == 0)
        sprite_scale = render_scale;

    g_render_scale = render_scale;
    g_sprite_scale = sprite_scale;
    g_sprite_ratio = (float)render_scale / (float)sprite_scale;
    g_exports.render_scale = render_scale;
    return true;
}

static void hd_Shutdown(void) {
    g_renderer = NULL;
    g_sprites_path[0] = '\0';
}

/* ================================================================
 * DLL entry point
 * ================================================================ */

EXPORT renderer_export_t* GetRendererAPI(const renderer_import_t* import) {
    g_import = import;

    g_exports.api_version = RENDERER_PLUGIN_API_VERSION;
    g_exports.Init = hd_Init;
    g_exports.Shutdown = hd_Shutdown;
    g_exports.render_scale = 4;
    g_exports.TryRenderSprite = hd_TryRenderSprite;
    g_exports.LoadBGTileOverride = hd_LoadBGTileOverride;
    g_exports.DrawBGTile = hd_DrawBGTile;
    g_exports.ClearBGTileCache = hd_ClearBGTileCache;

    return &g_exports;
}
