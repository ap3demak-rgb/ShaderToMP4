// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "ui_imgui/widgets.h"
#include "core/i18n.h"
#include "platform/platform.h"
#include "ui_imgui/icons.h"
#include "imgui_internal.h"

namespace ui {

Metrics metrics;
ImFont* fontRegular = nullptr;
ImFont* fontBold = nullptr;
ImFont* fontMono = nullptr;

ImVec4 colv(unsigned hex, float alpha) {
    return ImVec4(((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha);
}
ImU32 col(unsigned hex, float alpha) { return ImGui::ColorConvertFloat4ToU32(colv(hex, alpha)); }

std::string T(const char* key) { return i18n::tr8(key); }

void applyStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowRounding = S(12);
    st.ChildRounding = S(12);
    st.FrameRounding = S(8);
    st.PopupRounding = S(10);
    st.GrabRounding = S(8);
    st.ScrollbarRounding = S(8);
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 1;
    st.FrameBorderSize = 1;
    st.PopupBorderSize = 1;
    st.WindowPadding = ImVec2(S(14), S(12));
    st.FramePadding = ImVec2(S(10), S(6));
    st.ItemSpacing = ImVec2(S(8), S(8));
    st.ItemInnerSpacing = ImVec2(S(6), S(4));
    st.ScrollbarSize = S(10);
    st.GrabMinSize = S(14);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = colv(pal::TEXT);
    c[ImGuiCol_TextDisabled] = colv(pal::FAINT);
    c[ImGuiCol_WindowBg] = colv(pal::BG);
    c[ImGuiCol_ChildBg] = colv(pal::CARD);
    c[ImGuiCol_PopupBg] = colv(pal::CARD);
    c[ImGuiCol_Border] = colv(pal::CARD_BORDER);
    c[ImGuiCol_FrameBg] = colv(pal::FIELD);
    c[ImGuiCol_FrameBgHovered] = colv(pal::FIELD_HOT);
    c[ImGuiCol_FrameBgActive] = colv(pal::FIELD_DOWN);
    c[ImGuiCol_Button] = colv(pal::FIELD);
    c[ImGuiCol_ButtonHovered] = colv(pal::FIELD_HOT);
    c[ImGuiCol_ButtonActive] = colv(pal::FIELD_DOWN);
    c[ImGuiCol_Header] = colv(pal::ACCENT, 0.22f);
    c[ImGuiCol_HeaderHovered] = colv(pal::ACCENT, 0.32f);
    c[ImGuiCol_HeaderActive] = colv(pal::ACCENT, 0.42f);
    c[ImGuiCol_SliderGrab] = colv(pal::ACCENT);
    c[ImGuiCol_SliderGrabActive] = colv(pal::ACCENT_A);
    c[ImGuiCol_CheckMark] = colv(pal::ACCENT);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = colv(pal::FIELD_BORDER);
    c[ImGuiCol_ScrollbarGrabHovered] = colv(pal::BORDER_HOT);
    c[ImGuiCol_ScrollbarGrabActive] = colv(pal::BORDER_HOT);
    c[ImGuiCol_Separator] = colv(pal::CARD_BORDER);
    c[ImGuiCol_TextSelectedBg] = colv(pal::ACCENT, 0.35f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_NavHighlight] = colv(pal::ACCENT, 0.0f);
}

void setupFonts(float uiScale, float fbScale) {
    metrics.ui = uiScale;
    metrics.fb = fbScale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x0391, 0x03C9, 0x0400, 0x04FF, 0x2010, 0x2027,
                                     0x2030, 0x205E, 0x20AC, 0x20AC, 0x2190, 0x21FF, 0};
    static std::string regular, bold;
    platform::embeddedData("fonts/Inter-Regular.ttf", regular);
    platform::embeddedData("fonts/Inter-SemiBold.ttf", bold);
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // les octets restent dans nos chaînes statiques
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    const float px = 15.0f * uiScale * fbScale;
    // japonais et chinois : Inter est latine, les caracteres viennent d'une police du systeme fusionnee dans chacune des deux polices
    const std::string lang = i18n::language();
    static std::string cjk, cjkLang;
    int cjkNo = 0;
    if (lang == "ja" || lang == "zh") {
        const std::wstring file = platform::systemCjkFontFile(lang, cjkNo);
        if (cjkLang != lang) {
            cjk.clear();
            cjkLang = lang;
            if (!file.empty()) platform::readFile(file, cjk);
        }
    } else {
        cjk.clear();
        cjkLang.clear();
    }
    auto mergeCjk = [&](float size) {  // fusionne dans la police ajoutee juste avant
        if (cjk.empty()) return;
        ImFontConfig merge;
        merge.FontDataOwnedByAtlas = false;
        merge.MergeMode = true;
        merge.FontNo = cjkNo;
        merge.OversampleH = 1;
        merge.OversampleV = 1;
        io.Fonts->AddFontFromMemoryTTF(&cjk[0], (int)cjk.size(), size, &merge,
                                       lang == "ja" ? io.Fonts->GetGlyphRangesJapanese() : io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    };
    fontRegular = io.Fonts->AddFontFromMemoryTTF(&regular[0], (int)regular.size(), px, &cfg, ranges);
    mergeCjk(px);
    fontBold = io.Fonts->AddFontFromMemoryTTF(&bold[0], (int)bold.size(), px * 1.1f, &cfg, ranges);
    mergeCjk(px * 1.1f);
    {  // editeur de shader : police a chasse fixe integree a Dear ImGui (caracteres ASCII)
        ImFontConfig mono;
        mono.SizePixels = 14.0f * uiScale * fbScale;
        mono.OversampleH = 1;
        mono.OversampleV = 1;
        fontMono = io.Fonts->AddFontDefault(&mono);
    }
    io.FontGlobalScale = 1.0f / fbScale;
    io.Fonts->Build();
}

void icon(const char* name, float size, unsigned color, float alpha) {
    const unsigned tex = iconTexture(name, (int)std::ceil(size * metrics.fb));
    const ImVec2 sz(size, size);
    const float dy = (ImGui::GetTextLineHeight() - size) * 0.5f;
    if (dy > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + dy);
    if (tex) ImGui::ImageWithBg((ImTextureID)(intptr_t)tex, sz, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), colv(color, alpha));
    else ImGui::Dummy(sz);
}

bool beginCard(const char* id, const char* titleKey, const char* iconName, ImVec2 size, const std::string& rightText) {
    ImGui::PushStyleColor(ImGuiCol_Border, colv(pal::CARD_BORDER));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(14)));
    const bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    if (titleKey) {
        if (iconName) {
            icon(iconName, S(18), pal::ACCENT);
            ImGui::SameLine(0, S(8));
        }
        ImGui::PushFont(fontBold);
        ImGui::TextUnformatted(T(titleKey).c_str());
        ImGui::PopFont();
        if (!rightText.empty()) {
            ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(rightText.c_str()).x);
            ImGui::TextColored(colv(pal::MUTED), "%s", rightText.c_str());
        }
        ImGui::Spacing();
    }
    return open;
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void tip(const char* key) {
    if (key && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(8)));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(S(380));
        ImGui::TextUnformatted(T(key).c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        ImGui::PopStyleVar();
    }
}

float buttonWidth(const char* labelKey, bool withIcon) {
    const std::string text = labelKey[0] == '=' ? std::string(labelKey + 1) : T(labelKey);
    return (withIcon ? S(16) + (text.empty() ? 0.0f : S(8)) : 0) + ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

bool button(const char* labelKey, const char* iconName, int kind, bool enabled, float width, const char* tipKey) {
    const std::string text = labelKey[0] == '=' ? std::string(labelKey + 1) : T(labelKey);  // « =texte » : texte brut
    const float iconSize = S(16);
    const ImGuiStyle& st = ImGui::GetStyle();
    const float gapW = text.empty() ? 0.0f : S(8);
    const float contentW = (iconName ? iconSize + gapW : 0) + ImGui::CalcTextSize(text.c_str()).x;
    const ImVec2 size(width > 0 ? width : contentW + st.FramePadding.x * 2.0f, ImGui::GetFrameHeight() + S(4));
    ImGui::PushID(labelKey);
    ImGui::BeginDisabled(!enabled);
    ImVec4 base = colv(pal::FIELD);
    if (kind == 2) base = ImVec4(0, 0, 0, 0);
    if (kind == 1) base = ImVec4(0, 0, 0, 0);
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_Border, kind == 0 ? colv(pal::FIELD_BORDER) : ImVec4(0, 0, 0, 0));
    if (kind == 1) {
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0.18f));
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (kind == 1) {  // dégradé rose -> bleu sous le bouton (rectangle arrondi dont on recolore les sommets)
        const int v0 = dl->VtxBuffer.Size;
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32_WHITE, st.FrameRounding);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, p, ImVec2(p.x + size.x, p.y),
                                                      col(pal::ACCENT_A, enabled ? 1.0f : 0.35f), col(pal::ACCENT_B, enabled ? 1.0f : 0.35f));
    }
    const bool clicked = ImGui::Button("##b", size);
    const ImVec2 q = ImGui::GetItemRectMin();
    const unsigned fg = kind == 1 ? 0xFFFFFFu : pal::TEXT;
    const float alpha = enabled ? 1.0f : 0.45f;
    float x = q.x + (size.x - contentW) * 0.5f;
    const float cy = q.y + size.y * 0.5f;
    if (iconName) {
        const unsigned tex = iconTexture(iconName, (int)std::ceil(iconSize * metrics.fb));
        if (tex)
            dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(x, cy - iconSize * 0.5f), ImVec2(x + iconSize, cy + iconSize * 0.5f), ImVec2(0, 0),
                         ImVec2(1, 1), col(fg, alpha));
        x += iconSize + gapW;
    }
    dl->AddText(ImVec2(x, cy - ImGui::GetFontSize() * 0.5f), col(fg, alpha), text.c_str());
    ImGui::PopStyleColor(kind == 1 ? 4 : 2);
    ImGui::EndDisabled();
    ImGui::PopID();
    tip(tipKey);
    return clicked && enabled;
}

bool chip(const char* labelKey, const char* tipKey) {
    const std::string text = T(labelKey) + "##chip" + labelKey;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(14));
    ImGui::PushStyleColor(ImGuiCol_Border, colv(pal::FIELD_BORDER));
    const bool clicked = ImGui::Button(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    tip(tipKey);
    return clicked;
}

void progressBar(float frac, float w, float height, bool failed) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = height * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + height), col(pal::FIELD), r);
    frac = std::min(1.0f, std::max(0.0f, frac));
    if (frac > 0.0f) {
        const float fw = std::max(height, w * frac);
        if (failed) {
            dl->AddRectFilled(p, ImVec2(p.x + fw, p.y + height), col(pal::ERR), r);
        } else {
            const int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(p, ImVec2(p.x + fw, p.y + height), IM_COL32_WHITE, r);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, p, ImVec2(p.x + std::max(w, 1.0f), p.y), col(pal::ACCENT_A),
                                                          col(pal::ACCENT_B));
        }
    }
    ImGui::Dummy(ImVec2(w, height));
}

void label(const char* key) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(colv(pal::MUTED), "%s", T(key).c_str());
    ImGui::SameLine(S(120));
}

bool comboList(const char* id, int* index, const std::vector<std::string>& items, float width) {
    bool changed = false;
    if (width > 0) ImGui::SetNextItemWidth(width);
    const char* preview = *index >= 0 && *index < (int)items.size() ? items[*index].c_str() : "";
    if (ImGui::BeginCombo(id, preview)) {
        for (int i = 0; i < (int)items.size(); i++) {
            const bool sel = i == *index;
            if (ImGui::Selectable(items[i].c_str(), sel)) {
                *index = i;
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool comboInput(const char* id, char* buf, size_t cap, const std::vector<std::string>& choices, float width) {
    bool changed = false;
    const float arrow = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(width - arrow - gap);
    changed |= ImGui::InputText((std::string("##t") + id).c_str(), buf, cap);
    ImGui::SameLine(0, gap);
    const std::string popup = std::string("##p") + id;
    if (ImGui::ArrowButton((std::string("##a") + id).c_str(), ImGuiDir_Down)) ImGui::OpenPopup(popup.c_str());
    if (ImGui::BeginPopup(popup.c_str())) {
        for (const auto& c : choices) {
            if (ImGui::Selectable(c.c_str())) {
                snprintf(buf, cap, "%s", c.c_str());
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    return changed;
}

}  // namespace ui
