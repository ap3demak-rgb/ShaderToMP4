// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Historique des rendus : une ligne JSON par rendu dans history.jsonl, dans le dossier du programme (date, shader, sortie, duree, empreinte du shader et
// des reglages, resultat, reglages utilises). Rien n'est envoye nulle part ; les reglages enregistres permettent de refaire le rendu a l'identique.
#pragma once
#include "common.h"
#include "settings.h"

struct HistoryEntry {
    std::string time;       // « 2026-10-24 10:20:30 », heure locale
    std::wstring shader;
    std::wstring output;    // fichier produit ("" si le rendu a echoue avant)
    double seconds = 0;     // duree du rendu
    std::string hash;       // empreinte du texte du shader (et de ses passes) et des reglages : deux rendus identiques ont la meme
    std::string result;     // ok | failed | cancelled
    std::vector<Override> settings;  // reglages des fenetres au moment du rendu
};

constexpr size_t kHistoryMax = 2000;  // au-dela, les plus anciennes lignes sont retirees

// Empreinte hexadecimale (16 signes) du code du shader, de ses passes et des reglages
std::string historyHash(const std::wstring& shaderPath, const Settings& s);
// Une ligne de history.jsonl (sans fin de ligne), et sa lecture
std::string historyLine(const HistoryEntry& e);
bool parseHistoryLine(const std::string& line, HistoryEntry& out);
// Ajoute un rendu ; reecrit le fichier sans les plus anciennes lignes quand il depasse kHistoryMax
void appendHistory(const std::wstring& exeDir, const HistoryEntry& e);
// Tous les rendus, le plus recent d'abord ; les lignes illisibles sont ignorees
std::vector<HistoryEntry> loadHistory(const std::wstring& exeDir);
void clearHistory(const std::wstring& exeDir);
// Un rendu qui vient de finir : prend l'heure, le code, les reglages
HistoryEntry makeHistoryEntry(const std::wstring& shaderPath, const Settings& s, const std::wstring& output, double seconds, const std::string& result);
