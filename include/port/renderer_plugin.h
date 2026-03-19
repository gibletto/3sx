#ifndef RENDERER_PLUGIN_H
#define RENDERER_PLUGIN_H

/**
 * Renderer Plugin System
 *
 * Function-pointer-table plugin architecture for swappable rendering via DLL/shared library.
 * The base code defines the interface; a plugin DLL implements it.
 * If no plugin is loaded, all function pointers are NULL and the
 * base code renders normally using the standard tile-based path.
 */

#include <SDL3/SDL.h>
#include <stdbool.h>

#define RENDERER_PLUGIN_API_VERSION 1

/* ================================================================
 * renderer_export_t — Functions the plugin DLL provides
 * ================================================================ */
typedef struct renderer_export_t {
    int api_version;

    /* Lifecycle — receives argc/argv so the plugin can parse its own args */
    bool (*Init)(SDL_Renderer* renderer, int argc, const char** argv);
    void (*Shutdown)(void);

    /* Configuration — set by the plugin, read by the base code after Init. */
    int render_scale; /* Desired canvas scale (e.g. 4 for HD). */

    /**
     * Try to render a full-sprite override for the given group/cg.
     * Loads the override texture and pushes it to the render queue if found.
     *
     * @param group_index  Texture group index.
     * @param cg_number    CG animation frame number.
     * @param screen_x     Screen-space X position (post-transform).
     * @param screen_y     Screen-space Y position (post-transform).
     * @param z            Z depth for sorting.
     * @param flip_x       Non-zero to flip horizontally.
     * @param color        ARGB vertex color.
     * @return true if an override was rendered, false to fall through to standard rendering.
     */
    bool (*TryRenderSprite)(int group_index, int cg_number, float screen_x, float screen_y, float z, int flip_x,
                            unsigned int color);

    /* Background tile overrides */
    SDL_Texture* (*LoadBGTileOverride)(int gbix);
    void (*DrawBGTile)(SDL_Texture* texture, const void* scrDrawPos, unsigned int vtxCol);
    void (*ClearBGTileCache)(void);

} renderer_export_t;

/* ================================================================
 * renderer_import_t — Functions/data the base code provides to the DLL
 * ================================================================ */
typedef struct renderer_import_t {
    /* Logging */
    void (*Log)(const char* fmt, ...);

    /* Coordinate conversion (wraps flPS2ConvScreenFZ) */
    float (*ConvScreenFZ)(float z);

    /**
     * Push a render task into the base code's render queue.
     * This allows the plugin's HD sprites to z-sort correctly
     * with standard sprites.
     *
     * @param texture   SDL texture to render.
     * @param vertices  4 SDL_Vertex for the quad (positions, UVs, colors).
     * @param z         Z depth for sorting.
     */
    void (*PushRenderTask)(SDL_Texture* texture, const SDL_Vertex vertices[4], float z);

    /* Constants */
    int cps3_width;
    int cps3_height;
} renderer_import_t;

/* ================================================================
 * DLL entry point signature
 * ================================================================ */
typedef renderer_export_t* (*GetRendererAPI_fn)(const renderer_import_t* import);

/* ================================================================
 * Plugin loader API (base code side)
 * ================================================================ */

/** Global plugin state. NULL when no plugin is loaded. */
extern renderer_export_t* g_renderer_plugin;

/** Returns true if a renderer plugin is loaded and active. */
#define RENDERER_HAS_PLUGIN() (g_renderer_plugin != NULL)

/**
 * Attempt to load a renderer plugin DLL.
 *
 * @param plugin_name   Plugin name (e.g. "renderer_hd"). Loads lib<name>.dll/.so next to executable.
 * @param renderer      The SDL_Renderer to pass to the plugin.
 * @param argc          Original argument count from main.
 * @param argv          Original argument values from main (unmodified copy).
 * @return true if the plugin was loaded and initialized successfully.
 */
bool RendererPlugin_Load(const char* plugin_name, SDL_Renderer* renderer, int argc, const char** argv);

/** Unload the current renderer plugin, if any. */
void RendererPlugin_Unload(void);

#endif /* RENDERER_PLUGIN_H */
