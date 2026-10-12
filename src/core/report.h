// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Rapport de rendu : un fichier texte ou JSON a cote de la video (reglage `report=json|text`), avec la duree, la cadence, le goulot
// (GPU ou encodeur), la taille du fichier et les avertissements.
#pragma once
#include "common.h"

struct RenderReport {
    std::wstring shader, output;
    std::string status = "failed";  // ok | failed | cancelled
    std::wstring error;
    int width = 0, height = 0;
    double fps = 0, mediaSeconds = 0;   // taille et cadence de la sortie, duree de la video
    long long frames = 0;
    double wallSeconds = 0;             // duree totale du rendu, preparation et assemblage compris
    double loopSeconds = 0;             // duree de la boucle de rendu (images seulement)
    double waitSeconds = 0;             // temps passe par le rendu a attendre l'encodeur
    uint64_t bytes = 0;                 // taille du fichier produit
    std::string codec, container, encoder;
    bool loopChecked = false;           // controle de la couture d'une video en boucle (looptools.h)
    double loopSeam = 0, loopTypical = 0;
    bool loopPerfect = false;
    std::vector<std::wstring> warnings;
};

// Images par seconde de la boucle de rendu (0 si inconnue) et goulot : "encoder", "gpu" ou "" quand la mesure manque (rendu trop court).
double reportRenderFps(const RenderReport& r);
std::string reportBottleneck(const RenderReport& r);
double reportWaitPercent(const RenderReport& r);

std::string reportJson(const RenderReport& r);
std::string reportText(const RenderReport& r);  // lignes localisees, UTF-8
// Ligne du journal qui signale quelque chose a surveiller (liste de mots log.words.warn de la langue courante)
bool isWarningLine(const std::wstring& line);
// Ecrit le rapport (format "json" ou "text") ; faux si le fichier ne peut pas etre ecrit.
bool writeRenderReport(const RenderReport& r, const std::wstring& path, const std::string& format);
