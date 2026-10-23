// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Rapport de plantage LOCAL : si le programme s'arrete anormalement, un fichier texte (version, systeme, cartes graphiques, dernieres lignes du
// journal) est ecrit dans le dossier du programme pour etre joint a un rapport de bug. Rien n'est envoye. Les chemins sont remplaces par <path> et
// le texte des shaders n'est jamais garde : seules les dernieres lignes du journal du programme le sont.
#pragma once
#include "common.h"

// Derniere ligne du journal (soixante gardees, tronquees, chemins remplaces)
void crashLog(const std::wstring& line);
// Information de contexte (« gpu », « opengl »...) ajoutee au rapport
void crashSet(const std::string& key, const std::string& value);
// Texte de contexte et du journal pour le rapport (appele au moment de l'arret)
std::string crashContext();
// Remplace les chemins de fichiers (X:\...\nom, //serveur/...) par <path>
std::string scrubPaths(const std::string& text);
