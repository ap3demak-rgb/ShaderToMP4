// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Shaders VR (mainVR) rendus en video 360 equirectangulaire, mono ou stereo haut / bas, avec les metadonnees de video spherique.
#pragma once
#include "common.h"
#include "settings.h"

// Mode de rendu de l'image pour ce code : 0 = mainImage normal, 1 = 360 mono, 2 = 360 stereo haut / bas, 3 = 360 stereo cote a cote,
// 4 = 360 anaglyphe rouge-cyan, 5 = fulldome (fisheye 180 degres, carre) d'un shader mainVR, 6 = une face de cubemap (fixee par le moteur, jamais
// par `vr`), 7 = fulldome d'un shader mainImage : l'image carree telle quelle, noire hors du cercle (domemaster).
// Reglage `vr` : auto (mainVR utilise seulement si le code n'a pas de mainImage), off, mono, stereo, sbs, anaglyph, fulldome.
// Faux + err si un mode demande est choisi sans mainVR.
bool vrModeFor(const std::string& code, const Settings& s, int& mode, std::wstring& err);

// Ajoute au flux video d'un MP4 / MOV les metadonnees « Spherical Video V1 » (projection equirectangulaire, mode stereo haut / bas)
// lues par YouTube et les lecteurs 360. Le fichier est reecrit en place (copie temporaire puis remplacement). Faux + err en cas d'echec.
// Sans effet (vrai) si le fichier les contient deja.
bool injectSphericalMetadata(const std::wstring& path, int stereoMode, std::wstring& err);  // stereoMode : 0 mono, 1 haut / bas, 2 gauche / droite

// Les six faces d'une cubemap d'un shader mainVR en images PNG carrees (s.width x s.height ; <nom>_px.png ... _nz.png), a l'endroit vues de
// l'interieur : pour un moteur de jeu ou un dome. Renvoie les fichiers ecrits. Contexte OpenGL courant requis.
struct Hooks;
bool renderCubeFaces(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, const Hooks& H, std::vector<std::wstring>& files,
                     std::wstring& err);

// Rapport 360 : rapport largeur / hauteur attendu (mono 2:1, stereo 1:1) ; vrai si la taille convient (a 1 % pres).
bool vrAspectOk(int width, int height, int mode);
const wchar_t* vrAspectText(int mode);  // « 2:1 », « 1:1 », « 4:1 »
// Taille d'un oeil (iResolution) pour une image de la taille donnee : moitie de la hauteur (haut / bas) ou de la largeur (cote a cote)
void vrEyeSize(int mode, int width, int height, int& eyeW, int& eyeH);
// Noms des six faces de cubemap, dans l'ordre du uniform st_face : px nx py ny pz nz
const wchar_t* vrFaceName(int face);
