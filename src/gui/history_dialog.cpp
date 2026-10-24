// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Historique des rendus : la liste de history.jsonl (date, shader, resultat, duree, sortie) avec « Refaire ce rendu » (memes reglages que la derniere fois)
// et « Ouvrir le dossier ». Le fichier ne quitte jamais le dossier du programme.
#include "accessible.h"
#include "app.h"
#include "core/i18n.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"
#include <commctrl.h>

using namespace theme;

namespace {

enum { ID_HIS_LIST = 960, ID_HIS_REDO, ID_HIS_FOLDER, ID_HIS_CLEAR, ID_HIS_CLOSE = IDCANCEL };

struct Dialog {
    HWND wnd = nullptr, list = nullptr, bRedo = nullptr, bFolder = nullptr, bClear = nullptr, bClose = nullptr, hint = nullptr;
    UINT dpi = 96;
    std::vector<HistoryEntry> entries;
} D;

const wchar_t* kClass = L"ShaderToMP4History";

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }

std::wstring leaf(const std::wstring& p) {
    const size_t cut = p.find_last_of(L"\\/");
    return cut == std::wstring::npos ? p : p.substr(cut + 1);
}

std::wstring resultText(const std::string& r) { return TX(r == "ok" ? "status.done" : r == "cancelled" ? "status.cancelled" : "status.failed"); }

int selectedEntry() {
    const int i = (int)SendMessageW(D.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    return i >= 0 && i < (int)D.entries.size() ? i : -1;
}

void setCell(int row, int col, const std::wstring& t) {
    LVITEMW it{};
    it.iSubItem = col;
    it.pszText = const_cast<LPWSTR>(t.c_str());
    SendMessageW(D.list, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&it);
}

void fill() {
    D.entries = loadHistory(g.exeDir);
    SendMessageW(D.list, LVM_DELETEALLITEMS, 0, 0);
    for (size_t i = 0; i < D.entries.size(); i++) {
        const HistoryEntry& e = D.entries[i];
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = (int)i;
        const std::wstring when = W(e.time);
        it.pszText = const_cast<LPWSTR>(when.c_str());
        SendMessageW(D.list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        setCell((int)i, 1, leaf(e.shader));
        setCell((int)i, 2, resultText(e.result));
        setCell((int)i, 3, fmtTime(e.seconds));
        setCell((int)i, 4, e.output.empty() ? L"" : leaf(e.output));
    }
    if (!D.entries.empty()) {
        LVITEMW st{};
        st.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        st.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(D.list, LVM_SETITEMSTATE, 0, (LPARAM)&st);
    }
    SetWindowTextW(D.hint, D.entries.empty() ? TX("ui.history.empty").c_str() : TXF("ui.history.count", (int)D.entries.size()).c_str());
    EnableWindow(D.bClear, !D.entries.empty());
    EnableWindow(D.bRedo, !D.entries.empty());
    EnableWindow(D.bFolder, !D.entries.empty());
}

void layoutControls() {
    RECT c;
    GetClientRect(D.wnd, &c);
    const int pad = S(14), bh = S(34), hh = S(22);
    MoveWindow(D.hint, pad, pad, c.right - 2 * pad, hh, TRUE);
    MoveWindow(D.list, pad, pad + hh + S(6), c.right - 2 * pad, c.bottom - 3 * pad - hh - S(6) - bh, TRUE);
    int x = pad;
    const int y = c.bottom - pad - bh;
    MoveWindow(D.bRedo, x, y, S(190), bh, TRUE);
    x += S(190) + S(8);
    MoveWindow(D.bFolder, x, y, S(150), bh, TRUE);
    x += S(150) + S(8);
    MoveWindow(D.bClear, x, y, S(150), bh, TRUE);
    MoveWindow(D.bClose, c.right - pad - S(110), y, S(110), bh, TRUE);
    const int total = c.right - 2 * pad - GetSystemMetrics(SM_CXVSCROLL);
    const int w[5] = {S(150), total / 3, S(100), S(80), 0};
    for (int i = 0; i < 4; i++) SendMessageW(D.list, LVM_SETCOLUMNWIDTH, (WPARAM)i, w[i]);
    SendMessageW(D.list, LVM_SETCOLUMNWIDTH, 4, std::max(S(120), total - (w[0] + w[1] + w[2] + w[3])));
}

// Refaire un rendu : le shader rejoint la liste (s'il n'y est pas), les reglages notes deviennent ses reglages propres, et ce travail seul est lance
void redo(const HistoryEntry& e) {
    if (g.running) {
        MessageBoxW(D.wnd, TX("ui.history.busy").c_str(), TX("ui.history.title").c_str(), MB_ICONINFORMATION);
        return;
    }
    if (!fileExists(e.shader)) {
        MessageBoxW(D.wnd, TXF("ui.history.missing", e.shader.c_str()).c_str(), TX("ui.history.title").c_str(), MB_ICONWARNING);
        return;
    }
    stopPlayer();
    Settings cur = g.base;
    std::wstring err;
    if (uiToSettings(cur, err)) g.base = cur;
    flushFileScope();
    if (g.fileScope.on && g.fileScope.path == e.shader) {  // ses reglages propres vont etre remplaces : on quitte cet etat sans les reecrire
        Settings back = g.base;
        copyMainSettings(back, g.fileScope.general);
        g.base = back;
        g.fileScope = App::FileScope();
    }
    addFiles({e.shader});
    int row = -1;
    for (size_t i = 0; i < g.paths.size(); i++)
        if (_wcsicmp(g.paths[i].c_str(), e.shader.c_str()) == 0) row = (int)i;
    if (row < 0) return;
    saveFileSettings(g.exeDir, g.paths[(size_t)row], e.settings);
    LVITEMW st{};
    st.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
    st.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)row, (LPARAM)&st);
    g.fileScope.shown = L"";
    syncFileScope();
    updateForced();
    DestroyWindow(D.wnd);
    startJobs(false, 0, 0, 0, false, true);
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: layoutControls(); return 0;
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
            if (d->CtlType == ODT_BUTTON) {
                ui::drawButton(d, D.dpi);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, cref(MUTED));
            SetBkColor(hdc, cref(BG));
            static HBRUSH br = CreateSolidBrush(cref(BG));
            return (LRESULT)br;
        }
        case WM_NOTIFY: {
            NMHDR* nh = (NMHDR*)lp;
            if (nh->hwndFrom == D.list && nh->code == NM_DBLCLK) {
                if (const int i = selectedEntry(); i >= 0) redo(D.entries[(size_t)i]);
            } else if (nh->hwndFrom == D.list && nh->code == NM_CUSTOMDRAW) {  // resultat colore comme dans la liste principale
                NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
                switch (cd->nmcd.dwDrawStage) {
                    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT: return CDRF_NOTIFYSUBITEMDRAW;
                    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
                        const int i = (int)cd->nmcd.dwItemSpec;
                        COLORREF c = cref(TEXT);
                        if (cd->iSubItem == 2 && i >= 0 && i < (int)D.entries.size()) c = D.entries[(size_t)i].result == "ok" ? cref(OK) : D.entries[(size_t)i].result == "cancelled" ? cref(WARN) : cref(ERR);
                        cd->clrText = c;
                        return CDRF_NEWFONT;
                    }
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == ID_HIS_REDO) {
                if (const int i = selectedEntry(); i >= 0) redo(D.entries[(size_t)i]);
            } else if (id == ID_HIS_FOLDER) {
                if (const int i = selectedEntry(); i >= 0) {
                    const HistoryEntry& e = D.entries[(size_t)i];
                    const std::wstring target = !e.output.empty() && fileExists(e.output) ? e.output : e.shader;
                    ShellExecuteW(h, L"open", L"explorer.exe", (L"/select,\"" + target + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
                }
            } else if (id == ID_HIS_CLEAR) {
                if (MessageBoxW(h, TX("ui.history.clear_ask").c_str(), TX("ui.history.title").c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    clearHistory(g.exeDir);
                    fill();
                }
            } else if (id == IDCANCEL) {
                DestroyWindow(h);
            }
            return 0;
        }
        case WM_SETFOCUS: SetFocus(D.list); return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = S(640);
            mm->ptMinTrackSize.y = S(360);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: D.wnd = nullptr; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void showHistory(HWND owner) {
    if (D.wnd) {
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
    D.dpi = dpiFor(owner ? owner : g.wnd);
    RECT want{0, 0, S(920), S(520)};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectExForDpi(&want, style, FALSE, 0, D.dpi);
    RECT pr{};
    GetWindowRect(owner ? owner : g.wnd, &pr);
    const int ww = want.right - want.left, wh = want.bottom - want.top;
    RECT place = fitToWorkArea({pr.left + 50, pr.top + 50, pr.left + 50 + ww, pr.top + 50 + wh});
    D.wnd = CreateWindowExW(0, kClass, TX("ui.history.title").c_str(), style | WS_VISIBLE | WS_CLIPCHILDREN, place.left, place.top, place.right - place.left, place.bottom - place.top,
                            owner ? owner : g.wnd, nullptr, hi, nullptr);
    if (!D.wnd) return;
    ui::darkTitleBar(D.wnd);
    D.hint = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, D.wnd, nullptr, hi, nullptr);
    SendMessageW(D.hint, WM_SETFONT, (WPARAM)g.font, TRUE);
    D.list = CreateWindowExW(0, L"SysListView32", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 10, 10, D.wnd,
                             (HMENU)(INT_PTR)ID_HIS_LIST, hi, nullptr);
    SendMessageW(D.list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageW(D.list, LVM_SETBKCOLOR, 0, cref(CARD));
    SendMessageW(D.list, LVM_SETTEXTBKCOLOR, 0, cref(CARD));
    SendMessageW(D.list, LVM_SETTEXTCOLOR, 0, cref(TEXT));
    ui::darkTheme(D.list, L"DarkMode_Explorer");
    ui::darkTheme((HWND)SendMessageW(D.list, LVM_GETHEADER, 0, 0), L"DarkMode_ItemsView");  // en-tete des colonnes sombre aussi
    const char* heads[5] = {"ui.history.col_date", "ui.history.col_shader", "ui.history.col_result", "ui.history.col_time", "ui.history.col_output"};
    for (int i = 0; i < 5; i++) {
        const std::wstring t = TX(heads[i]);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 100;
        col.pszText = const_cast<LPWSTR>(t.c_str());
        SendMessageW(D.list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
    }
    auto button = [&](int id, const char* key, int icon, ui::BtnStyle st) {
        HWND b = CreateWindowExW(0, L"BUTTON", TX(key).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, D.wnd, (HMENU)(INT_PTR)id, hi, nullptr);
        ui::setupButton(b, icon, st, BG);
        SendMessageW(b, WM_SETFONT, (WPARAM)g.font, TRUE);
        return b;
    };
    D.bRedo = button(ID_HIS_REDO, "ui.history.redo", IC_PLAY, ui::BtnStyle::Primary);
    D.bFolder = button(ID_HIS_FOLDER, "ui.history.folder", IC_FOLDER, ui::BtnStyle::Secondary);
    D.bClear = button(ID_HIS_CLEAR, "ui.history.clear", IC_TRASH, ui::BtnStyle::Secondary);
    D.bClose = button(ID_HIS_CLOSE, "ui.history.close", IC_CHECK, ui::BtnStyle::Secondary);
    setAccessibleName(D.list, "ui.history.title");
    layoutControls();
    fill();
    SetFocus(D.list);
}
