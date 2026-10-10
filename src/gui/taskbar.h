// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Progression dans le bouton de la barre des taches (ITaskbarList3).
#pragma once
#include "common.h"

namespace taskbar {
void init(HWND wnd);                 // apres CoInitializeEx et la creation de la fenetre
void progress(long long cur, long long total);
void normal();                       // barre verte
void paused();                       // barre jaune
void error();                        // barre rouge
void indeterminate();                // animation (telechargement de taille inconnue)
void clear();                        // plus de barre
void flash(HWND wnd);                // attire l'attention si la fenetre n'est pas au premier plan
void shutdown();
}  // namespace taskbar
