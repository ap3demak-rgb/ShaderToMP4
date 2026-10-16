// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// TIFF en virgule flottante 32 bits (RGB ou RGBA, sans compression, une bande) ecrit et relu par le programme : ffmpeg ne sait pas ecrire ce format.
// L'etat de l'alpha est declare dans le fichier (balise ExtraSamples : 1 premultiplie, 2 droit).
#pragma once
#include "common.h"

namespace tifffloat {

struct Image {
    int width = 0, height = 0, samples = 3;  // 3 (RGB) ou 4 (RGBA)
    bool premultiplied = false;              // alpha premultiplie (seulement avec 4 canaux)
    std::vector<float> pixels;               // entrelaces, de haut en bas
};

bool write(const std::wstring& path, const Image& img, std::wstring& err);
bool read(const std::wstring& path, Image& img, std::wstring& err);

}  // namespace tifffloat
