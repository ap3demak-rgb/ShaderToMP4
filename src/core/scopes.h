// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Oscilloscopes de l'image affichee dans l'apercu : histogramme (rouge, vert, bleu, luminance), forme d'onde (luminance en fonction de la colonne) et
// vectorscope (chrominance Cb / Cr, BT.709). Le calcul porte sur l'image telle qu'elle est a l'ecran (BGRA, 8 bits), sans rien relire du disque.
#pragma once
#include "common.h"

constexpr int kScopeWaveCols = 256;

struct Scopes {
    int width = 0, height = 0;
    uint32_t hist[4][256] = {};                // 0 rouge, 1 vert, 2 bleu, 3 luminance
    std::vector<uint32_t> wave;                // kScopeWaveCols colonnes x 256 niveaux de luminance (ligne = niveau, 0 en bas)
    std::vector<uint32_t> vec;                 // 256 x 256 : colonne = Cb, ligne = Cr (centre 128, 128 = gris)
    uint64_t pixels = 0;
};

// Luminance BT.709 (0..255), Cb et Cr (0..255, 128 = pas de couleur) d'un pixel 8 bits
int scopeLuma(int r, int g, int b);
void scopeChroma(int r, int g, int b, int& cb, int& cr);

// bgra : haut en bas, 4 octets par pixel, stride = 4 * w
void computeScopes(const uint8_t* bgra, int w, int h, Scopes& out);

// Images a afficher (BGRA, haut en bas) : histogramme 256 x 128 (R, G, B en couleurs, luminance en blanc), forme d'onde kScopeWaveCols x 256, vectorscope 256 x 256
// avec sa grille (cercle, axes, six reperes de couleur). Les intensites suivent une echelle racine pour que les petits groupes restent visibles.
void renderHistogram(const Scopes& s, std::vector<uint8_t>& bgra, int& w, int& h);
void renderWaveform(const Scopes& s, std::vector<uint8_t>& bgra, int& w, int& h);
void renderVectorscope(const Scopes& s, std::vector<uint8_t>& bgra, int& w, int& h);
