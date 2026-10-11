// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// File d'attente reprenable : les shaders pas encore rendus sont notes dans queue.txt (dossier de l'application)
// et retrouves a la reouverture.
#pragma once
#include "common.h"

// Enregistre les chemins restant a rendre (un par ligne, UTF-8) ; supprime queue.txt s'il n'en reste aucun.
// N'ecrit rien si la liste n'a pas change depuis le dernier appel.
void saveQueue(const std::wstring& exeDir, const std::vector<std::wstring>& pending);
// Chemins notes, limites aux fichiers qui existent encore.
std::vector<std::wstring> loadQueue(const std::wstring& exeDir);
