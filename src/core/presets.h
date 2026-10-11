// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Preselections rapides integrees et preselections de l'utilisateur (dossier presets/ a cote du programme).
#pragma once
#include "common.h"
#include "settings.h"

// Preselections integrees : 0 brouillon, 1 YouTube 1080p60, 2 4K 60, 3 vertical 9:16 (pastilles de la fenetre),
// 4 X 720p, 5 GIF leger, 6 ProRes montage, 7 WebM VP9 (menu « Plus »).
constexpr int kBuiltinPresetCount = 8;
constexpr int kChipPresetCount = 4;
const char* builtinPresetKey(int idx);  // cle de langue du nom (ui.preset.*)
// Modifie seulement la taille, la cadence, le codec, le conteneur et la qualite ; le reste est conserve.
void applyBuiltinPreset(int idx, Settings& s);

struct UserPreset {
    std::wstring name;  // nom du fichier sans « .ini »
    std::wstring path;
};
// Fichiers presets/*.ini a cote du programme, tries par nom ; ils se chargent comme un profil (valeurs par defaut + fichier).
std::vector<UserPreset> userPresets(const std::wstring& exeDir);
Settings loadUserPreset(const UserPreset& p, bool* ok = nullptr);
