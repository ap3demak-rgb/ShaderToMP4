// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Actions du programme, raccourcis clavier configurables (keys.ini) et palette de commandes (Ctrl+K). Chaque action a un identifiant stable, un nom traduit
// (shortcut.action.<id>), la commande WM_COMMAND qu'elle envoie et un raccourci par defaut ; keys.ini, a cote du programme, change les raccourcis (un conflit
// ou une ligne invalide est refuse et signale dans le journal). La palette cherche dans les noms traduits, sans tenir compte de la casse ni des accents.
#include "accessible.h"
#include "app.h"
#include "core/i18n.h"
#include "core/proext.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_theme.h"
#include "../../res/resource.h"
#include <commctrl.h>

using namespace theme;

namespace {

const std::vector<ActionDef>& baseTable() {
    static const std::vector<ActionDef> t = {
        {"project_new", "shortcut.action.project_new", ID_PROJ_NEW, "Ctrl+N", false},
        {"project_open", "shortcut.action.project_open", ID_PROJ_OPEN, "Ctrl+Shift+O", false},
        {"project_save", "shortcut.action.project_save", ID_PROJ_SAVE, "Ctrl+S", false},
        {"project_saveas", "shortcut.action.project_saveas", ID_PROJ_SAVEAS, "Ctrl+Shift+S", false},
        {"add", "shortcut.action.add", ID_ADD, "Ctrl+O", false},
        {"remove", "shortcut.action.remove", ID_REMOVE, "", false},
        {"clear", "shortcut.action.clear", ID_CLEAR, "", false},
        {"start", "shortcut.action.start", ID_START, "F5", false},
        {"render_selection", "shortcut.action.render_selection", ID_RENDER_SEL, "Ctrl+F5", false},
        {"pause", "shortcut.action.pause", ID_PAUSE, "F6", false},
        {"cancel", "shortcut.action.cancel", ID_CANCEL, "Esc", false},
        {"hold", "shortcut.action.hold", ID_HOLD, "Ctrl+Space", false},
        {"prio_high", "shortcut.action.prio_high", ID_PRIO_UP, "", false},
        {"prio_normal", "shortcut.action.prio_normal", ID_PRIO_NORMAL, "", false},
        {"prio_low", "shortcut.action.prio_low", ID_PRIO_DOWN, "", false},
        {"move_up", "shortcut.action.move_up", ID_MOVE_UP, "", false},
        {"move_down", "shortcut.action.move_down", ID_MOVE_DOWN, "", false},
        {"job_log", "shortcut.action.job_log", ID_JOBLOG, "", false},
        {"history", "shortcut.action.history", ID_HISTORY, "Ctrl+H", false},
        {"preview", "shortcut.action.preview", ID_PREVBTN, "Ctrl+P", false},
        {"zoom_fit", "shortcut.action.zoom_fit", ID_ZOOMFIT, "Ctrl+0", false},
        {"zoom_100", "shortcut.action.zoom_100", ID_ZOOM100, "Ctrl+1", false},
        {"freeze", "shortcut.action.freeze", ID_FREEZE, "Ctrl+Shift+F", false},
        {"scopes", "shortcut.action.scopes", ID_SCOPES, "Ctrl+Shift+V", false},
        {"images", "shortcut.action.images", ID_SNAP, "", false},
        {"folder", "shortcut.action.folder", ID_FOLDER, "", false},
        {"editor", "shortcut.action.editor", ID_EDITOR, "Ctrl+E", false},
        {"params", "shortcut.action.params", ID_PARAMS, "", false},
        {"output", "shortcut.action.output", ID_OUTPUT, "", false},
        {"gallery", "shortcut.action.gallery", ID_GALLERY, "", false},
        {"help", "shortcut.action.help", ID_HELP, "F1", false},
        {"about", "shortcut.action.about", ID_ABOUT, "", false},
        {"language", "shortcut.action.language", ID_LANG, "", false},
        {"palette", "shortcut.action.palette", ID_PALETTE, "Ctrl+K", false},
        {"keys_edit", "shortcut.action.keys_edit", ID_KEYS_EDIT, "", false},
        {"keys_reload", "shortcut.action.keys_reload", ID_KEYS_RELOAD, "", false},
        {"undo", "shortcut.action.undo", ID_UNDO, "Ctrl+Z", true},
        {"redo", "shortcut.action.redo", ID_REDO, "Ctrl+Y", true},
        {"guides", "shortcut.action.guides", ID_GUIDES, "Ctrl+G", true},
    };
    return t;
}

}  // namespace

const std::vector<ActionDef>& actionTable() {
    static std::vector<ActionDef> t;
    if (t.empty()) {
        const ProExt* px = proExt();
        for (const ActionDef& a : baseTable()) {
            if (a.proOnly) {  // les outils d'edition n'existent que dans l'edition Pro
                const bool present = px && ((strcmp(a.id, "guides") == 0 && px->tools.guidesMenu) || (strcmp(a.id, "guides") != 0 && px->tools.undoRedo));
                if (!present) continue;
            }
            t.push_back(a);
        }
    }
    return t;
}

std::wstring actionName(const char* id) {
    for (const ActionDef& a : actionTable())
        if (strcmp(a.id, id) == 0) return TX(a.nameKey);
    return L"";
}

std::wstring actionShortcutText(const char* id) {
    if (const Shortcut* s = g.keys.find(id)) return W(shortcutText(*s));
    return L"";
}

static std::vector<KeyAction> keyActions() {
    std::vector<KeyAction> out;
    for (const ActionDef& a : actionTable()) out.push_back({a.id, a.defaultKey});
    return out;
}

void loadShortcuts() {
    std::vector<std::string> problems;
    g.keys = loadKeys(g.exeDir, keyActions(), problems);
    for (const std::string& p : problems) appendLog(TXF("shortcut.problem", W(p).c_str()));
}

HACCEL buildAccelerators() {
    std::vector<ACCEL> acc;
    auto add = [&](const Shortcut& s, int cmd) {
        ACCEL a{};
        a.fVirt = (BYTE)(FVIRTKEY | (s.ctrl ? FCONTROL : 0) | (s.shift ? FSHIFT : 0) | (s.alt ? FALT : 0));
        a.key = (WORD)s.vk;
        a.cmd = (WORD)cmd;
        acc.push_back(a);
    };
    for (const ActionDef& a : actionTable()) {
        const Shortcut* s = g.keys.find(a.id);
        if (!s || s->empty()) continue;
        if (strcmp(a.id, "help") == 0 && s->vk == VK_F1 && !s->ctrl && !s->shift && !s->alt) continue;  // F1 ouvre l'aide de la fenetre en cours (crochet), dans toutes les fenetres
        add(*s, a.command);
    }
    if (const Shortcut* r = g.keys.find("redo"); r && r->ctrl && !r->shift && !r->alt && r->vk == 'Y') {  // Ctrl+Maj+Z refait aussi, tant qu'il est libre
        Shortcut alt;
        alt.ctrl = alt.shift = true;
        alt.vk = 'Z';
        bool free = true;
        for (const auto& b : g.keys.binding)
            if (b.second == alt) free = false;
        if (free) add(alt, ID_REDO);
    }
    return CreateAcceleratorTableW(acc.data(), (int)acc.size());
}

void reloadShortcuts() {
    loadShortcuts();
    if (g.accel) DestroyAcceleratorTable(g.accel);
    g.accel = buildAccelerators();
    appendLog(TX("shortcut.reloaded"));
}

// keys.ini : cree avec les raccourcis actuels s'il n'existe pas, puis ouvert dans l'editeur de texte du systeme ; « Recharger » le prend en compte
void editShortcuts() {
    const std::wstring path = g.exeDir + L"keys.ini";
    if (!fileExists(path)) writeFileAtomic(path, keysTemplate(g.keys));
    ShellExecuteW(g.wnd, L"open", L"notepad.exe", (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    appendLog(TXF("shortcut.edit_hint", path.c_str()));
}

// ---------------------------------------------------------------------------
//  Palette de commandes
// ---------------------------------------------------------------------------
namespace {

enum { ID_PAL_EDIT = 950, ID_PAL_LIST };

struct Palette {
    HWND wnd = nullptr, edit = nullptr, list = nullptr;
    UINT dpi = 96;
    std::vector<std::wstring> names;  // noms traduits, dans l'ordre de la table
    std::vector<int> shown;           // indices dans la table des actions, affiches
} P;

const wchar_t* kPaletteClass = L"ShaderToMP4Palette";

int PS(int v) { return MulDiv(v, (int)P.dpi, 96); }

void fillList() {
    const std::wstring q = getText(P.edit);
    P.shown = filterNames(P.names, q);
    SendMessageW(P.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(P.list, LB_RESETCONTENT, 0, 0);
    const auto& table = actionTable();
    for (int i : P.shown) {
        const std::wstring key = actionShortcutText(table[(size_t)i].id);
        const std::wstring row = P.names[(size_t)i] + (key.empty() ? L"" : L"\t" + key);
        SendMessageW(P.list, LB_ADDSTRING, 0, (LPARAM)row.c_str());
    }
    if (!P.shown.empty()) SendMessageW(P.list, LB_SETCURSEL, 0, 0);
    SendMessageW(P.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(P.list, nullptr, TRUE);
}

void runSelected() {
    const int sel = (int)SendMessageW(P.list, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)P.shown.size()) return;
    const int cmd = actionTable()[(size_t)P.shown[(size_t)sel]].command;
    DestroyWindow(P.wnd);
    PostMessageW(g.wnd, WM_COMMAND, (WPARAM)cmd, 0);
}

void moveSel(int delta) {
    const int n = (int)SendMessageW(P.list, LB_GETCOUNT, 0, 0);
    if (n <= 0) return;
    int sel = (int)SendMessageW(P.list, LB_GETCURSEL, 0, 0) + delta;
    sel = std::max(0, std::min(n - 1, sel));
    SendMessageW(P.list, LB_SETCURSEL, (WPARAM)sel, 0);
}

// Touches de la zone de saisie : fleches et Page haut / bas deplacent la selection, Entree lance, Echap ferme
LRESULT CALLBACK editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_DOWN) { moveSel(1); return 0; }
        if (wp == VK_UP) { moveSel(-1); return 0; }
        if (wp == VK_NEXT) { moveSel(8); return 0; }
        if (wp == VK_PRIOR) { moveSel(-8); return 0; }
        if (wp == VK_RETURN) { runSelected(); return 0; }
        if (wp == VK_ESCAPE) { DestroyWindow(P.wnd); return 0; }
        if (wp == VK_TAB) { SetFocus(P.list); return 0; }  // Tab passe de la saisie a la liste
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB)) return 0;  // pas de bip
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS | (DefSubclassProc(h, msg, wp, lp));
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, editProc, id);
    return DefSubclassProc(h, msg, wp, lp);
}

// Touches de la liste : Entree lance la commande, Echap ferme, Tab revient a la saisie
LRESULT CALLBACK listProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) { runSelected(); return 0; }
        if (wp == VK_ESCAPE) { DestroyWindow(P.wnd); return 0; }
        if (wp == VK_TAB) { SetFocus(P.edit); return 0; }
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB)) return 0;
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, listProc, id);
    return DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK paletteProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == ID_PAL_EDIT && HIWORD(wp) == EN_CHANGE) fillList();
            else if (LOWORD(wp) == ID_PAL_LIST && HIWORD(wp) == LBN_DBLCLK) runSelected();
            return 0;
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, cref(TEXT));
            SetBkColor(hdc, cref(FIELD));
            return (LRESULT)g.brField;
        }
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, cref(TEXT));
            SetBkColor(hdc, cref(CARD));
            return (LRESULT)g.brCard;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) DestroyWindow(h);  // un clic ailleurs ferme la palette
            return 0;
        case WM_SETFOCUS: SetFocus(P.edit); return 0;
        case WM_DESTROY: P.wnd = nullptr; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void showPalette(HWND owner) {
    if (P.wnd) {
        SetForegroundWindow(P.wnd);
        return;
    }
    HINSTANCE hi = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = paletteProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = CreateSolidBrush(cref(BG));
        wc.lpszClassName = kPaletteClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    P = Palette();
    P.dpi = dpiFor(owner ? owner : g.wnd);
    for (const ActionDef& a : actionTable()) P.names.push_back(actionName(a.id));
    RECT pr{};
    GetWindowRect(owner ? owner : g.wnd, &pr);
    const int ww = PS(520), wh = PS(420);
    const int x = pr.left + (pr.right - pr.left - ww) / 2, y = pr.top + PS(90);
    RECT place = fitToWorkArea({x, y, x + ww, y + wh});
    P.wnd = CreateWindowExW(WS_EX_TOOLWINDOW, kPaletteClass, TX("shortcut.palette_title").c_str(), WS_POPUP | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_CLIPCHILDREN,
                            place.left, place.top, place.right - place.left, place.bottom - place.top, owner ? owner : g.wnd, nullptr, hi, nullptr);
    if (!P.wnd) return;
    ui::darkTitleBar(P.wnd);
    RECT c;
    GetClientRect(P.wnd, &c);
    const int pad = PS(10), fh = PS(30);
    P.edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, pad, pad, c.right - 2 * pad, fh, P.wnd, (HMENU)(INT_PTR)ID_PAL_EDIT, hi, nullptr);
    SendMessageW(P.edit, WM_SETFONT, (WPARAM)g.font, TRUE);
    SendMessageW(P.edit, EM_SETCUEBANNER, TRUE, (LPARAM)TX("shortcut.palette_hint").c_str());
    ui::darkTheme(P.edit, L"DarkMode_CFD");
    P.list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_USETABSTOPS, pad, pad * 2 + fh, c.right - 2 * pad,
                             c.bottom - 3 * pad - fh, P.wnd, (HMENU)(INT_PTR)ID_PAL_LIST, hi, nullptr);
    SendMessageW(P.list, WM_SETFONT, (WPARAM)g.font, TRUE);
    SendMessageW(P.list, LB_SETITEMHEIGHT, 0, PS(26));
    int stop = MulDiv(c.right - 2 * pad - PS(150), 4, std::max(1, (int)(LOWORD(GetDialogBaseUnits()))));
    SendMessageW(P.list, LB_SETTABSTOPS, 1, (LPARAM)&stop);
    ui::darkTheme(P.list, L"DarkMode_Explorer");
    setAccessibleName(P.edit, "shortcut.palette_title");
    setAccessibleName(P.list, "shortcut.palette_list");
    SetWindowSubclass(P.edit, editProc, 92, 0);
    SetWindowSubclass(P.list, listProc, 93, 0);
    fillList();
    SetFocus(P.edit);
}
