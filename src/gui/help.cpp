// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Aide integree : fenetre non modale (liste des rubriques a gauche, texte a droite), ouverte par F1 ou le bouton « ? » sur la rubrique de la fenetre
// en cours. Les textes viennent de lang/*.json (pas de page web) ; le glossaire est une rubrique comme les autres. La liste est une vraie liste Windows
// et le texte un controle RichEdit en lecture seule : clavier complet (Tab, fleches, Page haut / bas), selection et copie, lecteurs d'ecran.
#include "accessible.h"
#include "app.h"
#include "core/i18n.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"
#include <commctrl.h>
#include <richedit.h>

using namespace theme;

namespace {

enum { ID_HELP_LIST = 900, ID_HELP_TOUR, ID_HELP_TEXT, ID_HELP_CLOSE = IDCANCEL };

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
    HWND wnd = nullptr, list = nullptr, text = nullptr, bTour = nullptr;
    UINT dpi = 96;
    int topic = 0;
    HBRUSH brCard = nullptr;
} D;

const wchar_t* kClass = L"ShaderToMP4Help";

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }
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

// Ajoute une serie de caracteres a la fin du texte, avec sa taille (points), son epaisseur et sa couleur
void append(const std::wstring& s, int points, bool bold, unsigned color) {
    CHARRANGE end{-1, -1};
    SendMessageW(D.text, EM_EXSETSEL, 0, (LPARAM)&end);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SIZE | CFM_BOLD | CFM_COLOR | CFM_FACE;
    cf.yHeight = points * 20;
    cf.dwEffects = bold ? CFE_BOLD : 0;
    cf.crTextColor = cref(color);
    wcscpy_s(cf.szFaceName, L"Segoe UI");
    SendMessageW(D.text, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(D.text, EM_REPLACESEL, FALSE, (LPARAM)s.c_str());
}

void fillText() {
    SetWindowTextW(D.text, L"");
    const Topic& t = kTopics[D.topic];
    append(TX(t.title) + L"\n\n", 18, true, TEXT);
    if (t.body) {
        for (const std::wstring& p : paragraphs(t.body)) append(p + L"\n\n", 11, false, TEXT);
    } else {
        for (const char* key : kGlossary) {
            const std::wstring e = TX(key);
            const size_t bar = e.find(L'|');
            append((bar == std::wstring::npos ? e : e.substr(0, bar)) + L"\n", 11, true, ACCENT);
            if (bar != std::wstring::npos) append(e.substr(bar + 1) + L"\n\n", 11, false, TEXT);
        }
    }
    CHARRANGE top{0, 0};
    SendMessageW(D.text, EM_EXSETSEL, 0, (LPARAM)&top);
    SendMessageW(D.text, EM_SCROLLCARET, 0, 0);
}

void selectTopic(int i) {
    if (i < 0 || i >= kTopicCount) return;
    D.topic = i;
    if ((int)SendMessageW(D.list, LB_GETCURSEL, 0, 0) != i) SendMessageW(D.list, LB_SETCURSEL, (WPARAM)i, 0);
    fillText();
}

void layoutControls() {
    RECT rc;
    GetClientRect(D.wnd, &rc);
    const int m = S(10), bh = S(34), listBottom = rc.bottom - m - bh - S(8);
    MoveWindow(D.list, m, m, sideWidth() - m, std::max(S(60), listBottom - m), TRUE);
    MoveWindow(D.bTour, m, rc.bottom - m - bh, sideWidth() - m, bh, TRUE);
    MoveWindow(D.text, sideWidth() + m, m, std::max(S(100), (int)rc.right - sideWidth() - 2 * m), std::max(S(60), (int)rc.bottom - 2 * m), TRUE);
}

LRESULT CALLBACK escProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {  // Echap ferme l'aide depuis n'importe quel controle
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        DestroyWindow(GetParent(h));
        return 0;
    }
    if (msg == WM_GETDLGCODE) return DefSubclassProc(h, msg, wp, lp) & ~(LRESULT)(DLGC_WANTTAB | DLGC_WANTALLKEYS);  // le texte en lecture seule ne garde pas Tab : il passe au controle suivant
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, escProc, id);
    return DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: layoutControls(); return 0;
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, cref(TEXT));
            SetBkColor(hdc, cref(CARD));
            if (!D.brCard) D.brCard = CreateSolidBrush(cref(CARD));
            return (LRESULT)D.brCard;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
            if (d->CtlType == ODT_BUTTON) {
                ui::drawButton(d, D.dpi);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == ID_HELP_LIST && HIWORD(wp) == LBN_SELCHANGE) {
                const int i = (int)SendMessageW(D.list, LB_GETCURSEL, 0, 0);
                if (i >= 0 && i != D.topic) {
                    D.topic = i;
                    fillText();
                }
            } else if (LOWORD(wp) == IDCANCEL) {  // Echap (gestionnaire de dialogue)
                DestroyWindow(h);
            } else if (LOWORD(wp) == ID_HELP_TOUR) {
                showTour(GetWindow(h, GW_OWNER) ? GetWindow(h, GW_OWNER) : g.wnd, true);
                DestroyWindow(h);
            }
            return 0;
        case WM_SETFOCUS: SetFocus(D.list); return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) DestroyWindow(h);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = S(560);
            mm->ptMinTrackSize.y = S(380);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY:
            if (D.brCard) DeleteObject(D.brCard);
            D.brCard = nullptr;
            D.wnd = nullptr;
            return 0;
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
        LoadLibraryW(L"Msftedit.dll");  // RichEdit 5 (comme le journal)
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
    D.list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, 0, 0, 10, 10, D.wnd,
                             (HMENU)(INT_PTR)ID_HELP_LIST, hi, nullptr);
    SendMessageW(D.list, WM_SETFONT, (WPARAM)g.font, TRUE);
    SendMessageW(D.list, LB_SETITEMHEIGHT, 0, S(30));
    for (int i = 0; i < kTopicCount; i++) SendMessageW(D.list, LB_ADDSTRING, 0, (LPARAM)TX(kTopics[i].title).c_str());
    ui::darkTheme(D.list, L"DarkMode_Explorer");
    D.text = CreateWindowExW(0, L"RICHEDIT50W", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL, 0, 0, 10, 10, D.wnd,
                             (HMENU)(INT_PTR)ID_HELP_TEXT, hi, nullptr);
    SendMessageW(D.text, EM_SETBKGNDCOLOR, 0, cref(CARD));
    SendMessageW(D.text, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(18), S(18)));
    SendMessageW(D.text, EM_SETTARGETDEVICE, 0, 0);  // retour a la ligne a la largeur du controle
    ui::darkTheme(D.text, L"DarkMode_Explorer");
    D.bTour = CreateWindowExW(0, L"BUTTON", TX("help.btn.tour").c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, D.wnd,
                              (HMENU)(INT_PTR)ID_HELP_TOUR, hi, nullptr);
    ui::setupButton(D.bTour, IC_HELP, ui::BtnStyle::Secondary, BG);
    SendMessageW(D.bTour, WM_SETFONT, (WPARAM)g.font, TRUE);
    setAccessibleName(D.list, "help.topics");
    setAccessibleName(D.text, "help.content");
    for (HWND c : {D.list, D.text, D.bTour}) SetWindowSubclass(c, escProc, 91, 0);
    layoutControls();
    selectTopic(want);
    SetFocus(D.list);
}
