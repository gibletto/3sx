/**
 * @file game_renderer.h
 * @brief Cross-platform renderer interface for the game core.
 *
 * Game logic calls CRS_Renderer_* functions for all rendering operations.
 * Each platform provides implementations in its GPU backend.
 */
#ifndef RENDERING_GAME_RENDERER_H
#define RENDERING_GAME_RENDERER_H

#include "rendering/primitives.h"

void CRS_Renderer_CreateTexture(unsigned int th);
void CRS_Renderer_DestroyTexture(unsigned int texture_handle);
void CRS_Renderer_UnlockTexture(unsigned int th);
void CRS_Renderer_CreatePalette(unsigned int ph);
void CRS_Renderer_DestroyPalette(unsigned int palette_handle);
void CRS_Renderer_UnlockPalette(unsigned int th);
void CRS_Renderer_SetTexture(unsigned int th);
void CRS_Renderer_DrawTexturedQuad(const Sprite* sprite, unsigned int color);
void CRS_Renderer_DrawSprite(const Sprite* sprite, unsigned int color);
void CRS_Renderer_DrawSprite2(const Sprite2* sprite2);
void CRS_Renderer_DrawSolidQuad(const Quad* quad, unsigned int color);

#endif
