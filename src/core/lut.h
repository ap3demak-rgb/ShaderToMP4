// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// LUT de couleur au format .cube (Adobe / Resolve) : 1D (LUT_1D_SIZE) ou 3D (LUT_3D_SIZE), validee a la lecture.
#pragma once
#include "common.h"

struct Lut {
    int dim = 0;          // 1 ou 3
    int size = 0;         // points par axe (1D : nombre de lignes)
    float dmin[3] = {0, 0, 0}, dmax[3] = {1, 1, 1};  // DOMAIN_MIN / DOMAIN_MAX
    std::vector<float> rgba;  // size (1D) ou size^3 (3D) entrees en RGBA, rouge d'abord pour la 3D (r varie le plus vite, puis g, puis b)
};

// Lit et valide un fichier .cube : tailles 2 a 256 (1D) ou 2 a 65 (3D), nombre exact de lignes de donnees, valeurs finies, domaine valide.
// Faux + err (nom du fichier, ligne et raison) si le fichier est invalide.
bool loadCubeLut(const std::wstring& path, Lut& out, std::wstring& err);
