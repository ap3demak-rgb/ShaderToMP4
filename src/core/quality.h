// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Contrôle qualité avant publication : un rendu d'essai en petites images (sans ffmpeg pour l'analyse) cherche les flashs dangereux pour les
// personnes photosensibles, les valeurs invalides (NaN, infinis), les images noires, blanches ou brûlées, le scintillement, l'aliasing des détails fins
// et la perte de précision aux grands temps. Les résultats sont des aides, jamais une garantie : un avertissement ne bloque jamais le rendu.
#pragma once
#include "common.h"
#include "hooks.h"
#include "settings.h"

// Arrondit au flottant de 16 bits (10 bits de mantisse) le plus proche : ce que recoit un GPU qui calcule en demi-precision (mediump)
double roundToHalf(double v);

struct CheckOptions {
    bool flash = true;        // flashs et flashs rouges saturés (principes des lignes directrices de type Harding)
    bool defects = true;      // NaN, infinis, images noires / blanches, pixels brulés
    bool flicker = true;      // images sans rapport avec la suivante, images isolees fautives
    bool alias = true;        // aliasing des details fins (comparaison avec un rendu en sur-echantillonnage)
    bool portability = true;  // precision aux grands temps (iTime), precision reduite (mediump)
    int maxSamples = 4000;    // images analysees au plus (le pas grandit pour les longues durees)
};

struct CheckIssue {
    std::string kind;     // flash | red_flash | nan | inf | black | white | burnt | flicker | glitch | aliasing | time_large | mediump
    int severity = 1;     // 0 note, 1 avertissement
    double firstTime = -1;  // premier instant du shader concerne (s), -1 si sans objet
    double value = 0;       // mesure principale (flashs par seconde, part de pixels en %, ecart sur 255...)
    long long count = 0;    // images concernees
    std::wstring text;      // phrase localisee avec les chiffres
};

struct CheckReport {
    std::wstring shader;
    double start = 0, duration = 0, fps = 0;
    int samples = 0;          // images analysees
    double maxFlashes = 0, maxRedFlashes = 0;  // par seconde, au pire
    std::vector<CheckIssue> issues;
    std::vector<std::wstring> suspectLines;    // lignes du shader qui peuvent produire des NaN (quand des valeurs invalides ont ete vues)
    std::wstring mapFile;     // image PNG des pixels invalides au premier defaut (vide si aucun)
    int warnings() const;     // nombre d'avertissements (severity 1)
};

// « flash,nan,flicker,alias,portability » ou « all » (aussi « on ») : vrai si tous les mots sont connus
bool parseCheckList(const std::string& list, CheckOptions& out);

// Analyse le shader (contexte OpenGL courant requis). Faux et message dans `err` si le shader ne peut pas etre sonde.
bool runChecks(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, const CheckOptions& opt, const Hooks& H, CheckReport& rep,
               std::wstring& err);

std::string checkReportJson(const CheckReport& rep);
std::string checkReportText(const CheckReport& rep);  // lignes localisees, UTF-8
// Ecrit <stem>.check.json et <stem>.check.txt ; faux si l'un ne peut pas etre ecrit
bool writeCheckReport(const CheckReport& rep, const std::wstring& stem);

// Analyse d'une image RGBA flottante (premiere ligne en bas) : exposee pour les tests
struct FrameStats {
    long long nan = 0, inf = 0, clipped = 0;  // pixels concernes
    double meanLuma = 0;                      // luminance moyenne affichee (0 a 1)
    bool allBlack = false;
};
FrameStats analyseFrame(const float* rgba, int w, int h);
