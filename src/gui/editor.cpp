// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Éditeur de shader : un onglet par fichier du shader (Image et ses passes Buffer A-D, Common, Cubemap A, Sound, Post), coloration GLSL,
// numéros de ligne, erreurs de compilation soulignées. Enregistrer (Ctrl+S) écrit le fichier dans son dossier, jamais ailleurs, puis
// relance l'aperçu : les erreurs du compilateur reviennent par le journal (« fichier:ligne: message ») et se marquent dans l'éditeur.
#include "app.h"
#include "accessible.h"
#include "core/glslsyntax.h"
#include "core/i18n.h"
#include "core/locate.h"
#include "core/passfiles.h"
#include "core/render.h"
#include "core/util.h"
#include "platform/platform.h"
#include "ui_controls.h"
#include "ui_theme.h"
#include "../../res/resource.h"
#include <richole.h>
#include <tom.h>

using namespace theme;

namespace {

enum { ID_ED_SAVE = 1, ID_ED_CLOSE, ID_ED_EDIT, ID_ED_TAB = 100 };
constexpr UINT_PTR kHighlightTimer = 1;

struct Tab {
    PassTab info;
    std::wstring text;           // texte courant, fins de ligne « \n »
    std::wstring baseline;       // texte lu ou enregistre en dernier : « modifie » = different de lui
    bool loaded = false, dirty = false, crlf = true, bom = false;
    std::map<int, std::wstring> errors;  // erreurs de compilation par numero de ligne (1 = premiere)
};

struct Ed {
    HWND wnd = nullptr, edit = nullptr, gutter = nullptr, status = nullptr, bSave = nullptr, bClose = nullptr;
    std::vector<HWND> tabButtons;
    std::vector<Tab> tabs;
    int cur = 0;
    std::wstring shader;
    HFONT mono = nullptr;
    UINT dpi = 96;
    bool quiet = false;  // le texte change par programme (chargement, coloration) : pas de « modifie »
    int lineH = 16;
} E;

int S(int v) { return MulDiv(v, (int)E.dpi, 96); }
const wchar_t* kClass = L"ShaderToMP4Editor";

COLORREF kindColor(SyntaxKind k) {
    switch (k) {
        case SyntaxKind::Comment: return RGB(0x7E, 0x8A, 0xA3);
        case SyntaxKind::Directive: return RGB(0xC3, 0xA6, 0xFF);
        case SyntaxKind::Keyword: return RGB(0xFF, 0x79, 0xB0);
        case SyntaxKind::Type: return RGB(0x5E, 0xE0, 0xC0);
        case SyntaxKind::Function: return RGB(0xE5, 0xC0, 0x7B);
        case SyntaxKind::Number: return RGB(0xB5, 0xE0, 0x8C);
        case SyntaxKind::Preprocessor: return RGB(0xFF, 0xB8, 0x6C);
        case SyntaxKind::Uniform: return RGB(0x82, 0xAA, 0xFF);
        default: return cref(TEXT);
    }
}

// --- Texte de l'éditeur ----------------------------------------------------------------------------------------------------
std::wstring editText() {  // fins de ligne « \n » (le controle garde « \r » entre les paragraphes)
    GETTEXTLENGTHEX gl{GTL_DEFAULT, 1200};
    const LRESULT len = SendMessageW(E.edit, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
    std::wstring t((size_t)len + 1, L' ');
    GETTEXTEX gt{};
    gt.cb = (DWORD)((len + 1) * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    const LRESULT got = SendMessageW(E.edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)&t[0]);
    t.resize((size_t)got);
    for (auto& c : t)
        if (c == L'\r') c = L'\n';
    return t;
}

void setEditText(const std::wstring& lf) {
    std::wstring t = lf;
    for (auto& c : t)
        if (c == L'\n') c = L'\r';
    SETTEXTEX st{ST_DEFAULT, 1200};
    E.quiet = true;
    SendMessageW(E.edit, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)t.c_str());
    SendMessageW(E.edit, EM_EMPTYUNDOBUFFER, 0, 0);
    E.quiet = false;
}

// Les changements de format (coloration) ne doivent pas encombrer l'historique d'annulation
ITextDocument* textDocument() {
    IRichEditOle* ole = nullptr;
    if (!SendMessageW(E.edit, EM_GETOLEINTERFACE, 0, (LPARAM)&ole) || !ole) return nullptr;
    ITextDocument* doc = nullptr;
    static const GUID kIidTextDocument = {0x8CC497C0, 0xA1DF, 0x11CE, {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}};  // IID_ITextDocument
    ole->QueryInterface(kIidTextDocument, (void**)&doc);
    ole->Release();
    return doc;
}

void applyHighlight() {
    if (!E.edit || E.tabs.empty()) return;
    E.quiet = true;
    const std::wstring text = editText();
    const std::vector<SyntaxSpan> spans = highlightGlsl(text);
    ITextDocument* doc = textDocument();
    long undo = 0;
    if (doc) doc->Undo(tomSuspend, &undo);
    CHARRANGE keep{};
    SendMessageW(E.edit, EM_EXGETSEL, 0, (LPARAM)&keep);
    POINT scroll{};
    SendMessageW(E.edit, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(E.edit, WM_SETREDRAW, FALSE, 0);
    const DWORD mask = SendMessageW(E.edit, EM_GETEVENTMASK, 0, 0);
    SendMessageW(E.edit, EM_SETEVENTMASK, 0, 0);
    auto format = [&](int a, int b, COLORREF c, bool wave) {
        CHARRANGE r{a, b};
        SendMessageW(E.edit, EM_EXSETSEL, 0, (LPARAM)&r);
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_UNDERLINETYPE | CFM_UNDERLINE;
        cf.crTextColor = c;
        cf.dwEffects = wave ? CFE_UNDERLINE : 0;
        cf.bUnderlineType = wave ? CFU_UNDERLINEWAVE : CFU_UNDERLINENONE;
        SendMessageW(E.edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    };
    format(0, (int)text.size(), cref(TEXT), false);
    for (const SyntaxSpan& s : spans) format(s.start, s.start + s.length, kindColor(s.kind), false);
    // lignes en erreur : texte rouge souligné
    const Tab& tab = E.tabs[(size_t)E.cur];
    for (const auto& er : tab.errors) {
        const int lineIdx = er.first - 1;
        const int a = (int)SendMessageW(E.edit, EM_LINEINDEX, (WPARAM)lineIdx, 0);
        if (a < 0) continue;
        const int len = (int)SendMessageW(E.edit, EM_LINELENGTH, (WPARAM)a, 0);
        if (len > 0) format(a, a + len, cref(ERR), true);
    }
    SendMessageW(E.edit, EM_EXSETSEL, 0, (LPARAM)&keep);
    SendMessageW(E.edit, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(E.edit, EM_SETEVENTMASK, 0, (LPARAM)mask);
    if (doc) {
        doc->Undo(tomResume, &undo);
        doc->Release();
    }
    SendMessageW(E.edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(E.edit, nullptr, TRUE);
    InvalidateRect(E.gutter, nullptr, FALSE);
    E.quiet = false;
}

// --- Fichiers --------------------------------------------------------------------------------------------------------------
bool loadFile(Tab& t) {
    std::string raw;
    if (!readFile(t.info.path, raw)) return false;
    t.bom = raw.size() >= 3 && (uint8_t)raw[0] == 0xEF && (uint8_t)raw[1] == 0xBB && (uint8_t)raw[2] == 0xBF;
    t.crlf = raw.find("\r\n") != std::string::npos || raw.find('\n') == std::string::npos;
    Hooks quiet;
    quiet.log = [](const std::wstring&) {};
    normalizeShaderText(raw, quiet);  // UTF-16 -> UTF-8, fins de ligne « \n »
    if (raw.size() >= 3 && (uint8_t)raw[0] == 0xEF && (uint8_t)raw[1] == 0xBB && (uint8_t)raw[2] == 0xBF) raw.erase(0, 3);
    t.text = W(raw);
    t.baseline = t.text;
    t.loaded = true;
    return true;
}

void captureCurrent() {
    if (E.cur < 0 || E.cur >= (int)E.tabs.size() || !E.tabs[(size_t)E.cur].loaded) return;
    E.tabs[(size_t)E.cur].text = editText();
}

bool saveTab(int i) {
    Tab& t = E.tabs[(size_t)i];
    if (i == E.cur) captureCurrent();
    std::string out = U8(t.text);
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
        appendLog(TXF("log.error", TXF("error.write_failed", t.info.path.c_str()).c_str()));
        return false;
    }
    t.dirty = false;
    t.baseline = t.text;
    return true;
}

// --- Interface -------------------------------------------------------------------------------------------------------------
std::wstring baseName(const std::wstring& p) {
    const size_t cut = p.find_last_of(L"\\/");
    return cut == std::wstring::npos ? p : p.substr(cut + 1);
}

void updateTitle() {
    const Tab& t = E.tabs[(size_t)E.cur];
    SetWindowTextW(E.wnd, (TXF("ui.editor.title", baseName(t.info.path).c_str()) + (t.dirty ? L" *" : L"")).c_str());
}

void updateTabButtons() {
    for (size_t i = 0; i < E.tabs.size(); i++) {
        const Tab& t = E.tabs[i];
        std::wstring label = (t.info.exists ? L"" : L"+ ") + t.info.label + (t.dirty ? L" *" : L"");
        SetWindowTextW(E.tabButtons[i], label.c_str());
        ui::setButtonStyle(E.tabButtons[i], (int)i == E.cur ? ui::BtnStyle::Primary : ui::BtnStyle::Chip);
    }
}

void updateStatus() {
    if (!E.status || E.tabs.empty()) return;
    const Tab& t = E.tabs[(size_t)E.cur];
    CHARRANGE sel{};
    SendMessageW(E.edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    const int line = (int)SendMessageW(E.edit, EM_EXLINEFROMCHAR, 0, sel.cpMin);
    const int col = sel.cpMin - (int)SendMessageW(E.edit, EM_LINEINDEX, (WPARAM)line, 0);
    std::wstring text = TXF("ui.editor.pos", line + 1, col + 1);
    auto it = t.errors.find(line + 1);
    if (it != t.errors.end()) text += L"   " + it->second;
    else if (!t.errors.empty()) text += L"   " + TXF("ui.editor.line_error", t.errors.begin()->first, t.errors.begin()->second.c_str());
    SetWindowTextW(E.status, text.c_str());
}

void layout() {
    RECT rc;
    GetClientRect(E.wnd, &rc);
    const int w = rc.right, h = rc.bottom, m = S(12), tabsH = S(36), barH = S(48), gutterW = S(52);
    int x = m;
    for (size_t i = 0; i < E.tabButtons.size(); i++) {
        const int bw = S(i == 0 ? 76 : i < 5 ? 96 : 104);
        MoveWindow(E.tabButtons[i], x, m, bw, S(30), TRUE);
        x += bw + S(6);
    }
    const int top = m + tabsH;
    MoveWindow(E.gutter, m, top, gutterW, h - top - barH - m, TRUE);
    MoveWindow(E.edit, m + gutterW, top, w - 2 * m - gutterW, h - top - barH - m, TRUE);
    MoveWindow(E.status, m, h - barH, w - 2 * m - S(250), S(24), TRUE);
    MoveWindow(E.bSave, w - m - S(236), h - barH - S(4), S(112), S(34), TRUE);
    MoveWindow(E.bClose, w - m - S(116), h - barH - S(4), S(116), S(34), TRUE);
}

void switchTab(int i) {
    if (i < 0 || i >= (int)E.tabs.size()) return;
    Tab& t = E.tabs[(size_t)i];
    if (!t.info.exists) {  // passe absente : creee avec son squelette, un clic
        std::wstring err;
        if (!createPassFile(t.info, err)) {
            appendLog(TXF("log.error", err.c_str()));
            return;
        }
        t.info.exists = true;
        appendLog(TXF("ui.editor.created", t.info.path.c_str()));
    }
    captureCurrent();
    E.cur = i;
    if (!t.loaded && !loadFile(t)) {
        t.loaded = true;
        t.text.clear();
    }
    setEditText(t.text);
    applyHighlight();
    updateTabButtons();
    updateTitle();
    updateStatus();
    SetFocus(E.edit);
}

// Enregistre l'onglet courant puis relance l'aperçu : les erreurs de compilation reviennent par le journal
void saveCurrent() {
    Tab& t = E.tabs[(size_t)E.cur];
    if (!saveTab(E.cur)) return;
    for (auto& tab : E.tabs) tab.errors.clear();
    applyHighlight();
    updateTabButtons();
    updateTitle();
    appendLog(TXF("ui.editor.saved", t.info.path.c_str()));
    if (g.player && g.player->running()) return;  // la lecture recharge le fichier modifie toute seule
    if (!g.running && !g.paths.empty()) runPreview();
}

bool confirmClose() {
    captureCurrent();
    for (size_t i = 0; i < E.tabs.size(); i++) {
        if (!E.tabs[i].dirty) continue;
        const int r = MessageBoxW(E.wnd, TXF("ui.editor.save_changes", baseName(E.tabs[i].info.path).c_str()).c_str(), TX("ui.editor.title_short").c_str(),
                                  MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return false;
        if (r == IDYES && !saveTab((int)i)) return false;
    }
    return true;
}

void drawGutter(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT rc = d->rcItem;
    HBRUSH bg = CreateSolidBrush(RGB(0x12, 0x15, 0x1B));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    const Tab& t = E.tabs[(size_t)E.cur];
    HGDIOBJ old = SelectObject(dc, E.mono);
    SetBkMode(dc, TRANSPARENT);
    const int first = (int)SendMessageW(E.edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    const int total = (int)SendMessageW(E.edit, EM_GETLINECOUNT, 0, 0);
    RECT er;
    GetClientRect(E.edit, &er);
    for (int line = first; line < total; line++) {
        const int ci = (int)SendMessageW(E.edit, EM_LINEINDEX, (WPARAM)line, 0);
        if (ci < 0) break;
        const LRESULT pos = SendMessageW(E.edit, EM_POSFROMCHAR, (WPARAM)ci, 0);
        const int y = (short)HIWORD(pos);
        if (y > er.bottom) break;
        const bool bad = t.errors.count(line + 1) != 0;
        RECT lr{0, y, rc.right - S(10), y + E.lineH};
        SetTextColor(dc, bad ? cref(ERR) : cref(MUTED));
        const std::wstring num = std::to_wstring(line + 1);
        DrawTextW(dc, num.c_str(), (int)num.size(), &lr, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
        if (bad) {
            HBRUSH dot = CreateSolidBrush(cref(ERR));
            RECT dr{S(5), y + E.lineH / 2 - S(3), S(11), y + E.lineH / 2 + S(3)};
            FillRect(dc, &dr, dot);
            DeleteObject(dot);
        }
    }
    SelectObject(dc, old);
}

LRESULT CALLBACK editSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        saveCurrent();
        return 0;
    }
    if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {  // onglets au clavier : Ctrl+Tab / Ctrl+Maj+Tab (onglets existants), Ctrl+1 a Ctrl+9
        const int n = (int)E.tabs.size();
        if (wp == VK_TAB) {
            const int dir = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            for (int k = 1; k < n; k++) {
                const int i = ((E.cur + dir * k) % n + n) % n;
                if (E.tabs[(size_t)i].info.exists) {
                    switchTab(i);
                    break;
                }
            }
            return 0;
        }
        if (wp >= '1' && wp <= '9') {
            const int i = (int)(wp - '1');
            if (i < n && E.tabs[(size_t)i].info.exists) switchTab(i);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wp == 19 || wp == 9 && (GetKeyState(VK_CONTROL) & 0x8000))) return 0;  // Ctrl+S, Ctrl+Tab : pas de caractere insere
    if (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL || msg == WM_KEYDOWN || msg == WM_SIZE) {
        const LRESULT r = DefSubclassProc(h, msg, wp, lp);
        InvalidateRect(E.gutter, nullptr, FALSE);
        return r;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK editorProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: layout(); return 0;
        case WM_DPICHANGED: {  // fenetre deplacee sur un ecran d'une autre echelle : police, hauteur de ligne et mise en page suivent
            E.dpi = theme::dpiOverride ? theme::dpiOverride : HIWORD(wp);
            const RECT r = theme::fitToWorkArea(*(const RECT*)lp);
            SetWindowPos(h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (E.mono) DeleteObject(E.mono);
            E.mono = CreateFontW(-MulDiv(10, (int)E.dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Cascadia Mono");
            HDC dc = GetDC(h);
            HGDIOBJ old = SelectObject(dc, E.mono);
            TEXTMETRICW tm{};
            GetTextMetricsW(dc, &tm);
            E.lineH = tm.tmHeight;
            SelectObject(dc, old);
            ReleaseDC(h, dc);
            CHARFORMAT2W cf{};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_SIZE;
            cf.yHeight = 200;
            SendMessageW(E.edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
            layout();
            applyHighlight();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize = {S(720), S(360)};
            return 0;
        }
        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* d = (const DRAWITEMSTRUCT*)lp;
            if (d->CtlType == ODT_BUTTON) {
                ui::drawButton(d, E.dpi);
                return TRUE;
            }
            if (d->hwndItem == E.gutter) {
                drawGutter(d);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            SetBkColor(dc, cref(CARD));
            SetTextColor(dc, cref(MUTED));
            return (LRESULT)g.brCard;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == ID_ED_SAVE) saveCurrent();
            else if (id == ID_ED_CLOSE) SendMessageW(h, WM_CLOSE, 0, 0);
            else if (id >= ID_ED_TAB && id < ID_ED_TAB + (int)E.tabs.size()) switchTab(id - ID_ED_TAB);
            else if (id == ID_ED_EDIT && HIWORD(wp) == EN_CHANGE && !E.quiet) {
                Tab& t = E.tabs[(size_t)E.cur];
                const bool changed = editText() != t.baseline;  // le controle signale aussi la mise en forme : on compare le texte
                if (changed != t.dirty) {
                    t.dirty = changed;
                    updateTitle();
                    updateTabButtons();
                }
                SetTimer(h, kHighlightTimer, 250, nullptr);  // coloration relancee apres une pause de frappe
                InvalidateRect(E.gutter, nullptr, FALSE);
            } else if (id == ID_ED_EDIT && HIWORD(wp) == EN_VSCROLL) {
                InvalidateRect(E.gutter, nullptr, FALSE);
            }
            return 0;
        }
        case WM_NOTIFY: {
            const NMHDR* nh = (const NMHDR*)lp;
            if (nh->hwndFrom == E.edit && nh->code == EN_SELCHANGE) updateStatus();
            return 0;
        }
        case WM_TIMER:
            if (wp == kHighlightTimer) {
                KillTimer(h, kHighlightTimer);
                applyHighlight();
            }
            return 0;
        case WM_CLOSE:
            if (!confirmClose()) return 0;
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            KillTimer(h, kHighlightTimer);
            if (E.mono) DeleteObject(E.mono);
            E = Ed();
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

// Ouvre l'éditeur sur le shader sélectionné (ou ramène la fenêtre déjà ouverte au premier plan)
void showEditor(HWND parent) {
    const std::wstring shader = selectedShaderPath();
    if (shader.empty()) return;
    if (E.wnd) {
        if (E.shader == shader) {
            ShowWindow(E.wnd, SW_RESTORE);
            SetForegroundWindow(E.wnd);
            return;
        }
        if (!confirmClose()) return;
        DestroyWindow(E.wnd);
    }
    static bool registered = false;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = editorProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = CreateSolidBrush(cref(CARD));
        wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    E = Ed();
    E.dpi = theme::dpiFor(parent);
    E.shader = shader;
    for (const PassTab& p : passTabs(shader)) {
        Tab t;
        t.info = p;
        E.tabs.push_back(t);
    }
    RECT pr;
    GetWindowRect(parent, &pr);
    const int ww = S(960), wh = S(720);
    E.wnd = CreateWindowExW(0, kClass, TXF("ui.editor.title", baseName(shader).c_str()).c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                            std::max(0, (int)pr.left + 40), std::max(0, (int)pr.top + 40), ww, wh, nullptr, nullptr, hi, nullptr);
    if (!E.wnd) return;
    ui::darkTitleBar(E.wnd);
    E.mono = CreateFontW(-MulDiv(10, (int)E.dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Cascadia Mono");
    HDC dc = GetDC(E.wnd);
    HGDIOBJ old = SelectObject(dc, E.mono);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    E.lineH = tm.tmHeight;
    SelectObject(dc, old);
    ReleaseDC(E.wnd, dc);

    for (size_t i = 0; i < E.tabs.size(); i++) {
        HWND b = CreateWindowExW(0, L"BUTTON", E.tabs[i].info.label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, E.wnd,
                                 (HMENU)(INT_PTR)(ID_ED_TAB + (int)i), hi, nullptr);
        ui::setupButton(b, 0, ui::BtnStyle::Chip, CARD);
        SendMessageW(b, WM_SETFONT, (WPARAM)g.font, TRUE);
        E.tabButtons.push_back(b);
    }
    LoadLibraryW(L"Msftedit.dll");
    E.edit = CreateWindowExW(0, L"RICHEDIT50W", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL | WS_TABSTOP, 0, 0,
                             10, 10, E.wnd, (HMENU)(INT_PTR)ID_ED_EDIT, hi, nullptr);
    SendMessageW(E.edit, EM_SETBKGNDCOLOR, 0, RGB(0x0F, 0x11, 0x16));
    SendMessageW(E.edit, EM_SETLIMITTEXT, 1 << 24, 0);
    SendMessageW(E.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
    SendMessageW(E.edit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_SCROLL);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    cf.yHeight = 200;  // 10 pt
    cf.crTextColor = cref(TEXT);
    wcscpy_s(cf.szFaceName, L"Cascadia Mono");
    SendMessageW(E.edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    SendMessageW(E.edit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    ui::darkTheme(E.edit, L"DarkMode_Explorer");
    setAccessibleName(E.edit, "ui.editor.title_short");
    SetWindowSubclass(E.edit, editSubclass, 1, 0);
    E.gutter = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 10, 10, E.wnd, nullptr, hi, nullptr);
    E.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0, 10, 10, E.wnd, nullptr, hi, nullptr);
    SendMessageW(E.status, WM_SETFONT, (WPARAM)g.font, TRUE);
    auto button = [&](const char* key, int id, int icon, ui::BtnStyle st) {
        HWND b = CreateWindowExW(0, L"BUTTON", TX(key).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, E.wnd, (HMENU)(INT_PTR)id, hi,
                                 nullptr);
        ui::setupButton(b, icon, st, CARD);
        SendMessageW(b, WM_SETFONT, (WPARAM)g.font, TRUE);
        return b;
    };
    E.bSave = button("ui.btn.save", ID_ED_SAVE, IC_SAVE, ui::BtnStyle::Primary);
    E.bClose = button("ui.params.close", ID_ED_CLOSE, IC_CHECK, ui::BtnStyle::Secondary);
    layout();
    // l'onglet Image : le fichier du shader
    E.cur = -1;
    switchTab(0);
    SetForegroundWindow(E.wnd);
}

bool editorIsOpen() { return E.wnd != nullptr; }

// Ligne du journal « fichier:ligne: message » : marque l'erreur dans l'onglet de ce fichier
static void noteLogLine(const std::wstring& line);

void editorNoteLog(const std::wstring& text) {  // un message du journal peut tenir sur plusieurs lignes (erreurs du compilateur)
    if (!E.wnd || E.tabs.empty()) return;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find(L'\n', pos);
        if (e == std::wstring::npos) e = text.size();
        std::wstring l = text.substr(pos, e - pos);
        while (!l.empty() && l.back() == L'\r') l.pop_back();
        if (!l.empty()) noteLogLine(l);
        pos = e + 1;
    }
}

static void noteLogLine(const std::wstring& line) {
    std::wstring path;
    int n = 0;
    if (!parseLocation(line, path, n) || n < 1) return;
    for (size_t i = 0; i < E.tabs.size(); i++) {
        if (_wcsicmp(E.tabs[i].info.path.c_str(), path.c_str()) != 0) continue;
        // le message suit « fichier:ligne: »
        std::wstring msg = line;
        const size_t at = line.find(path);
        if (at != std::wstring::npos) {
            size_t p = at + path.size();
            if (p < line.size() && line[p] == L':') p++;
            while (p < line.size() && iswdigit(line[p])) p++;
            if (p < line.size() && line[p] == L':') p++;
            msg = line.substr(p);
        }
        while (!msg.empty() && msg.front() == L' ') msg.erase(msg.begin());
        E.tabs[i].errors[n] = msg;
        if ((int)i == E.cur) {
            applyHighlight();
            updateStatus();
        }
        return;
    }
}
