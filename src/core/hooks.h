// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Rappels du moteur de rendu vers l'appelant (interface graphique ou auto-test).
#pragma once
#include "common.h"


struct Hooks {
    std::function<void(const std::wstring&)> log;
    std::function<void(long long n, long long total, double speed, double remaining)> progress;
    const std::atomic<bool>* cancel = nullptr;
    const std::atomic<bool>* pause = nullptr;
    bool snapshot = false;  // une seule image PNG (au temps "start") au lieu de la video
    // vignette de couverture (avec snapshot) : -1 = non, -2 = milieu de la plage rendue, sinon l'instant en secondes ; image reduite a 1280 pixels au plus
    double thumbTime = -1;
    std::wstring thumbSuffix;  // ajoute au nom de la vignette (_thumb2.png...) : plusieurs propositions de la vignette intelligente
    // planche-contact : une image PNG de colonnes x lignes vignettes tirees a intervalles egaux sur la duree (0 = non)
    int sheetCols = 0, sheetRows = 0;
    // apercu : une image BGRA (haut en bas) au temps previewTime, renvoyee a l'appelant, sans ffmpeg
    std::function<void(int w, int h, std::vector<uint8_t>&& bgra)> previewFrame;
    double previewTime = 0;
    // fichier produit (premiere image pour une sequence), appele une fois le rendu termine avec succes
    std::function<void(const std::wstring&)> output;
    bool cancelled() const { return cancel && cancel->load(); }
    bool paused() const { return pause && pause->load(); }
};

enum class Result { Ok, Failed, Cancelled };
