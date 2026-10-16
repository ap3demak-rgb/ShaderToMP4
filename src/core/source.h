// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Mode effet : le shader s'applique a une video, une image, une sequence d'images ou un dossier de fichiers existants. La source devient iChannel0
// (et une deuxieme source iChannel1 pour une transition) ; sa taille, sa cadence, sa duree, son son, ses sous-titres et ses chapitres sont repris.
#pragma once
#include "common.h"
#include "settings.h"

struct SourceInfo {
    bool ok = false;
    int width = 0, height = 0;   // taille apres la rotation declaree dans le fichier
    double fps = 0, duration = 0;
    int rotation = 0;            // degres (0, 90, 180, 270)
    bool hasAudio = false;
    bool still = false;          // image fixe
    std::vector<std::string> subCodecs;
    int chapters = 0;
    bool colorTagged = false;    // le fichier declare sa matrice de couleur (bt709, bt601, bt2020)
};

// Lit les proprietes d'un fichier video, d'une image ou d'une sequence (motif `img_%04d.png`) par `ffmpeg -i`. seqFps : cadence d'une sequence (0 = 25).
bool probeSource(const std::wstring& ffmpeg, const std::wstring& path, double seqFps, SourceInfo& out);

// Cadence usuelle la plus proche : 29.97 devient 30000/1001, 23.976 devient 24000/1001, 59.94 devient 60000/1001...
double snapFps(double fps);

bool isSourcePattern(const std::wstring& path);         // contient %d ou %0Nd
std::wstring sourceStem(const std::wstring& path);      // nom du fichier sans dossier ni extension
// Chemin tel quel s'il existe (ou si c'est un motif), sinon relatif au dossier du shader
std::wstring resolveSource(const std::wstring& shaderDir, const std::wstring& path);
// Fichiers d'un dossier que le mode effet sait lire (videos et images), tries par nom
std::vector<std::wstring> listSourceFiles(const std::wstring& dir);

// Applique la source aux reglages : taille, cadence et duree (selon sourcematch, sauf les cles de `pinned`, ecrites par le shader ou imposees),
// canaux 0 et 1, son, temps de la transition. `shaderDir` sert a resoudre les chemins relatifs. Faux et message si la source est illisible.
bool applySource(Settings& s, const std::wstring& shaderDir, const std::wstring& ffmpeg, const std::set<std::string>& pinned,
                 const std::function<void(const std::wstring&)>& log, std::wstring& err);

// Arguments ffmpeg qui reprennent les sous-titres et les chapitres de la source (entree numero 1) selon le conteneur ; vide s'il n'y en a pas ou
// s'ils ne tiennent pas dans ce conteneur (dropped : vrai dans ce cas)
std::wstring sourceExtraMaps(const Settings& s, bool& dropped);

// Son d'une transition : A jusqu'a la transition, fondu enchaine, puis B (ecrit dans un WAV). Faux si ffmpeg echoue.
bool buildTransitionAudio(const std::wstring& ffmpeg, const Settings& s, double totalSeconds, const std::wstring& outWav);

// iFrame : numero de l'image de la source montree a l'instant t du shader (sinon le numero de l'image rendue plus le chauffage)
inline long long frameNumber(const Settings& s, long long n, double t) {
    return s.rt.active && s.rt.fps > 0 && !s.rt.still ? (long long)std::llround(t * s.rt.fps) : n + s.warmup;
}
