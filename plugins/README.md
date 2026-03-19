# Renderer Plugin System

3SX supports an optional renderer plugin that can override sprites, portraits, and background tiles with high-resolution PNG replacements at runtime.

## Architecture

The base code defines a plugin interface (`renderer_export_t` / `renderer_import_t`) in `include/port/renderer_plugin.h`. At startup, if `--renderer <name>` is specified, the base code loads `lib<name>.dll` (or `.so`) from the executable directory. The plugin receives an import table with functions it needs from the base code (logging, coordinate conversion, render task submission), and returns an export table with the override functions it implements.

If no `--renderer` flag is specified, the game renders normally with no overhead — all plugin call sites are guarded by a null pointer check.

## Building

The plugin builds automatically alongside the main executable:

```
cmake -S . -B build -G Ninja
cmake --build build
```

This produces `librenderer_hd.dll` (or `.so` on Linux) in the build directory, which is automatically copied next to the `3sx` executable.

## Usage

```
3sx --renderer renderer_hd --sprites-path /path/to/sprites
```

- `--renderer <name>` is handled by the base code — it loads `lib<name>.dll` (or `.so`) from the executable directory.
- `--sprites-path <path>` is parsed by the base code and forwarded to the plugin via the `extra_args` array in `Init`.

### File naming conventions

Place HD sprite PNGs in the sprites directory using these naming conventions:

| Type | Filename | Example |
|------|----------|---------|
| Character sprites | `sprite_{group}_{cg}.png` | `sprite_2_1569.png` |
| Portraits / UI | `sprite_{cg}.png` (fallback) | `sprite_28284.png` |
| Background tiles | `bg_{key}.png` | `bg_1801132.png` |

### Background tile key format

Background tile keys encode the screen context to avoid collisions:

```
key = texture_type * 100000 + (stage + 1) * 1000 + gbix
```

Where `texture_type` is `0x12` (gameplay), `0x18` (select screen), or `0x20` (endings).

## Plugin Interface

The plugin exports a single function:

```c
renderer_export_t* GetRendererAPI(const renderer_import_t* import);
```

### Export table (`renderer_export_t`)

| Field | Description |
|-------|-------------|
| `api_version` | Must match `RENDERER_PLUGIN_API_VERSION` |
| `Init` | Called with SDL renderer and NULL-terminated key/value `extra_args` array |
| `Shutdown` | Called on unload |
| `render_scale` | Desired canvas scale (e.g. 4 for HD) |
| `LoadFullSpriteOverride` | Returns an `SDL_Texture*` for a sprite override, or NULL |
| `PushHDSprite` | Submits an HD sprite to the render queue |
| `LoadBGTileOverride` | Returns an `SDL_Texture*` for a background tile, or NULL |
| `DrawBGTile` | Renders a background tile directly |
| `ClearBGTileCache` | Called when stage textures change |

### Import table (`renderer_import_t`)

| Field | Description |
|-------|-------------|
| `Log` | Printf-style logging function |
| `ConvScreenFZ` | Converts z depth for render sorting |
| `PushRenderTask` | Submits a render task to the base code's queue for z-sorting |
| `cps3_width` | CPS3 screen width (384) |
| `cps3_height` | CPS3 screen height (224) |

## Writing a Custom Plugin

To create an alternative renderer plugin, implement `GetRendererAPI` in a shared library that:

1. Stores the import table pointer
2. Returns a populated `renderer_export_t` with `api_version` set to `RENDERER_PLUGIN_API_VERSION`
3. Reads configuration from the `extra_args` key/value pairs in `Init`
4. Uses `import->PushRenderTask` to submit render tasks for correct z-ordering with standard sprites

Name the output `lib<name>.dll` (Windows) or `lib<name>.so` (Linux), then launch with `--renderer <name>`.
