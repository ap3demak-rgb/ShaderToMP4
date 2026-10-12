// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Shaders VR (mainVR) rendus en video 360 equirectangulaire, mono ou stereo haut / bas, avec les metadonnees de video spherique.
#pragma once
#include "common.h"
#include "settings.h"

// Mode de rendu de l'image pour ce code : 0 = mainImage normal, 1 = 360 mono, 2 = 360 stereo haut / bas.
// Reglage `vr` : auto (mainVR utilise seulement si le code n'a pas de mainImage), off, mono, stereo.
// Faux + err si `vr=mono|stereo` est demande sans mainVR.
bool vrModeFor(const std::string& code, const Settings& s, int& mode, std::wstring& err);

// Ajoute au flux video d'un MP4 / MOV les metadonnees « Spherical Video V1 » (projection equirectangulaire, mode stereo haut / bas)
// lues par YouTube et les lecteurs 360. Le fichier est reecrit en place (copie temporaire puis remplacement). Faux + err en cas d'echec.
// Sans effet (vrai) si le fichier les contient deja.
bool injectSphericalMetadata(const std::wstring& path, bool stereo, std::wstring& err);

// Rapport 360 : rapport largeur / hauteur attendu (mono 2:1, stereo 1:1) ; vrai si la taille convient (a 1 % pres).
bool vrAspectOk(int width, int height, int mode);
