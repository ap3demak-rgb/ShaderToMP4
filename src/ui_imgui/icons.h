// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Icônes SVG (res/icons/*.svg, embarquées) rastérisées par NanoSVG en textures OpenGL, et textures d'aperçu.
#pragma once
#include "common.h"

namespace ui {

// Texture OpenGL (blanc sur fond transparent, à teinter) de l'icône « name » (sans extension) à px pixels. 0 si introuvable.
unsigned iconTexture(const char* name, int px);
// Crée ou met à jour une texture à partir de pixels BGRA ; renvoie son identifiant.
unsigned uploadBGRA(unsigned texture, int w, int h, const uint8_t* bgra);
void freeTextures();

}  // namespace ui
