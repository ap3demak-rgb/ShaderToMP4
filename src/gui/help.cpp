// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Aide integree : fenetre non modale (rubriques a gauche, texte defilant a droite), ouverte par F1 ou le bouton « ? » sur la rubrique de la fenetre
// en cours. Les textes viennent de lang/*.json (pas de page web) ; le glossaire est une rubrique comme les autres.
#include "app.h"
#include "core/i18n.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"

using namespace theme;

namespace {

enum { ID_HELP_TOUR = 901, ID_HELP_CLOSE = IDCANCEL };

struct Topic {
    const char* id;
    const char* title;
    const char* body;  // nullptr : le glossaire
};

const Topic kTopics[] = {
    {"start", "help.start.title", "help.start.body"},       {"main", "help.main.title", "help.main.body"},
    {"output", "help.output.title", "help.output.body"},    {"params", "help.params.title", "help.params.body"},
    {"editor", "help.editor.title", "help.editor.body"},    {"shaders", "help.shaders.title", "help.shaders.body"},
    {"gallery", "help.gallery.title", "help.gallery.body"}, {"gloss", "help.gloss.title", nullptr},
    {"errors", "help.errors.title", "help.errors.body"},
};
constexpr int kTopicCount = (int)(sizeof(kTopics) / sizeof(kTopics[0]));

// Glossaire : « Terme|definition »
const char* kGlossary[] = {"help.gloss.shader", "help.gloss.channel", "help.gloss.pass",   "help.gloss.part",   "help.gloss.loop",
                           "help.gloss.hdr",    "help.gloss.ss",      "help.gloss.blur",   "help.gloss.encoder", "help.gloss.crf"};

struct Dialog {
    HWND wnd = nullptr, bTour = nullptr;
    UINT dpi = 96;
    int topic = 0, hover = -1;
    float scroll = 0, contentH = 0, viewH = 0;
} D;

const wchar_t* kClass = L"ShaderToMP4Help";

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }
float F(int v) { return (float)v * D.dpi / 96.0f; }

int sideWidth() { return S(250); }

std::vector<std::wstring> paragraphs(const char* key) {
    std::vector<std::wstring> out;
    const std::wstring all = TX(key);
    size_t pos = 0;
    while (pos <= all.size()) {
        size_t e = all.find(L'\n', pos);
        if (e == std::wstring::npos) e = all.size();
        if (e > pos) out.push_back(all.substr(pos, e - pos));
        pos = e + 1;
    }
    return out;
}

RECT topicRow(int i) {
    RECT rc;
    GetClientRect(D.wnd, &rc);
    const int top = S(16) + i * S(36);
    return {S(12), top, sideWidth() - S(8), top + S(32)};
}

void layoutControls() {
    RECT rc;
    GetClientRect(D.wnd, &rc);
    MoveWindow(D.bTour, S(12), rc.bottom - S(16) - S(34), sideWidth() - S(20), S(34), TRUE);
}

void clampScroll() { D.scroll = std::max(0.0f, std::min(D.scroll, std::max(0.0f, D.contentH - D.viewH))); }

void paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(D.wnd, &ps);
    RECT rc;
    GetClientRect(D.wnd, &rc);
    ui::Frame f(rc.right, rc.bottom, ui::rgb(BG));
    const float w = (float)rc.right, h = (float)rc.bottom;

    // Rubriques
    f.fillRound(ui::rectf(F(8), F(8), (float)sideWidth() - F(4), h - F(8)), F(12), ui::rgb(CARD));
    for (int i = 0; i < kTopicCount; i++) {
        const RECT r = topicRow(i);
        const bool sel = i == D.topic, hot = i == D.hover;
        if (sel || hot) f.fillRound(ui::rectf(r), F(8), ui::rgb(sel ? FIELD_HOT : FIELD));
        if (sel) f.fillRound(ui::rectf((float)r.left, (float)r.top + F(6), (float)r.left + F(3), (float)r.bottom - F(6)), F(1.5f), ui::rgb(ACCENT));
        f.text(TX(kTopics[i].title), ui::rectf((float)r.left + F(14), (float)r.top, (float)r.right - F(6), (float)r.bottom), F(13), sel, ui::rgb(sel ? TEXT : MUTED));
    }

    // Texte de la rubrique
    const float cx = (float)sideWidth() + F(24), cw = w - cx - F(28);
    D.viewH = h - F(16) - F(8);
    float y = F(24) - D.scroll;
    const Topic& t = kTopics[D.topic];
    f.text(TX(t.title), ui::rectf(cx, y, cx + cw, y + F(34)), F(22), true, ui::rgb(TEXT));
    y += F(46);
    auto block = [&](const std::wstring& s, float size, bool bold, unsigned col, float after) {
        const float bh = ui::measureWrapped(s, size, bold, cw) + F(2);
        f.text(s, ui::rectf(cx, y, cx + cw, y + bh), size, bold, ui::rgb(col), ui::Left, false, true);
        y += bh + after;
    };
    if (t.body) {
        for (const std::wstring& p : paragraphs(t.body)) block(p, F(14), false, TEXT, F(12));
    } else {
        for (const char* key : kGlossary) {
            const std::wstring e = TX(key);
            const size_t bar = e.find(L'|');
            block(bar == std::wstring::npos ? e : e.substr(0, bar), F(14), true, ACCENT, F(2));
            if (bar != std::wstring::npos) block(e.substr(bar + 1), F(14), false, TEXT, F(14));
        }
    }
    D.contentH = y + D.scroll + F(16);
    clampScroll();
    if (D.contentH > D.viewH) {  // barre de defilement discrete
        const float trackTop = F(16), trackH = h - F(32);
        const float thumbH = std::max(F(30), trackH * D.viewH / D.contentH);
        const float thumbY = trackTop + (trackH - thumbH) * (D.scroll / (D.contentH - D.viewH));
        f.fillRound(ui::rectf(w - F(10), thumbY, w - F(6), thumbY + thumbH), F(2), ui::rgb(FIELD_BORDER));
    }
    f.present(hdc);
    EndPaint(D.wnd, &ps);
}

void selectTopic(int i) {
    if (i < 0 || i >= kTopicCount) return;
    D.topic = i;
    D.scroll = 0;
    InvalidateRect(D.wnd, nullptr, FALSE);
}

int hitTopic(POINT p) {
    for (int i = 0; i < kTopicCount; i++) {
        const RECT r = topicRow(i);
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

void scrollBy(float dy) {
    D.scroll += dy;
    clampScroll();
    InvalidateRect(D.wnd, nullptr, FALSE);
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: layoutControls(); InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
            if (d->CtlType == ODT_BUTTON) {
                ui::drawButton(d, D.dpi);
                return TRUE;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            const int hv = hitTopic({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (hv != D.hover) {
                D.hover = hv;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const int hv = hitTopic({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (hv >= 0) selectTopic(hv);
            return 0;
        }
        case WM_MOUSEWHEEL: scrollBy(-(float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * F(60)); return 0;
        case WM_KEYDOWN:
            switch (wp) {
                case VK_ESCAPE: DestroyWindow(h); return 0;
                case VK_DOWN: scrollBy(F(40)); return 0;
                case VK_UP: scrollBy(-F(40)); return 0;
                case VK_NEXT: scrollBy(D.viewH * 0.9f); return 0;
                case VK_PRIOR: scrollBy(-D.viewH * 0.9f); return 0;
                case VK_HOME: scrollBy(-1e9f); return 0;
                case VK_END: scrollBy(1e9f); return 0;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wp) == ID_HELP_TOUR) {
                showTour(GetWindow(h, GW_OWNER) ? GetWindow(h, GW_OWNER) : g.wnd, true);
                DestroyWindow(h);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = S(560);
            mm->ptMinTrackSize.y = S(380);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: D.wnd = nullptr; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK f1Hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == PM_REMOVE) {
        MSG* m = (MSG*)lp;
        if (m->message == WM_KEYDOWN && m->wParam == VK_F1 && !(m->lParam & 0x40000000)) {
            HWND root = m->hwnd ? GetAncestor(m->hwnd, GA_ROOT) : nullptr;
            const char* topic = helpTopicForWindow(root);
            m->message = WM_NULL;
            if (topic) showHelp(root ? root : g.wnd, topic);
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

}  // namespace

const char* helpTopicForWindow(HWND root) {
    wchar_t cls[64] = L"";
    if (root) GetClassNameW(root, cls, 64);
    const std::wstring c = cls;
    if (c == kClass) return nullptr;  // F1 dans l'aide : rien
    if (c == L"ShaderToMP4Output") return "output";
    if (c == L"ShaderToMP4Params") return "params";
    if (c == L"ShaderToMP4Editor") return "editor";
    if (c == L"ShaderToMP4Gallery") return "gallery";
    if (c == L"ShaderToMP4About" || c == L"ShaderToMP4Buy" || c == L"ShaderToMP4Tour") return "start";
    return "main";
}

void installHelpKey() { SetWindowsHookExW(WH_GETMESSAGE, f1Hook, nullptr, GetCurrentThreadId()); }

void showHelp(HWND owner, const char* topicId) {
    int want = 0;
    for (int i = 0; i < kTopicCount; i++)
        if (topicId && strcmp(topicId, kTopics[i].id) == 0) want = i;
    if (D.wnd) {  // deja ouverte : on change de rubrique et on la ramene devant
        selectTopic(want);
        ShowWindow(D.wnd, SW_RESTORE);
        SetForegroundWindow(D.wnd);
        return;
    }
    HINSTANCE hi = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = proc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = CreateSolidBrush(cref(BG));
        wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    D = Dialog();
    D.topic = want;
    D.dpi = theme::dpiFor(owner ? owner : g.wnd);
    RECT want_{0, 0, S(900), S(620)};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectExForDpi(&want_, style, FALSE, 0, D.dpi);
    RECT pr{};
    GetWindowRect(owner ? owner : g.wnd, &pr);
    const int ww = want_.right - want_.left, wh = want_.bottom - want_.top;
    RECT place = theme::fitToWorkArea({pr.left + 40, pr.top + 40, pr.left + 40 + ww, pr.top + 40 + wh});
    D.wnd = CreateWindowExW(0, kClass, TXF("help.window_title", APP_NAME_WSTR).c_str(), style | WS_VISIBLE | WS_CLIPCHILDREN, place.left, place.top,
                            place.right - place.left, place.bottom - place.top, owner ? owner : g.wnd, nullptr, hi, nullptr);
    if (!D.wnd) return;
    ui::darkTitleBar(D.wnd);
    D.bTour = CreateWindowExW(0, L"BUTTON", TX("help.btn.tour").c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, D.wnd,
                              (HMENU)(INT_PTR)ID_HELP_TOUR, hi, nullptr);
    ui::setupButton(D.bTour, IC_HELP, ui::BtnStyle::Secondary, CARD);
    SendMessageW(D.bTour, WM_SETFONT, (WPARAM)g.font, TRUE);
    layoutControls();
    SetFocus(D.wnd);
}
