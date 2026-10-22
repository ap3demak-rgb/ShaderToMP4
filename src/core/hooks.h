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
    // face de cubemap (avec snapshot, shader mainVR) : 0 a 5 = px nx py ny pz nz, -1 = non ; l'image est ecrite en <nom>_<face>.png
    int cubeFace = -1;
    // planche-contact : une image PNG de colonnes x lignes vignettes tirees a intervalles egaux sur la duree (0 = non)
    int sheetCols = 0, sheetRows = 0;
    // apercu : une image BGRA (haut en bas) au temps previewTime, renvoyee a l'appelant, sans ffmpeg
    std::function<void(int w, int h, std::vector<uint8_t>&& bgra)> previewFrame;
    double previewTime = 0;
    // fichier produit (premiere image pour une sequence), appele une fois le rendu termine avec succes
    std::function<void(const std::wstring&)> output;
    // temps GPU moyens par image (image, tampons, post-traitement, en ms) mesures sur les images echantillonnees jusqu'ici : appele pendant le rendu
    std::function<void(double image, double buffers, double post)> gpuTimes;
    // Rendu d'une seule partie (rendu parallele, ferme) : les images [partFirst, partLast) sont ecrites telles quelles dans partOut, sans son, sans assemblage
    // ni fichier annexe ; tout le reste du travail (reglages, shader, signature) est celui du rendu complet. partLast <= partFirst : rendu ordinaire.
    long long partFirst = 0, partLast = 0;
    std::wstring partOut;
    // Auto-test seulement : panne provoquee d'un contexte de rendu (numero du contexte, numero de la partie : vrai = il echoue) et retard (ms) avant de
    // rendre une partie (une partie lente, pour verifier qu'elle est redistribuee)
    std::function<bool(int worker, long long chunk)> injectFailure;
    std::function<int(int worker, long long chunk)> injectDelay;
    // Auto-test seulement : fonctions Pro simulees (passent par proCheck comme une vraie fonction Pro, voir prolicense.h)
    std::vector<std::string> proTestFeatures;
    double speculateAfter = 8.0;  // secondes apres lesquelles une partie encore en cours peut etre rendue aussi par un contexte libre (la plus rapide gagne)
    bool cancelled() const { return cancel && cancel->load(); }
    bool paused() const { return pause && pause->load(); }
};

enum class Result { Ok, Failed, Cancelled };
