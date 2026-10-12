// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Réglages de sortie « avancés » (transparence, HDR, mise à l'échelle, interpolation, normalisation du son, texte incrusté) tels que
// les deux interfaces les éditent : des champs texte et des indices de liste, convertis en Settings avec la même validation.
#pragma once
#include "common.h"
#include "settings.h"

struct OutputFields {
    bool alpha = false;
    int hdr = 0;                 // 0 aucun, 1 HDR10 (PQ), 2 HLG
    std::wstring hdrPeak, hdrFall, hdrWhite;
    std::wstring scale;          // vide = aucune
    int scaleFilter = 0;         // indice dans scaleFilterId()
    std::wstring interp;         // vide ou 0 = aucune
    std::wstring loudnorm;       // vide = aucune
    std::wstring title, credit, titleDur;
};

constexpr int kScaleFilterCount = 5;
const char* scaleFilterId(int i);            // lanczos, bicubic, bilinear, area, neighbor
int scaleFilterIndex(const std::string& id);  // 0 si inconnu
const char* scaleFilterLabelKey(int i);      // clé i18n du libellé

void outputFieldsFrom(const Settings& s, OutputFields& f);
// Applique les champs à `s`. Faux et message dans `err` si une valeur est invalide (rien n'est modifié).
bool outputFieldsApply(const OutputFields& f, Settings& s, std::wstring& err);

// Planche-contact et vignette : une grille CxR (au plus 100 vignettes) ; la vignette est prise au milieu de la plage.
constexpr int kSheetGrids[][2] = {{3, 2}, {4, 3}, {5, 4}, {6, 5}};
constexpr int kSheetGridCount = 4;
