// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "ui_imgui/dock.h"

// Linux : pas de progression sur le bouton de la barre des taches (l'API des lanceurs passe par D-Bus, une bibliotheque
// que le programme ne livre pas) ; la fenetre demande seulement l'attention a la fin d'un lot.
void setDockProgress(float) {}
