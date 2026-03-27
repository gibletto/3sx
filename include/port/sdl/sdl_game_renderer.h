#ifndef SDL_GAME_RENDERER_H
#define SDL_GAME_RENDERER_H

#include "rendering/game_renderer.h"
#include <SDL3/SDL.h>

typedef struct SDLGameRenderer_Vertex {
    struct {
        float x;
        float y;
        float z;
        float w;
    } coord;
    unsigned int color;
    TexCoord tex_coord;
} SDLGameRenderer_Vertex;

extern SDL_Texture* cps3_canvas;

/* SDL-specific lifecycle */
void SDLGameRenderer_Init(SDL_Renderer* renderer);
void SDLGameRenderer_BeginFrame();
void SDLGameRenderer_RenderFrame();
void SDLGameRenderer_EndFrame();

#endif
