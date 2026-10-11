// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Progression du rendu dans l'icone de l'application : Dock de macOS (NSDockTile). Sans effet sous Linux, ou aucune
// bibliotheque deja presente ne permet d'afficher une progression sur le bouton de la barre des taches.
#pragma once

// fraction : 0..1 ; negatif = effacer la progression. A appeler depuis le thread principal.
void setDockProgress(float fraction);
