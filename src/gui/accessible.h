// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include "common.h"

// Donne a un controle son nom accessible (cle de langue, relue a chaque demande : suit le changement de langue).
// langKey doit rester valide (litteral).
void setAccessibleName(HWND control, const char* langKey);
// NVDA et les autres lecteurs d'ecran classiques lisent MSAA (IAccessible) et non UI Automation : le nom, le role (curseur, barre de progression)
// et la valeur sont aussi donnes par le service de proprietes oleacc, sans fournisseur a ecrire.
void refreshAccessibleNames();                                // la langue a change : noms relus
void setAccessibleRole(HWND control, long msaaRole);          // ROLE_SYSTEM_SLIDER, ROLE_SYSTEM_PROGRESSBAR...
void setAccessibleValue(HWND control, const std::wstring& value);
// Bouton dessine par le programme (BS_OWNERDRAW) : type « bouton », focalisable au clavier, motif Invoke (UI Automation) ; le nom est le texte du bouton
void setAccessibleButton(HWND control);
