// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Palette sombre et constantes d'interface (couleurs 0xRRGGBB).
#pragma once
#include "common.h"

namespace theme {
constexpr unsigned BG = 0x0E1014;           // fond de la fenetre
constexpr unsigned CARD = 0x161920;         // cartes
constexpr unsigned CARD_BORDER = 0x252A35;
constexpr unsigned FIELD = 0x1E222B;        // champs, boutons secondaires
constexpr unsigned FIELD_HOT = 0x282D39;    // survol
constexpr unsigned FIELD_DOWN = 0x181B22;   // appui
constexpr unsigned FIELD_BORDER = 0x2D3340;
constexpr unsigned BORDER_HOT = 0x465066;
constexpr unsigned TEXT = 0xE8EAF0;
constexpr unsigned MUTED = 0x8B93A7;
constexpr unsigned FAINT = 0x838BA0;      // texte discret : contraste >= 4,5:1 (WCAG AA, controle par check-repo)
constexpr unsigned ACCENT_A = 0xFF5A96;     // degrade d'accentuation (rose -> bleu)
constexpr unsigned ACCENT_B = 0x465AFF;
constexpr unsigned ACCENT = 0x8678FF;
constexpr unsigned OK = 0x3DDC97;
constexpr unsigned ERR = 0xFF5C70;
constexpr unsigned WARN = 0xFFB020;

// Echelle d'affichage : celle de la fenetre, ou la valeur forcee par --dpi=N (verification de la mise en page a 125 - 200 %)
inline UINT dpiOverride = 0;
inline UINT dpiFor(HWND h) { return dpiOverride ? dpiOverride : GetDpiForWindow(h); }

// Rectangle de fenetre ramene dans la zone de travail de l'ecran qu'il recouvre le plus (changement d'echelle : la taille suggeree par Windows
// peut depasser l'ecran)
inline RECT fitToWorkArea(RECT r) {
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi)) return r;
    const int w = std::min<int>(r.right - r.left, mi.rcWork.right - mi.rcWork.left), h = std::min<int>(r.bottom - r.top, mi.rcWork.bottom - mi.rcWork.top);
    const int x = std::max<int>(mi.rcWork.left, std::min<int>(r.left, mi.rcWork.right - w)), y = std::max<int>(mi.rcWork.top, std::min<int>(r.top, mi.rcWork.bottom - h));
    return {x, y, x + w, y + h};
}

inline COLORREF cref(unsigned hex) { return RGB((hex >> 16) & 255, (hex >> 8) & 255, hex & 255); }
inline int scale(int v, UINT dpi) { return MulDiv(v, (int)dpi, 96); }
}  // namespace theme
