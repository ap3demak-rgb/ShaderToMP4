// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Utilitaires generaux : conversions de texte, fichiers, chemins.
#pragma once
#include "common.h"

std::wstring W(const std::string& s);   // UTF-8 -> UTF-16
std::string U8(const std::wstring& w);  // UTF-16 -> UTF-8
bool readFile(const std::wstring& path, std::string& out);
bool fileExists(const std::wstring& p);
std::string trim(const std::string& s);
std::string lower(std::string s);
std::wstring fmtTime(double sec);                // mm:ss
std::wstring wfmt(const wchar_t* f, ...);        // swprintf renvoyant une std::wstring (1023 caracteres max)
std::string fnvHex(const std::string& s);        // empreinte FNV-1a 64 bits en hexadecimal
void clearDir(const std::wstring& dir);          // supprime les fichiers d'un dossier (pas les sous-dossiers)
void removeTree(const std::wstring& dir);        // supprime un dossier et tout son contenu

// Filtre de boîte de dialogue de fichiers : "nom1\0motif1\0nom2\0motif2\0\0" (paires nom, motif)
std::wstring makeFilter(const std::vector<std::pair<std::wstring, std::wstring>>& pairs);

// Chemin de sortie : <outdir ou dossier du shader>\<nom du shader><ext>
std::wstring outputPath(const std::wstring& shaderPath, const std::wstring& outdir, const std::wstring& ext);
