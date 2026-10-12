// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Outils de boucle et de choix d'images : recherche de la période d'un shader, contrôle de la couture d'une vidéo en boucle,
// vignette intelligente. Tout se fait sur de petites images ; la recherche et la vignette demandent un contexte OpenGL courant.
#pragma once
#include "common.h"
#include "hooks.h"
#include "settings.h"

// --- Contrôle de la couture d'un fichier produit ------------------------------------------------------------------------------------
struct LoopCheck {
    double seam = 0;      // écart moyen (0 à 255) entre la dernière et la première image
    double typical = 0;   // écart moyen entre deux images voisines de la vidéo
    bool perfect = false; // la couture n'est pas plus visible que le mouvement ordinaire
};
// Décode le fichier avec ffmpeg en petites images et compare la dernière à la première. Faux si le fichier ne peut pas être lu.
bool checkLoopSeam(const std::wstring& ffmpeg, const std::wstring& file, LoopCheck& out);

// --- Recherche de la période -----------------------------------------------------------------------------------------------------------
struct LoopFound {
    bool found = false;       // une période a été trouvée
    bool isStatic = false;    // l'image ne bouge pas : toute durée convient
    double period = 0;        // secondes
    double distance = 0;      // écart moyen (0 à 255) entre l'image au départ et une période plus loin
    double typical = 0;       // mouvement ordinaire entre deux pas de recherche
    int quality = 0;          // 0 aucune, 1 approchée, 2 parfaite
    double searched = 0;      // durée explorée
};
// Cherche la plus petite période T > 0 telle que le shader à l'instant t + T ressemble à celui de l'instant t (trois instants de
// référence). maxPeriod <= 0 : la durée du shader. Faux et message dans `err` si le shader ne peut pas être sondé (passes à historique...).
// Contexte OpenGL courant requis. H.cancel / H.progress / H.log sont utilisés.
bool findLoop(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, double maxPeriod, const Hooks& H, LoopFound& out,
              std::wstring& err);

// Phrase localisee qui resume le resultat (periode trouvee et reglages a utiliser, image fixe, ou aucune boucle)
std::wstring describeLoop(const LoopFound& lf);

// --- Vignette intelligente ----------------------------------------------------------------------------------------------------------------
struct BestFrame {
    double t = 0;       // instant du shader
    double score = 0;   // 0 à 1
};
// Évalue `candidates` images réparties sur la durée (richesse des couleurs, contraste, détail, exposition) et renvoie les `top` meilleures
// (instants distincts), la meilleure d'abord. Contexte OpenGL courant requis.
bool findBestFrames(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, int candidates, int top, const Hooks& H,
                    std::vector<BestFrame>& out, std::wstring& err);

// Vignette intelligente : cherche les `top` meilleures images puis écrit chacune en PNG (1280 pixels au plus) à côté de la sortie
// (`<nom>_thumb.png` pour la meilleure, `_thumb2.png`, `_thumb3.png` ensuite). Renvoie les fichiers écrits, la meilleure d'abord.
bool renderSmartThumbnails(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, const Hooks& H, std::vector<std::wstring>& files,
                           std::wstring& err);

// Note d'une image RGBA (première ligne en bas) : entropie des couleurs, contraste, détail local et exposition combinés, de 0 à 1.
double frameRichness(const uint8_t* rgba, int w, int h);
