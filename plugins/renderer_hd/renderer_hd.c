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
#include <string.h>

/* ================================================================
 * Plugin state
 * ================================================================ */

static const renderer_import_t* g_import = NULL;
static SDL_Renderer* g_renderer = NULL;
static char g_sprites_path[512] = { 0 };

#define TEXTURE_SCALE 4

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

    int w, h, channels;
    unsigned char* pixels = stbi_load(path, &w, &h, &channels, 4);

    if (pixels == NULL) {
        snprintf(path, sizeof(path), "%s/sprite_%d.png", g_sprites_path, cg_number);
        pixels = stbi_load(path, &w, &h, &channels, 4);
    }

    SDL_Texture* tex = NULL;

    if (pixels != NULL) {
        SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);

        if (surface != NULL) {
            tex = SDL_CreateTextureFromSurface(g_renderer, surface);
            SDL_DestroySurface(surface);

            if (tex != NULL) {
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            }
        }

        stbi_image_free(pixels);
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
 * PushHDSprite — push into base code's render queue via import
 * ================================================================ */

static void hd_PushHDSprite(SDL_Texture* texture, float x0, float y0, float x1, float y1, float z, int flip_x,
                            int flip_y, unsigned int color) {
    float tex_w, tex_h;
    SDL_GetTextureSize(texture, &tex_w, &tex_h);

    float origin_x = x0 * TEXTURE_SCALE;
    float origin_y = y0 * TEXTURE_SCALE;

    float sx0 = origin_x - tex_w * x1;
    float sx1 = origin_x + tex_w * (1.0f - x1);
    float sy0 = origin_y - tex_h * y1;
    float sy1 = origin_y + tex_h * (1.0f - y1);

    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;

    if (flip_x) {
        float tmp = u0;
        u0 = u1;
        u1 = tmp;
    }
    if (flip_y) {
        float tmp = v0;
        v0 = v1;
        v1 = tmp;
    }

    const SDL_FColor fcolor = { .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f };

    SDL_Vertex vertices[4];
    SDL_zeroa(vertices);

    vertices[0].position.x = sx0;
    vertices[0].position.y = sy0;
    vertices[0].tex_coord.x = u0;
    vertices[0].tex_coord.y = v0;
    vertices[0].color = fcolor;

    vertices[1].position.x = sx1;
    vertices[1].position.y = sy0;
    vertices[1].tex_coord.x = u1;
    vertices[1].tex_coord.y = v0;
    vertices[1].color = fcolor;

    vertices[2].position.x = sx0;
    vertices[2].position.y = sy1;
    vertices[2].tex_coord.x = u0;
    vertices[2].tex_coord.y = v1;
    vertices[2].color = fcolor;

    vertices[3].position.x = sx1;
    vertices[3].position.y = sy1;
    vertices[3].tex_coord.x = u1;
    vertices[3].tex_coord.y = v1;
    vertices[3].color = fcolor;

    g_import->PushRenderTask(texture, vertices, g_import->ConvScreenFZ(z));
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

    int w, h, channels;
    unsigned char* pixels = stbi_load(path, &w, &h, &channels, 4);
    SDL_Texture* tex = NULL;

    if (pixels != NULL) {
        SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);

        if (surface != NULL) {
            tex = SDL_CreateTextureFromSurface(g_renderer, surface);
            SDL_DestroySurface(surface);

            if (tex != NULL) {
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            }
        }

        stbi_image_free(pixels);
    }

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

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--sprites-path") == 0) {
            snprintf(g_sprites_path, sizeof(g_sprites_path), "%s", argv[i + 1]);
            break;
        }
    }

    if (g_sprites_path[0] == '\0') {
        g_import->Log("No --sprites-path specified");
        return false;
    }

    return true;
}

static void hd_Shutdown(void) {
    g_renderer = NULL;
    g_sprites_path[0] = '\0';
}

/* ================================================================
 * Export table
 * ================================================================ */

static renderer_export_t g_exports = {
    .api_version = RENDERER_PLUGIN_API_VERSION,
    .Init = hd_Init,
    .Shutdown = hd_Shutdown,
    .render_scale = 4,
    .LoadFullSpriteOverride = hd_LoadFullSpriteOverride,
    .PushHDSprite = hd_PushHDSprite,
    .LoadBGTileOverride = hd_LoadBGTileOverride,
    .DrawBGTile = hd_DrawBGTile,
    .ClearBGTileCache = hd_ClearBGTileCache,
};

/* ================================================================
 * DLL entry point
 * ================================================================ */

EXPORT renderer_export_t* GetRendererAPI(const renderer_import_t* import) {
    g_import = import;
    return &g_exports;
}
