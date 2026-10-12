// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Fenêtre « Sortie » : transparence, HDR, taille de sortie, interpolation, volume sonore, titre et crédit. Ces réglages ne sont pas dans la
// carte des réglages (trop nombreux) ; ils sont lus et écrits dans g.base (puis config.ini) avec la même validation que la ligne de commande
// et l'interface Linux / macOS (core/outputopts).
#include "app.h"
#include "core/i18n.h"
#include "core/outputopts.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"

using namespace theme;

namespace {

enum {
    ID_OD_CLOSE = 900, ID_OD_ALPHA, ID_OD_HDR, ID_OD_PEAK, ID_OD_FALL, ID_OD_WHITE, ID_OD_SCALE, ID_OD_FILTER, ID_OD_INTERP, ID_OD_LOUD,
    ID_OD_TITLE, ID_OD_CREDIT, ID_OD_TITLEDUR
};

struct Dlg {
    HWND wnd = nullptr, bClose = nullptr;
    HWND alpha = nullptr, hdr = nullptr, peak = nullptr, fall = nullptr, white = nullptr, scale = nullptr, filter = nullptr, interp = nullptr,
         loud = nullptr, title = nullptr, credit = nullptr, titleDur = nullptr;
    std::vector<HWND> headers;  // titres de section (en couleur d'accentuation)
    UINT dpi = 96;
    bool done = false;
} D;

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }

const wchar_t* kDlgClass = L"ShaderToMP4Output";

HWND make(const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), D.wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g.font, TRUE);
    return c;
}

HWND label(const std::wstring& text, int x, int y, int w, int h = 20) {
    return make(L"STATIC", text.c_str(), SS_LEFT | SS_NOPREFIX, 0, x, y + 3, w, h);
}

HWND header(const std::wstring& text, int y) {
    HWND h = make(L"STATIC", text.c_str(), SS_LEFT | SS_NOPREFIX, 0, 20, y, 560, 22);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.fontBold, TRUE);
    D.headers.push_back(h);
    return h;
}

HWND edit(int id, const std::wstring& text, int x, int y, int w) {
    HWND e = make(L"EDIT", text.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, id, x, y, w, 26, WS_EX_CLIENTEDGE);
    ui::darkTheme(e, L"DarkMode_CFD");
    SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
    return e;
}

HWND combo(int id, const std::vector<std::wstring>& items, int sel, int x, int y, int w) {
    HWND c = make(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, id, x, y, w, 26 + 160);
    ui::darkTheme(c, L"DarkMode_CFD");
    for (const auto& t : items) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t.c_str());
    SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
    SendMessageW(c, CB_SETITEMHEIGHT, (WPARAM)-1, S(18));
    return c;
}

void hint(const std::wstring& text, int y, int h) { make(L"STATIC", text.c_str(), SS_LEFT | SS_NOPREFIX, 0, 20, y, 560, h); }

void updateEnabled() {
    const bool on = SendMessageW(D.hdr, CB_GETCURSEL, 0, 0) == 1;  // HDR10 : MaxCLL, MaxFALL
    const bool any = SendMessageW(D.hdr, CB_GETCURSEL, 0, 0) > 0;
    EnableWindow(D.peak, on);
    EnableWindow(D.fall, on);
    EnableWindow(D.white, any);
}

std::wstring txt(HWND h) {  // texte d'un champ, sans limite de 128 caractères
    std::wstring t((size_t)GetWindowTextLengthW(h) + 1, L' ');
    t.resize((size_t)GetWindowTextW(h, &t[0], (int)t.size()));
    return t;
}

OutputFields readFields() {
    OutputFields f;
    f.alpha = SendMessageW(D.alpha, CB_GETCURSEL, 0, 0) == 1;
    f.hdr = std::max(0, (int)SendMessageW(D.hdr, CB_GETCURSEL, 0, 0));
    f.hdrPeak = txt(D.peak);
    f.hdrFall = txt(D.fall);
    f.hdrWhite = txt(D.white);
    f.scale = txt(D.scale);
    f.scaleFilter = std::max(0, (int)SendMessageW(D.filter, CB_GETCURSEL, 0, 0));
    f.interp = txt(D.interp);
    f.loudnorm = txt(D.loud);
    f.title = txt(D.title);
    f.credit = txt(D.credit);
    f.titleDur = txt(D.titleDur);
    return f;
}

// Les valeurs sont appliquées à g.base et écrites dans config.ini ; faux (message affiché) si l'une est invalide
bool applyAndClose() {
    Settings s = g.base;
    std::wstring err;
    if (!outputFieldsApply(readFields(), s, err)) {
        MessageBoxW(D.wnd, err.c_str(), TX("dialog.invalid_settings").c_str(), MB_ICONWARNING);
        return false;
    }
    g.base = s;
    saveSettings();
    updateEstimate();
    updateForced();
    if (g.player && g.player->running()) startPlayer();  // l'aperçu en lecture reprend les nouveaux réglages (transparence, HDR)
    DestroyWindow(D.wnd);
    return true;
}

LRESULT CALLBACK dlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* d = (const DRAWITEMSTRUCT*)lp;
            if (d->CtlType == ODT_BUTTON) {
                ui::drawButton(d, D.dpi);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, IsWindowEnabled((HWND)lp) ? cref(TEXT) : cref(FAINT));
            SetBkColor(dc, cref(FIELD));
            return (LRESULT)g.brField;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            const bool head = std::find(D.headers.begin(), D.headers.end(), (HWND)lp) != D.headers.end();
            SetBkColor(dc, cref(CARD));
            SetTextColor(dc, head ? cref(ACCENT) : cref(MUTED));
            return (LRESULT)g.brCard;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == ID_OD_CLOSE || LOWORD(wp) == IDCANCEL) applyAndClose();
            else if (LOWORD(wp) == ID_OD_HDR && HIWORD(wp) == CBN_SELCHANGE) updateEnabled();
            return 0;
        case WM_CLOSE: applyAndClose(); return 0;
        case WM_DESTROY: D.done = true; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void showOutput(HWND parent) {
    static bool registered = false;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = dlgProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = CreateSolidBrush(cref(CARD));
        wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
        wc.lpszClassName = kDlgClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    D = Dlg();
    D.dpi = theme::dpiFor(parent);

    OutputFields f;
    outputFieldsFrom(g.base, f);

    const DWORD style = WS_CAPTION | WS_SYSMENU;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int clientH = 620;
    RECT want{0, 0, S(600), S(clientH)};
    AdjustWindowRectExForDpi(&want, style, FALSE, WS_EX_DLGMODALFRAME, D.dpi);
    RECT pr;
    GetWindowRect(parent, &pr);
    const int ww = want.right - want.left, wh = want.bottom - want.top;
    const int x = pr.left + ((pr.right - pr.left) - ww) / 2, y = std::max((int)work.top, (int)(pr.top + ((pr.bottom - pr.top) - wh) / 2));
    D.wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kDlgClass, TX("ui.output.title").c_str(), style | WS_VISIBLE | WS_CLIPCHILDREN, x, y, ww, wh, parent, nullptr,
                            hi, nullptr);
    if (!D.wnd) return;
    ui::darkTitleBar(D.wnd);

    const std::vector<std::wstring> offOn = {TX("ui.output.off"), TX("ui.output.on")};
    const int lx = 20, fx = 152, rx = 330, rf = 462;  // colonne gauche : libellé / champ ; colonne droite : libellé / champ

    header(TX("ui.output.sec_alpha"), 12);
    label(TX("ui.output.alpha"), lx, 40, 130);
    D.alpha = combo(ID_OD_ALPHA, offOn, f.alpha ? 1 : 0, fx, 40, 150);
    hint(TX("ui.output.hint_alpha"), 74, 34);

    header(TX("ui.output.sec_hdr"), 116);
    label(TX("ui.output.hdr"), lx, 144, 130);
    D.hdr = combo(ID_OD_HDR, {TX("ui.output.off"), L"HDR10 (PQ)", L"HLG"}, f.hdr, fx, 144, 150);
    label(TX("ui.output.white"), rx, 144, 130);
    D.white = edit(ID_OD_WHITE, f.hdrWhite, rf, 144, 118);
    label(TX("ui.output.peak"), lx, 176, 130);
    D.peak = edit(ID_OD_PEAK, f.hdrPeak, fx, 176, 150);
    label(TX("ui.output.fall"), rx, 176, 130);
    D.fall = edit(ID_OD_FALL, f.hdrFall, rf, 176, 118);
    hint(TX("ui.output.hint_hdr"), 210, 34);

    header(TX("ui.output.sec_size"), 252);
    label(TX("ui.output.scale"), lx, 280, 130);
    D.scale = edit(ID_OD_SCALE, f.scale, fx, 280, 150);
    label(TX("ui.output.filter"), rx, 280, 130);
    std::vector<std::wstring> filters;
    for (int i = 0; i < kScaleFilterCount; i++) filters.push_back(TX(scaleFilterLabelKey(i)));
    D.filter = combo(ID_OD_FILTER, filters, f.scaleFilter, rf, 280, 118);
    SendMessageW(D.filter, CB_SETDROPPEDWIDTH, S(240), 0);
    label(TX("ui.output.interp"), lx, 312, 130);
    D.interp = edit(ID_OD_INTERP, f.interp, fx, 312, 150);
    label(TX("ui.output.loudnorm"), rx, 312, 130);
    D.loud = edit(ID_OD_LOUD, f.loudnorm, rf, 312, 118);
    hint(TX("ui.output.hint_size"), 346, 50);

    header(TX("ui.output.sec_text"), 404);
    label(TX("ui.output.text_title"), lx, 432, 130);
    D.title = edit(ID_OD_TITLE, f.title, fx, 432, 428);
    label(TX("ui.output.credit"), lx, 464, 130);
    D.credit = edit(ID_OD_CREDIT, f.credit, fx, 464, 428);
    label(TX("ui.output.titledur"), lx, 496, 130);
    D.titleDur = edit(ID_OD_TITLEDUR, f.titleDur, fx, 496, 150);
    hint(TX("ui.output.hint_text"), 530, 34);

    D.bClose = CreateWindowExW(0, L"BUTTON", TX("ui.params.close").c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, S(600 - 20 - 130), S(clientH - 50),
                               S(130), S(36), D.wnd, (HMENU)(INT_PTR)ID_OD_CLOSE, hi, nullptr);
    ui::setupButton(D.bClose, IC_CHECK, ui::BtnStyle::Primary, CARD);
    SendMessageW(D.bClose, WM_SETFONT, (WPARAM)g.font, TRUE);
    for (HWND c : {D.alpha, D.hdr, D.filter}) SendMessageW(c, CB_SETITEMHEIGHT, (WPARAM)-1, S(18));
    updateEnabled();

    // Boîte modale : la fenêtre principale est désactivée pendant l'affichage
    EnableWindow(parent, FALSE);
    SetFocus(D.alpha);
    MSG m;
    while (!D.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(D.wnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}
