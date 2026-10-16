// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Montage : plusieurs shaders enchaines dans une seule video, avec une transition entre deux clips (coupe, fondu, fondu enchaine, balayage).
// Un fichier `nom.montage` liste les clips ; chacun est rendu comme un shader (par parties reprenables), puis ffmpeg les assemble.
//
//   # commentaire
//   set codec=h265            (reglage de la video finale, comme dans config.ini)
//   duration=6                (duree par defaut d'un clip, en secondes)
//   transition=crossfade 1.0  (transition par defaut : cut | fade | crossfade | wipe, puis sa duree)
//   intro.glsl 5
//   plasma.glsl 8 crossfade 1.5
//   "mon tunnel.glsl" 6 cut
//
// Ligne de clip : chemin (relatif au fichier .montage ; entre guillemets s'il contient des espaces), duree facultative, puis la transition
// qui amene ce clip depuis le precedent (ignoree pour le premier).
#pragma once
#include "common.h"
#include "hooks.h"
#include "settings.h"

struct MontageClip {
    std::wstring path;
    double duration = 0;
    std::string transition = "cut";  // cut | fade | crossfade | wipe
    double transitionDuration = 1.0;
};

struct MontageDef {
    std::vector<MontageClip> clips;
    std::vector<std::pair<std::string, std::string>> settings;  // lignes « set cle=valeur »
};

// Lit un fichier .montage. Faux + err (fichier, ligne et raison) si une ligne est invalide ou si un clip est introuvable.
bool parseMontage(const std::wstring& path, double defaultDuration, MontageDef& out, std::wstring& err);

// Rend le montage : clips (reprenables), assemblage, nettoyage. Contexte OpenGL courant requis, comme convertFile.
Result convertMontage(const std::wstring& montagePath, const std::wstring& exeDir, const Settings& base, const Hooks& H, std::wstring& err);

// Vrai si le chemin se termine par .montage (casse ignoree).
bool isMontageFile(const std::wstring& path);

// Cale les durees des clips sur une grille musicale : chaque coupe (le centre de la transition) tombe sur un point de `grid` (secondes de la video, triees),
// le plus proche de l'endroit demande, jamais assez pres du precedent pour qu'un clip soit plus court que ses transitions. `requested` : durees demandees ;
// `trans` : duree de la transition qui amene chaque clip (0 pour une coupe, ignoree pour le premier). La fin de la video est calee aussi. Sans point
// utilisable, la duree demandee est gardee.
std::vector<double> snapMontageDurations(const std::vector<double>& requested, const std::vector<double>& trans, const std::vector<double>& grid);
