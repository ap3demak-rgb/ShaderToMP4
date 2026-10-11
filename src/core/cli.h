// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Rendu sans fenetre, identique sur Windows, Linux et macOS : ShaderToMP4 --render [options] fichiers...
#pragma once
#include "common.h"

// Codes de sortie
constexpr int kExitOk = 0;           // tous les rendus ont reussi
constexpr int kExitRenderFailed = 1; // au moins un rendu a echoue
constexpr int kExitUsage = 2;        // ligne de commande invalide (option inconnue, fichier absent, valeur illisible)
constexpr int kExitNoGL = 3;         // contexte OpenGL 3.3 impossible a creer
constexpr int kExitNoFfmpeg = 4;     // ffmpeg introuvable (voir --get-ffmpeg)

// args : arguments apres --render (fichiers et options). write(texte UTF-8, erreur) : sortie standard ou d'erreur.
int runRenderCli(const std::wstring& exeDir, const std::vector<std::wstring>& args,
                 const std::function<void(const std::string&, bool isError)>& write);

// --diagnostic : systeme, carte graphique, OpenGL, ffmpeg et encodeurs, sans chemin ni donnee personnelle (a joindre a un ticket).
int runDiagnosticCli(const std::wstring& exeDir, const std::function<void(const std::string&, bool isError)>& write);
// --benchmark [--seconds=N] [--json] : images par seconde par resolution et par encodeur (rendu reel puis encodage).
int runBenchmarkCli(const std::wstring& exeDir, const std::vector<std::wstring>& args,
                    const std::function<void(const std::string&, bool isError)>& write);

// Texte de l'aide des options de rendu (localise), a ajouter a --help.
std::string renderHelpText();
