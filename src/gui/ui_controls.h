// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Controles dessines (boutons, curseur, barre de progression) et assombrissement des controles natifs.
#pragma once
#include "common.h"

namespace ui {

enum class BtnStyle { Secondary, Primary, Danger, Ghost, Chip };

// A appeler une fois : enregistre les classes "STSlider" et "STProgress".
bool registerControls(HINSTANCE hi);

// Transforme un BUTTON (style BS_OWNERDRAW) en bouton dessine : icone SVG (0 = aucune), style, fond sur lequel il repose.
void setupButton(HWND btn, int iconId, BtnStyle style, unsigned bgColor);
void drawButton(const DRAWITEMSTRUCT* d, UINT dpi);  // reponse a WM_DRAWITEM
void setButtonIcon(HWND btn, int iconId);

// Messages du curseur "STSlider" : memes que le trackbar (TBM_SETRANGE, TBM_SETPOS, TBM_GETPOS), il envoie WM_HSCROLL.
// Messages de "STProgress" : memes que la barre de progression (PBM_SETRANGE32, PBM_SETPOS, PBM_GETPOS) + etat.
enum { STPB_SETSTATE = WM_USER + 100 };  // wParam : 0 normal, 1 pause, 2 erreur
// STSlider : reperes sur la piste (images cles des parametres) ; wParam = nombre, lParam = float* de positions dans 0..1 (copiees)
enum { STSL_SETMARKS = WM_USER + 101 };

void darkTheme(HWND h, const wchar_t* theme);  // "DarkMode_Explorer", "DarkMode_CFD"...
void darkTitleBar(HWND wnd);                   // barre de titre sombre (Windows 10 1809+ / 11)

}  // namespace ui
