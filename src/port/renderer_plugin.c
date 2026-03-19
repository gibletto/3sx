#include "port/renderer_plugin.h"
#include "port/sdl/sdl_game_renderer.h"
#include "sf33rd/AcrSDK/ps2/flps2render.h"

#include <SDL3/SDL.h>
#include <stdarg.h>

/* ================================================================
 * Global state
 * ================================================================ */

renderer_export_t* g_renderer_plugin = NULL;

static SDL_SharedObject* plugin_handle = NULL;

/* ================================================================
 * Import function implementations
 * ================================================================ */

static void import_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    SDL_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    SDL_Log("[renderer] %s", buf);
}

static float import_conv_screen_fz(float z) {
    return flPS2ConvScreenFZ(z);
}

/* ================================================================
 * Plugin loading
 * ================================================================ */

bool RendererPlugin_Load(const char* plugin_name, SDL_Renderer* renderer, int argc, const char** argv) {
    if (g_renderer_plugin != NULL) {
        SDL_Log("Renderer plugin already loaded");
        return true;
    }

    /* Build DLL path from plugin name */
    char dll_path[512];
    const char* base = SDL_GetBasePath();
#ifdef _WIN32
    SDL_snprintf(dll_path, sizeof(dll_path), "%slib%s.dll", base ? base : "", plugin_name);
#else
    SDL_snprintf(dll_path, sizeof(dll_path), "%slib%s.so", base ? base : "", plugin_name);
#endif

    SDL_Log("Loading renderer plugin: %s", dll_path);

    /* Load the shared library */
    plugin_handle = SDL_LoadObject(dll_path);
    if (plugin_handle == NULL) {
        SDL_Log("Renderer plugin not found: %s (%s)", dll_path, SDL_GetError());
        return false;
    }

    /* Look up the entry point */
    GetRendererAPI_fn get_api = (GetRendererAPI_fn)SDL_LoadFunction(plugin_handle, "GetRendererAPI");
    if (get_api == NULL) {
        SDL_Log("Renderer plugin missing GetRendererAPI: %s", SDL_GetError());
        SDL_UnloadObject(plugin_handle);
        plugin_handle = NULL;
        return false;
    }

    /* Build the import table (static so it outlives this function) */
    static renderer_import_t import = { 0 };
    import.Log = import_log;
    import.ConvScreenFZ = import_conv_screen_fz;
    import.PushRenderTask = SDLGameRenderer_PushRenderTaskFromPlugin;
    import.cps3_width = 384;
    import.cps3_height = 224;

    /* Call the DLL entry point */
    renderer_export_t* exports = get_api(&import);
    if (exports == NULL) {
        SDL_Log("Renderer plugin GetRendererAPI returned NULL");
        SDL_UnloadObject(plugin_handle);
        plugin_handle = NULL;
        return false;
    }

    /* Version check */
    if (exports->api_version != RENDERER_PLUGIN_API_VERSION) {
        SDL_Log("Renderer plugin API version mismatch: got %d, expected %d",
                exports->api_version,
                RENDERER_PLUGIN_API_VERSION);
        SDL_UnloadObject(plugin_handle);
        plugin_handle = NULL;
        return false;
    }

    /* Initialize the plugin — it receives argc/argv to parse its own args */
    if (exports->Init && !exports->Init(renderer, argc, argv)) {
        SDL_Log("Renderer plugin Init failed");
        SDL_UnloadObject(plugin_handle);
        plugin_handle = NULL;
        return false;
    }

    g_renderer_plugin = exports;
    SDL_Log("Renderer plugin loaded successfully");
    return true;
}

void RendererPlugin_Unload(void) {
    if (g_renderer_plugin != NULL) {
        if (g_renderer_plugin->Shutdown) {
            g_renderer_plugin->Shutdown();
        }
        g_renderer_plugin = NULL;
    }

    if (plugin_handle != NULL) {
        SDL_UnloadObject(plugin_handle);
        plugin_handle = NULL;
    }
}
