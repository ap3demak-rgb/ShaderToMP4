// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Style sombre et widgets de l'interface Linux / macOS (même palette que l'interface Windows).
#pragma once
#include "common.h"
#include "core/util.h"
#include "imgui.h"

namespace ui {

// Palette (0xRRGGBB), identique à src/gui/ui_theme.h
namespace pal {
constexpr unsigned BG = 0x0E1014, CARD = 0x161920, CARD_BORDER = 0x252A35, FIELD = 0x1E222B, FIELD_HOT = 0x282D39, FIELD_DOWN = 0x181B22,
                   FIELD_BORDER = 0x2D3340, BORDER_HOT = 0x465066, TEXT = 0xE8EAF0, MUTED = 0x8B93A7, FAINT = 0x838BA0,
                   ACCENT_A = 0xFF5A96, ACCENT_B = 0x465AFF, ACCENT = 0x8678FF, OK = 0x3DDC97, ERR = 0xFF5C70, WARN = 0xFFB020;
}

ImVec4 colv(unsigned hex, float alpha = 1.0f);
ImU32 col(unsigned hex, float alpha = 1.0f);

struct Metrics {
    float ui = 1.0f;  // facteur d'échelle de l'interface (dp -> pixels de fenêtre)
    float fb = 1.0f;  // pixels de framebuffer par pixel de fenêtre (écrans Retina)
};
extern Metrics metrics;
inline float S(float v) { return v * metrics.ui; }

std::string T(const char* key);  // texte traduit, en UTF-8 (pour ImGui)
#define TXF8(key, ...) U8(TXF(key, __VA_ARGS__))

void applyStyle();
void setupFonts(float uiScale, float fbScale);
extern ImFont* fontRegular;
extern ImFont* fontBold;

void icon(const char* name, float size, unsigned color = pal::TEXT, float alpha = 1.0f);  // icône seule, alignée sur la ligne de texte
bool beginCard(const char* id, const char* titleKey, const char* iconName, ImVec2 size, const std::string& rightText = "");   // à terminer par endCard()
void endCard();
// Bouton à icône. kind : 0 secondaire, 1 principal (dégradé), 2 discret
bool button(const char* labelKey, const char* iconName, int kind, bool enabled, float width = 0.0f, const char* tipKey = nullptr);
float buttonWidth(const char* labelKey, bool withIcon);  // largeur qu'aura button() sans width imposée
bool chip(const char* labelKey, const char* tipKey);
void progressBar(float frac, float width, float height, bool failed);
void tip(const char* key);    // info-bulle de l'élément précédent
void label(const char* key);  // libellé de champ (gris, largeur fixe)
bool comboList(const char* id, int* index, const std::vector<std::string>& items, float width);
// Champ de texte avec liste de valeurs usuelles (flèche à droite)
bool comboInput(const char* id, char* buf, size_t cap, const std::vector<std::string>& choices, float width);

}  // namespace ui
