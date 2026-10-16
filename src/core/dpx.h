// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// DPX : l'encodeur de ffmpeg n'ecrit ni la cadence ni les en-tetes « film » et « television ». Cette etape ajoute ces deux en-tetes (384 octets)
// a un fichier ecrit par ffmpeg et y inscrit la cadence (images par seconde), comme les logiciels de montage la lisent.
#pragma once
#include "common.h"

// Rend vrai si le fichier porte maintenant la cadence (ou la portait deja) ; faux si ce n'est pas un DPX que l'on sait completer.
bool dpxWriteFrameRate(const std::wstring& path, double fps);
// Cadence lue dans un fichier DPX (0 si absente).
double dpxFrameRate(const std::wstring& path);
