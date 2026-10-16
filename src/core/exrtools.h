// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Outils sur les fichiers OpenEXR ecrits par le programme : liste des couches (--exr-info) et conversion de la profondeur en carte 16 bits
// (--depthmap) pour les logiciels de montage qui attendent une image en niveaux de gris.
#pragma once
#include "common.h"

// Lignes de description d'un fichier : taille, canaux (nom et precision), attributs.
bool exrSummary(const std::wstring& path, std::vector<std::wstring>& lines, std::wstring& err);

// Ecrit une image PNG 16 bits en niveaux de gris (valeurs de haut en bas).
bool writeGray16Png(const std::wstring& path, int width, int height, const std::vector<uint16_t>& pixels);

// Carte de profondeur 16 bits d'un fichier EXR (canal « depth.Z »). near/far : bornes de la profondeur brute ; a 0:0, les bornes declarees dans le
// fichier (depthNear / depthFar) sont lues et le canal est deja normalise. invert : le plus proche en blanc.
bool exrDepthMap(const std::wstring& exrPath, const std::wstring& pngPath, double nearZ, double farZ, bool invert, std::wstring& err);
