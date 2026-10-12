// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "ui_imgui/editor_imgui.h"
#include "core/glslsyntax.h"
#include "core/i18n.h"
#include "core/locate.h"
#include "core/passfiles.h"
#include "core/render.h"
#include "core/util.h"
#include "ui_imgui/widgets.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace ui {

namespace {

struct Tab {
    PassTab info;
    std::string text;      // fins de ligne « \n »
    std::string baseline;  // texte lu ou enregistre en dernier
    bool loaded = false, crlf = true, bom = false;
    std::map<int, std::wstring> errors;  // erreurs de compilation par numero de ligne (1 = premiere)
    bool dirty() const { return text != baseline; }
};

struct EditorState {
    bool open = false;
    std::wstring shader;
    std::vector<Tab> tabs;
    int cur = 0;
    bool askClose = false;
    bool focus = false;
} S_;

// std::string comme tampon d'un champ de texte multiligne (redimensionne par le rappel de Dear ImGui)
int resizeCallback(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        std::string* s = (std::string*)d->UserData;
        s->resize((size_t)d->BufTextLen);
        d->Buf = &(*s)[0];
    }
    return 0;
}

bool inputMultiline(const char* id, std::string* s, ImVec2 size) {
    return ImGui::InputTextMultiline(id, &(*s)[0], s->capacity() + 1, size, ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize,
                                     resizeCallback, s);
}

// Coloration : le champ de texte de Dear ImGui ne sait pas colorer. Son texte est dessine transparent (il garde la saisie, la selection et
// le defilement) et le meme texte est redessine par-dessus, plage par plage, avec les couleurs de core/glslsyntax.
struct HighlightCache {
    std::string text;                 // texte analyse
    std::wstring wide;
    std::vector<SyntaxSpan> spans;
    std::vector<int> lineStart;       // indice (dans `wide`) du debut de chaque ligne
    int tab = -1;
} cache_;

void refreshHighlight(const Tab& tab, int index) {
    if (cache_.tab == index && cache_.text == tab.text) return;
    cache_.tab = index;
    cache_.text = tab.text;
    cache_.wide = W(tab.text);
    cache_.spans = highlightGlsl(cache_.wide);
    cache_.lineStart.assign(1, 0);
    for (size_t i = 0; i < cache_.wide.size(); i++)
        if (cache_.wide[i] == L'\n') cache_.lineStart.push_back((int)i + 1);
}

ImU32 kindColor(SyntaxKind k) {
    switch (k) {
        case SyntaxKind::Comment: return col(0x7E8AA3);
        case SyntaxKind::Directive: return col(0xC3A6FF);
        case SyntaxKind::Keyword: return col(0xFF79B0);
        case SyntaxKind::Type: return col(0x5EE0C0);
        case SyntaxKind::Function: return col(0xE5C07B);
        case SyntaxKind::Number: return col(0xB5E08C);
        case SyntaxKind::Preprocessor: return col(0xFFB86C);
        case SyntaxKind::Uniform: return col(0x82AAFF);
        default: return col(pal::TEXT);
    }
}

// Dessine une ligne (indice `line`, a partir de 0) coloree a partir de (x0, y), x0 etant deja decale du defilement horizontal
void drawColoredLine(ImDrawList* dl, int line, float x0, float y) {
    if (line < 0 || line >= (int)cache_.lineStart.size()) return;
    const int a = cache_.lineStart[(size_t)line];
    int b = line + 1 < (int)cache_.lineStart.size() ? cache_.lineStart[(size_t)line + 1] - 1 : (int)cache_.wide.size();
    if (b > a && cache_.wide[(size_t)b - 1] == L'\r') b--;
    if (b <= a) return;
    // premiere plage qui finit apres le debut de la ligne
    size_t si = (size_t)(std::lower_bound(cache_.spans.begin(), cache_.spans.end(), a, [](const SyntaxSpan& s, int pos) { return s.start + s.length <= pos; }) -
                         cache_.spans.begin());
    float x = x0;
    int pos = a;
    auto emit = [&](int from, int to, ImU32 color) {
        if (to <= from) return;
        const std::string piece = U8(cache_.wide.substr((size_t)from, (size_t)(to - from)));
        dl->AddText(ImVec2(x, y), color, piece.c_str());
        x += ImGui::CalcTextSize(piece.c_str()).x;
    };
    for (; si < cache_.spans.size() && cache_.spans[si].start < b; si++) {
        const SyntaxSpan& s = cache_.spans[si];
        const int from = std::max(a, s.start), to = std::min(b, s.start + s.length);
        emit(pos, from, kindColor(SyntaxKind::Plain));
        emit(from, to, kindColor(s.kind));
        pos = to;
    }
    emit(pos, b, kindColor(SyntaxKind::Plain));
}

std::string baseName(const std::wstring& p) {
    const size_t cut = p.find_last_of(L"\\/");
    return U8(cut == std::wstring::npos ? p : p.substr(cut + 1));
}

bool loadTab(Tab& t) {
    std::string raw;
    if (!readFile(t.info.path, raw)) return false;
    t.bom = raw.size() >= 3 && (uint8_t)raw[0] == 0xEF && (uint8_t)raw[1] == 0xBB && (uint8_t)raw[2] == 0xBF;
    t.crlf = raw.find("\r\n") != std::string::npos || raw.find('\n') == std::string::npos;
    Hooks quiet;
    quiet.log = [](const std::wstring&) {};
    normalizeShaderText(raw, quiet);  // UTF-16 -> UTF-8, fins de ligne « \n »
    if (raw.size() >= 3 && (uint8_t)raw[0] == 0xEF && (uint8_t)raw[1] == 0xBB && (uint8_t)raw[2] == 0xBF) raw.erase(0, 3);
    t.text = raw;
    t.baseline = raw;
    t.loaded = true;
    return true;
}

void selectTab(Model& m, int i) {
    if (i < 0 || i >= (int)S_.tabs.size()) return;
    Tab& t = S_.tabs[(size_t)i];
    if (!t.info.exists) {  // passe absente : creee avec son squelette, un clic
        std::wstring err;
        if (!createPassFile(t.info, err)) {
            m.appendLog(TXF("log.error", err.c_str()));
            return;
        }
        t.info.exists = true;
        m.appendLog(TXF("ui.editor.created", t.info.path.c_str()));
    }
    if (!t.loaded && !loadTab(t)) {
        t.loaded = true;
        t.text.clear();
        t.baseline.clear();
    }
    S_.cur = i;
    S_.focus = true;
}

bool saveTab(Model& m, Tab& t) {
    std::string out = t.text;
    if (t.crlf) {
        std::string crlf;
        crlf.reserve(out.size() + out.size() / 20);
        for (char c : out) {
            if (c == '\n') crlf += '\r';
            crlf += c;
        }
        out.swap(crlf);
    }
    if (t.bom) out = "\xEF\xBB\xBF" + out;
    if (!writeFileAtomic(t.info.path, out)) {
        m.appendLog(TXF("log.error", TXF("error.write_failed", t.info.path.c_str()).c_str()));
        return false;
    }
    t.baseline = t.text;
    return true;
}

// Enregistre l'onglet courant puis relance l'apercu : les erreurs de compilation reviennent par le journal
void saveCurrent(Model& m) {
    Tab& t = S_.tabs[(size_t)S_.cur];
    if (!saveTab(m, t)) return;
    for (auto& tab : S_.tabs) tab.errors.clear();
    m.appendLog(TXF("ui.editor.saved", t.info.path.c_str()));
    if (m.playing()) return;  // la lecture recharge le fichier modifie toute seule
    if (!m.running() && !m.paths.empty()) m.runPreview();
}

bool anyDirty() {
    for (const auto& t : S_.tabs)
        if (t.loaded && t.dirty()) return true;
    return false;
}

void closeEditor() {
    S_ = EditorState();
}

}  // namespace

bool editorOpen() { return S_.open; }

void openEditor(Model& m) {
    if (m.paths.empty()) return;
    const std::wstring shader = m.paths[std::min(std::max(0, m.selected), (int)m.paths.size() - 1)];
    if (S_.open && S_.shader == shader) return;
    if (S_.open && anyDirty()) return;  // d'abord enregistrer ou fermer l'editeur en cours
    S_ = EditorState();
    S_.open = true;
    S_.shader = shader;
    for (const PassTab& p : passTabs(shader)) {
        Tab t;
        t.info = p;
        S_.tabs.push_back(t);
    }
    selectTab(m, 0);
}

void drawEditor(Model& m) {
    if (!S_.open || S_.tabs.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colv(pal::BG));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##editor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (S_.focus) ImGui::SetWindowFocus();

    Tab& tab = S_.tabs[(size_t)S_.cur];
    ImGui::PushFont(fontBold);
    ImGui::TextUnformatted((TXF8("ui.editor.title", baseName(tab.info.path).c_str()) + (tab.dirty() ? " *" : "")).c_str());
    ImGui::PopFont();
    ImGui::Spacing();
    // onglets : « + » devant une passe qui n'existe pas encore (un clic la cree)
    for (size_t i = 0; i < S_.tabs.size(); i++) {
        const Tab& t = S_.tabs[i];
        if (i) ImGui::SameLine();
        const std::string label = std::string("=") + (t.info.exists ? "" : "+ ") + U8(t.info.label) + (t.loaded && t.dirty() ? " *" : "");
        ImGui::PushID((int)i);
        if (button(label.c_str(), nullptr, (int)i == S_.cur ? 1 : 0, true)) selectTab(m, (int)i);
        ImGui::PopID();
    }
    ImGui::Spacing();

    // champ de texte et numeros de ligne
    Tab& cur = S_.tabs[(size_t)S_.cur];
    const float bottom = ImGui::GetFrameHeight() + S(20);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int lines = 1 + (int)std::count(cur.text.begin(), cur.text.end(), '\n');
    const float gutterW = ImGui::CalcTextSize(std::to_string(std::max(99, lines)).c_str()).x + S(22);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + gutterW, origin.y));
    ImGui::PushFont(fontMono ? fontMono : fontRegular);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, colv(0x0F1116));
    if (S_.focus) {
        ImGui::SetKeyboardFocusHere();
        S_.focus = false;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));  // le texte du champ est redessine colore par-dessus
    inputMultiline("##code", &cur.text, ImVec2(avail.x - gutterW, std::max(S(80), avail.y - bottom)));
    ImGui::PopStyleColor(2);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const bool active = ImGui::IsItemActive();
    float scrollY = 0, scrollX = 0;
    int cursor = -1;
    float caretAnim = 0;
    if (ImGuiInputTextState* st = ImGui::GetInputTextState(ImGui::GetItemID())) {
        scrollY = st->Scroll.y;
        scrollX = st->Scroll.x;
        if (active) {
            cursor = st->GetCursorPos();
            caretAnim = st->CursorAnim;
        }
    }
    refreshHighlight(cur, S_.cur);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float lh = ImGui::GetFontSize();
        const float y0 = mn.y + ImGui::GetStyle().FramePadding.y - scrollY;
        dl->AddRectFilled(ImVec2(origin.x, mn.y), ImVec2(origin.x + gutterW, mx.y), col(0x12151B), S(6), ImDrawFlags_RoundCornersLeft);
        dl->PushClipRect(ImVec2(origin.x, mn.y), mx, true);
        for (int i = 0; i < lines; i++) {
            const float y = y0 + i * lh;
            if (y + lh < mn.y) continue;
            if (y > mx.y) break;
            const bool bad = cur.errors.count(i + 1) != 0;
            const std::string num = std::to_string(i + 1);
            dl->AddText(ImVec2(origin.x + gutterW - S(10) - ImGui::CalcTextSize(num.c_str()).x, y), col(bad ? pal::ERR : pal::MUTED), num.c_str());
            if (bad) {
                dl->AddCircleFilled(ImVec2(origin.x + S(8), y + lh * 0.5f), S(3), col(pal::ERR));
                dl->AddRectFilled(ImVec2(mn.x, y), ImVec2(mx.x, y + lh), col(pal::ERR, 0.16f));  // la ligne en erreur est teintee
            }
        }
        dl->PopClipRect();
        // texte colore dans la zone du champ (marges comprises : x de depart = bord + marge de saisie)
        const float tx = mn.x + ImGui::GetStyle().FramePadding.x - scrollX;
        dl->PushClipRect(mn, mx, true);
        for (int i = 0; i < lines; i++) {
            const float y = y0 + i * lh;
            if (y + lh < mn.y) continue;
            if (y > mx.y) break;
            drawColoredLine(dl, i, tx, y);
        }
        // curseur : celui du champ est transparent avec le texte, on le dessine (clignotement de Dear ImGui)
        if (cursor >= 0 && cursor <= (int)cur.text.size() && (caretAnim <= 0.0f || ImFmod(caretAnim, 1.20f) <= 0.80f)) {
            const int line = (int)std::count(cur.text.begin(), cur.text.begin() + cursor, '\n');
            const size_t ls = cur.text.rfind('\n', cursor > 0 ? (size_t)cursor - 1 : 0);
            const size_t from = (cursor > 0 && ls != std::string::npos && ls < (size_t)cursor) ? ls + 1 : 0;
            const std::string prefix = cur.text.substr(from, (size_t)cursor - from);
            const float cx = tx + ImGui::CalcTextSize(prefix.c_str()).x, cy = y0 + line * lh;
            dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + lh), col(pal::TEXT), 1.0f);
        }
        dl->PopClipRect();
    }
    ImGui::PopFont();

    // barre du bas : etat, enregistrer, fermer
    ImGui::SetCursorScreenPos(ImVec2(origin.x, mx.y + S(10)));
    std::string status;
    if (!cur.errors.empty()) status = TXF8("ui.editor.line_error", cur.errors.begin()->first, cur.errors.begin()->second.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(colv(cur.errors.empty() ? pal::MUTED : pal::ERR), "%s", status.c_str());
    const float bw = buttonWidth("ui.editor.compare", true) + buttonWidth("ui.btn.save", true) + buttonWidth("ui.params.close", true) + 2 * ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetWindowWidth() - bw - ImGui::GetStyle().WindowPadding.x);
    if (button("ui.editor.compare", "eye", 0, S_.cur == 0 && !m.running())) m.compareVersions(cur.baseline, cur.text);  // l'image du fichier tel qu'il etait contre celle du texte
    ImGui::SameLine();
    if (button("ui.btn.save", "save", 1, cur.dirty())) saveCurrent(m);
    ImGui::SameLine();
    if (button("ui.params.close", "check", 0, true)) {
        if (anyDirty()) S_.askClose = true;
        else closeEditor();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveCurrent(m);
    ImGui::End();
    if (!S_.open) return;

    if (S_.askClose) {
        ImGui::OpenPopup("##editorclose");
        S_.askClose = false;
    }
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(460), 0));
    if (ImGui::BeginPopupModal("##editorclose", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushTextWrapPos(S(430));
        ImGui::TextUnformatted(TXF8("ui.editor.save_changes", baseName(S_.tabs[(size_t)S_.cur].info.path).c_str()).c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (button("ui.btn.save", "save", 1, true)) {
            bool ok = true;
            for (auto& t : S_.tabs)
                if (t.loaded && t.dirty()) ok = saveTab(m, t) && ok;
            ImGui::CloseCurrentPopup();
            if (ok) closeEditor();
        }
        ImGui::SameLine();
        if (button("ui.btn.no", nullptr, 0, true)) {  // ne pas enregistrer
            ImGui::CloseCurrentPopup();
            closeEditor();
        }
        ImGui::SameLine();
        if (button("ui.btn.cancel", nullptr, 2, true) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// Ligne du journal « fichier:ligne: message » : marque l'erreur dans l'onglet de ce fichier
void editorNoteLog(const std::wstring& text) {
    if (!S_.open || S_.tabs.empty()) return;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find(L'\n', pos);
        if (e == std::wstring::npos) e = text.size();
        std::wstring line = text.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && line.back() == L'\r') line.pop_back();
        std::wstring path;
        int n = 0;
        if (line.empty() || !parseLocation(line, path, n) || n < 1) continue;
        for (auto& t : S_.tabs) {
            if (t.info.path != path) continue;
            std::wstring msg = line;
            const size_t at = line.find(path);
            if (at != std::wstring::npos) {  // le message suit « fichier:ligne: »
                size_t p = at + path.size();
                if (p < line.size() && line[p] == L':') p++;
                while (p < line.size() && iswdigit(line[p])) p++;
                if (p < line.size() && line[p] == L':') p++;
                msg = line.substr(p);
            }
            while (!msg.empty() && msg.front() == L' ') msg.erase(msg.begin());
            t.errors[n] = msg;
            break;
        }
    }
}

}  // namespace ui
