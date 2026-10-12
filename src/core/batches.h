// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Lots : des numeros (1 a 4) donnes aux shaders de la liste pour en rendre un groupe a part (« Rendre le lot 2 »). Les numeros sont notes
// dans jobs/batches.txt, dans le dossier de l'application, et retrouves a la reouverture.
#pragma once
#include "common.h"

constexpr int kBatchCount = 4;

// chemin du shader -> numero de lot (1 a kBatchCount) ; les shaders sans lot n'y figurent pas
using BatchMap = std::map<std::wstring, int>;

BatchMap loadBatches(const std::wstring& exeDir);
// N'ecrit rien si rien n'a change depuis le dernier appel ; supprime le fichier quand il ne reste aucun lot
void saveBatches(const std::wstring& exeDir, const BatchMap& batches);
// Numero de lot d'un shader (0 = aucun)
int batchOf(const BatchMap& batches, const std::wstring& path);
// « [2] chemin » : texte d'une ligne de la liste
std::wstring batchLabel(const BatchMap& batches, const std::wstring& path);
