// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Action facultative a la fin d'un lot de rendus reussi (reglage `endaction` de config.ini, jamais active par defaut).
#pragma once
#include "common.h"

bool isEndAction(const std::string& action);  // none | folder | sleep

// folder : montre le dernier fichier produit dans l'explorateur / le gestionnaire de fichiers.
// sleep : met l'ordinateur en veille apres 15 s (fermer le programme pendant ce delai l'annule).
// blocking : attend la fin de l'action (ligne de commande) au lieu de la lancer en arriere-plan (fenetre).
void performEndAction(const std::string& action, const std::wstring& lastOutput, const std::function<void(const std::wstring&)>& log,
                      bool blocking);
