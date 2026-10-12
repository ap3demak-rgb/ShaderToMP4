// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Moteur de rendu : shader Shadertoy -> images -> ffmpeg (video), image PNG ou apercu. Independant de l'interface.
#pragma once
#include "common.h"
#include "hooks.h"
#include "settings.h"

// Texte d'un shader : UTF-16 -> UTF-8, fins de ligne normalisees.
void normalizeShaderText(std::string& code, const Hooks& H);

// Convertit shaderPath selon les reglages `base` (completes par les commentaires // du shader).
// Un contexte OpenGL doit etre courant dans le thread appelant. exeDir : dossier contenant ffmpeg.exe.
// Un fichier .montage enchaine plusieurs shaders (voir montage.h).
Result convertFile(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H,
                   std::wstring& err);
// Comme convertFile pour un shader, mais les couples (cle, valeur) de `forced` priment sur les commentaires du shader ; sans variantes.
Result convertFileForced(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H, std::wstring& err,
                         const std::vector<std::pair<std::string, std::string>>& forced);
