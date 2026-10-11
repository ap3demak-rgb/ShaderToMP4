// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Erreurs GLSL localisees (« fichier:ligne: message », comme un compilateur) et ouverture d'un fichier a une ligne.
#pragma once
#include "common.h"

// Numero de ligne d'une ligne du journal du compilateur GLSL (NVIDIA « 0(12) : error », Mesa « 0:12(5): error »,
// AMD « ERROR: 0:12: »), 0 si la ligne n'en contient pas.
int glslErrorLine(const std::wstring& line);

// Reecrit le message d'erreur de compilation : chaque ligne du pilote avec un numero de ligne devient
// « <fichier>:<ligne>: <texte du pilote> ». Si `commonFile` existe et que le numero depasse les `fileLines` lignes du
// fichier, l'erreur est attribuee au code commun.
std::wstring locateGlslErrors(const std::wstring& err, const std::wstring& file, int fileLines, const std::wstring& commonFile = L"");

// Reconnait « [espaces]<fichier existant>:<ligne>: ... » dans une ligne du journal.
bool parseLocation(const std::wstring& logLine, std::wstring& path, int& line);

// Ouvre le fichier dans un editeur a la ligne donnee (Notepad++, Sublime Text, VS Code s'ils sont installes),
// sinon avec l'application par defaut (sans positionnement).
void openAtLine(const std::wstring& path, int line);
