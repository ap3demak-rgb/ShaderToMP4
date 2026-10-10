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
constexpr unsigned FAINT = 0x555D70;
constexpr unsigned ACCENT_A = 0xFF5A96;     // degrade d'accentuation (rose -> bleu)
constexpr unsigned ACCENT_B = 0x465AFF;
constexpr unsigned ACCENT = 0x7C6CFF;
constexpr unsigned OK = 0x3DDC97;
constexpr unsigned ERR = 0xFF5C70;
constexpr unsigned WARN = 0xFFB020;

inline COLORREF cref(unsigned hex) { return RGB((hex >> 16) & 255, (hex >> 8) & 255, hex & 255); }
inline int scale(int v, UINT dpi) { return MulDiv(v, (int)dpi, 96); }
}  // namespace theme
