// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Export autonome en page HTML : le shader Image et ses tampons (Buffer A-D, Common) deviennent un seul fichier .html qui tourne dans un navigateur
// (WebGL 2) sans réseau. Les images des canaux, le bruit et la grille y sont embarqués. Refusés avec un message : volumes, cubemaps, son, vidéo,
// micro, clavier, paramètres animés, post-traitement, shaders mainVR.
#pragma once
#include "common.h"
#include "settings.h"

// Écrit `outPath`. Faux et message dans `err` si le shader utilise une fonction que la page ne sait pas reproduire.
bool exportHtml(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, const std::wstring& outPath, std::wstring& err);
// Ecrit la page a cote du shader (ou dans `s.outdir`) sous le nom <shader>.html ; `outPath` rend le fichier ecrit
bool exportHtmlBeside(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, std::wstring& outPath, std::wstring& err);
