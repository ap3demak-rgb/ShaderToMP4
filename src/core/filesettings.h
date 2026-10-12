// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Reglages propres a un shader : les champs de la carte des reglages peuvent etre edites pour un seul fichier de la liste. Ils sont
// enregistres dans le dossier de l'application (jobs/<nom>-<empreinte>.ini), jamais dans le shader, sauf si l'utilisateur le demande
// (writeSettingsIntoShader). Ordre d'application : reglages generaux, reglages du fichier, commentaires `// cle=valeur` du shader.
#pragma once
#include "common.h"
#include "settings.h"

// Les reglages que les fenetres editent (carte des reglages et fenetre Sortie : taille, cadence, qualite, codec, alpha, HDR, texte...), en texte
std::vector<Override> mainSettingValues(const Settings& s);
// Ceux de `edited` qui different de `base`
std::vector<Override> diffMainSettings(const Settings& base, const Settings& edited);
// Remplace dans `dst` les reglages de la carte par ceux de `src`
void copyMainSettings(Settings& dst, const Settings& src);
// Applique la liste d'un fichier jobs/ ou d'une difference : comme applyOverrides, mais les lignes `variant` remplacent les variantes
void applyFileOverrides(Settings& s, const std::vector<Override>& list);

std::wstring fileSettingsPath(const std::wstring& exeDir, const std::wstring& shaderPath);
bool fileSettingsExist(const std::wstring& exeDir, const std::wstring& shaderPath);
std::vector<Override> loadFileSettings(const std::wstring& exeDir, const std::wstring& shaderPath);
// Ecrit le fichier meme si la liste est vide : sa presence marque « reglages propres a ce shader »
bool saveFileSettings(const std::wstring& exeDir, const std::wstring& shaderPath, const std::vector<Override>& list);
void removeFileSettings(const std::wstring& exeDir, const std::wstring& shaderPath);
// Reglages generaux + ceux du fichier (s'il en a)
Settings settingsForFile(const Settings& general, const std::wstring& exeDir, const std::wstring& shaderPath);

// Ecrit les reglages en commentaires `// cle=valeur` en tete du shader (une ligne existante pour la meme cle est remplacee). Demande explicite
// de l'utilisateur seulement. Faux et message dans `err` si le fichier ne peut pas etre lu ou ecrit (un fichier UTF-16 est refuse).
bool writeSettingsIntoShader(const std::wstring& shaderPath, const std::vector<Override>& list, std::wstring& err);
