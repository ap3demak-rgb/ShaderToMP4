// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Conversion d'une image lue sur le GPU en image d'apercu : BGRA de haut en bas ; avec transparence, composee sur un damier gris.
#pragma once
#include "common.h"

// rgba : w x h octets RGBA (alpha droit), premiere ligne EN BAS (ordre OpenGL). Renvoie du BGRA, premiere ligne en haut, alpha a 255.
inline std::vector<uint8_t> previewBgra(const uint8_t* rgba, int w, int h, bool alpha) {
    std::vector<uint8_t> out((size_t)w * h * 4);
    const size_t row = (size_t)w * 4;
    for (int y = 0; y < h; y++) {
        const uint8_t* src = rgba + (size_t)(h - 1 - y) * row;
        uint8_t* dst = &out[(size_t)y * row];
        for (int x = 0; x < w; x++) {
            int r = src[x * 4 + 0], g = src[x * 4 + 1], b = src[x * 4 + 2];
            if (alpha) {
                const int a = src[x * 4 + 3];
                const int bg = ((x / 8 + y / 8) & 1) ? 200 : 150;  // damier de 8 pixels
                r = (r * a + bg * (255 - a) + 127) / 255;
                g = (g * a + bg * (255 - a) + 127) / 255;
                b = (b * a + bg * (255 - a) + 127) / 255;
            }
            dst[x * 4 + 0] = (uint8_t)b;
            dst[x * 4 + 1] = (uint8_t)g;
            dst[x * 4 + 2] = (uint8_t)r;
            dst[x * 4 + 3] = 255;
        }
    }
    return out;
}
