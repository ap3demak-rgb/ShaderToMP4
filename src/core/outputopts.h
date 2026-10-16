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
    // Reglages avances (config.ini ou commentaires du shader) : nom du fichier, fichier existant, parties, file de lecture GPU, obturateur, fin de lot
    std::wstring name;
    int exists = 0;              // 0 ecraser, 1 numeroter, 2 ignorer
    std::wstring chunk, pbo, shutter;
    int endAction = 0;           // 0 rien, 1 montrer le dossier, 2 veille
    bool metadata = true;
    std::wstring variants;       // variantes separees par « ; » : nom:cle=valeur|cle=valeur
    // Temps et boucle : boucle sans couture (0 aucune, 1 fondu, 2 aller-retour), duree du fondu, courbe de temps, vitesse, echantillons de l'image fixe
    int loopMode = 0;
    std::wstring loopFade, timemap, speed, samples;
    // Publication : cible, poids cible, recadrage, maitre en cache et sa taille maximale, source embarquee, apercu anime (0 aucun, 1 GIF, 2 WebP), credit du shader
    std::wstring target, targetSize, crop, cacheMax;
    bool masterCache = false, embed = false, autoCredit = false;
    int animPreview = 0;
    // Son et rythme : tempo force (vide = detecte), temps par mesure, marqueurs (0 aucun, 1 chapitres, 2 fichiers, 3 les deux), montage sur la musique (0 aucun, 1 temps, 2 mesures, 3 sections)
    std::wstring bpm, beatsPerBar;
    // Calcul reparti : contextes en parallele (1 = en serie), dossier partage de la ferme (vide = aucune)
    std::wstring parallel, farm;
    int markers = 0, beatSync = 0;
    bool precheck = false;       // controle qualite avant le rendu
    int stillFormat = 0;         // 0 PNG, 1 TIFF
    bool audioFollow = false;    // le son suit la vitesse constante
    // Mode effet : source(s), ce que la source impose (0 taille+cadence+duree, 1 taille+cadence, 2 taille, 3 rien), son de la source, lecture a l'envers,
    // cadence d'une sequence, duree et decalage de la transition
    std::wstring source, source2, sourceFps, transDur, transOffset;
    int sourceMatch = 0;
    bool sourceAudio = true, reverse = false;
    // Sorties multicouches : passes de sortie (separees par « ; »), compression EXR (0 zip, 1 zips, 2 aucune), EXR 32 bits, alpha premultiplie, bornes de profondeur, TIFF flottant
    std::wstring aov, depthRange, vectorBlur;
    int exrComp = 0;
    bool exrFloat = false, premult = false, tiffFloat = false;
    std::wstring tile, dpi;      // image fixe : cote des tuiles (vide = automatique), resolution ecrite dans le PNG en ppp (vide = aucune)
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
