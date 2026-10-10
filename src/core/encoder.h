// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Encodeurs ffmpeg (CPU ou materiel) et espaces colorimetriques.
#pragma once
#include "common.h"
#include "hooks.h"
#include "settings.h"

struct ColorSpec {
    const wchar_t* range;   // tv | pc
    const wchar_t* matrix;  // matrice RGB -> YUV
    const wchar_t* tags;    // etiquettes ffmpeg (espace, primaires, transfert)
};
ColorSpec colorSpec(const std::string& c);

// Encodeurs materiels connus : "nvenc", "amf", "qsv" (Windows, Linux), "vaapi" (Linux), "videotoolbox" (macOS).
// Choisit l'encodeur reellement utilisable : "cpu" ou l'un d'eux (sonde ffmpeg, repli sur le CPU).
std::string resolveEncoder(const Settings& s, const std::wstring& ffmpeg, const Hooks& H);
// Arguments ffmpeg (-c:v ..., qualite, preset) pour l'encodeur resolu.
std::wstring encoderArgs(const Settings& s, const std::string& enc);
// Fin de la chaine de filtres (format envoye a l'encodeur) et option -pix_fmt de sortie.
std::wstring encoderFormat(const std::string& enc);
std::wstring encoderPixFmt(const std::string& enc);
