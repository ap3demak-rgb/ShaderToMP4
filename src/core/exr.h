// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// OpenEXR multicouche ecrit et relu par le programme, sans bibliotheque : images a balayage (scanlines), canaux nommes en demi-flottant ou
// flottant, compression NONE, ZIPS (une ligne par bloc) ou ZIP (seize lignes), selon la specification publique du format.
// Non pris en charge (refuse a la lecture avec un message) : PIZ, B44, PXR24, DWA, images en tuiles, images profondes.
#pragma once
#include "common.h"

namespace exr {

enum class Compression { None = 0, Rle = 1, Zips = 2, Zip = 3 };

struct Channel {
    std::string name;            // « R », « depth.Z », « normal.X »...
    std::vector<float> pixels;   // largeur x hauteur, de haut en bas
    bool full = false;           // vrai : flottant 32 bits ; faux : demi-flottant 16 bits
};

struct Image {
    int width = 0, height = 0;
    std::vector<Channel> channels;                          // tries par nom a l'ecriture
    std::vector<std::pair<std::string, std::string>> text;  // attributs « string » (comment, owner, software...)
    std::vector<std::pair<std::string, float>> numbers;     // attributs « float » (depthNear, depthFar...)
    const Channel* find(const std::string& name) const;
};

uint16_t floatToHalf(float f);   // arrondi au plus proche, pair en cas d'egalite
float halfToFloat(uint16_t h);

// Compression zlib (deflate a codes fixes + LZ77) et decompression complete ; exposes pour l'auto-test
std::string zlibCompress(const std::string& raw);
bool zlibDecompress(const std::string& packed, size_t expected, std::string& out);

// Ecrit un fichier .exr. Faux et message dans err en cas d'echec (canal vide, tailles incoherentes, fichier non ecrivable).
bool write(const std::wstring& path, const Image& img, Compression c, std::wstring& err);
// Relit un fichier ecrit par ce module (ou par un autre logiciel, dans les compressions prises en charge).
bool read(const std::wstring& path, Image& img, std::wstring& err);

}  // namespace exr
