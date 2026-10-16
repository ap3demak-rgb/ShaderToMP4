// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Fenêtre « Sortie » en deux pages. Page 1 : transparence, HDR, taille de sortie, interpolation, volume sonore, titre et crédit. Page 2
// (« Avancé ») : nom du fichier, fichier existant, métadonnées, fin de lot, parties, file de lecture GPU, obturateur du flou. Ces réglages ne
// sont pas dans la carte des réglages (trop nombreux) ; ils sont lus et écrits dans g.base (puis config.ini) avec la même validation que la
// ligne de commande et l'interface Linux / macOS (core/outputopts).
#include "app.h"
#include "accessible.h"
#include "core/i18n.h"
#include "core/outputopts.h"
#include "core/publish.h"
#include "core/timemap.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"

using namespace theme;

namespace {

enum {
    ID_OD_CLOSE = 900, ID_OD_ALPHA, ID_OD_HDR, ID_OD_PEAK, ID_OD_FALL, ID_OD_WHITE, ID_OD_SCALE, ID_OD_FILTER, ID_OD_INTERP, ID_OD_LOUD,
    ID_OD_TITLE, ID_OD_CREDIT, ID_OD_TITLEDUR, ID_OD_NEXT, ID_OD_NAME, ID_OD_EXISTS, ID_OD_META, ID_OD_ENDACT, ID_OD_CHUNK, ID_OD_PBO, ID_OD_SHUTTER, ID_OD_VARIANTS,
    ID_OD_LOOPMODE, ID_OD_LOOPFADE, ID_OD_TIMEMAP, ID_OD_SPEED, ID_OD_SAMPLES, ID_OD_TILE, ID_OD_DPI, ID_OD_FINDLOOP, ID_OD_TIME, ID_OD_CURVE, ID_OD_AUDIOF, ID_OD_STILLF, ID_OD_PRECHECK, ID_OD_PUBLISH, ID_OD_TARGET, ID_OD_TSIZE, ID_OD_CROP, ID_OD_MASTER, ID_OD_CACHEMAX, ID_OD_EMBED, ID_OD_ACREDIT, ID_OD_ANIMP, ID_OD_PURGE
};

struct Dlg {
    HWND wnd = nullptr, bClose = nullptr, bNext = nullptr;
    HWND alpha = nullptr, hdr = nullptr, peak = nullptr, fall = nullptr, white = nullptr, scale = nullptr, filter = nullptr, interp = nullptr,
         loud = nullptr, title = nullptr, credit = nullptr, titleDur = nullptr;
    HWND name = nullptr, exists = nullptr, meta = nullptr, endAct = nullptr, chunk = nullptr, pbo = nullptr, shutter = nullptr, variants = nullptr;
    HWND loopMode = nullptr, loopFade = nullptr, timemap = nullptr, speed = nullptr, samples = nullptr, tile = nullptr, dpiEdit = nullptr, audioF = nullptr, curve = nullptr, stillF = nullptr, precheck = nullptr;
    HWND target = nullptr, tsize = nullptr, crop = nullptr, master = nullptr, cacheMax = nullptr, embed = nullptr, aCredit = nullptr, animP = nullptr;
    std::vector<HWND> headers;  // titres de section (en couleur d'accentuation)
    UINT dpi = 96;
    int page = 0;   // 0 sortie, 1 avance, 2 temps et boucle, 3 publication
    int next = -1;  // page a ouvrir apres la fermeture (-1 = aucune)
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
    if (D.page != 0) return;
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

// Part des reglages actuels : la page affichee ne remplace que ses propres champs
OutputFields readFields() {
    OutputFields f;
    outputFieldsFrom(g.base, f);
    if (D.page == 0) {
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
    } else if (D.page == 1) {
        f.name = txt(D.name);
        f.exists = std::max(0, (int)SendMessageW(D.exists, CB_GETCURSEL, 0, 0));
        f.metadata = SendMessageW(D.meta, CB_GETCURSEL, 0, 0) == 1;
        f.endAction = std::max(0, (int)SendMessageW(D.endAct, CB_GETCURSEL, 0, 0));
        f.chunk = txt(D.chunk);
        f.pbo = txt(D.pbo);
        f.shutter = txt(D.shutter);
        f.variants = txt(D.variants);
        f.precheck = SendMessageW(D.precheck, CB_GETCURSEL, 0, 0) == 1;
    } else if (D.page == 3) {
        f.target = txt(D.target);
        f.targetSize = txt(D.tsize);
        f.crop = txt(D.crop);
        f.cacheMax = txt(D.cacheMax);
        f.masterCache = SendMessageW(D.master, CB_GETCURSEL, 0, 0) == 1;
        f.embed = SendMessageW(D.embed, CB_GETCURSEL, 0, 0) == 1;
        f.autoCredit = SendMessageW(D.aCredit, CB_GETCURSEL, 0, 0) == 1;
        f.animPreview = std::max(0, (int)SendMessageW(D.animP, CB_GETCURSEL, 0, 0));
    } else {
        f.loopMode = std::max(0, (int)SendMessageW(D.loopMode, CB_GETCURSEL, 0, 0));
        f.audioFollow = SendMessageW(D.audioF, CB_GETCURSEL, 0, 0) == 1;
        f.stillFormat = std::max(0, (int)SendMessageW(D.stillF, CB_GETCURSEL, 0, 0));
        f.loopFade = txt(D.loopFade);
        f.timemap = txt(D.timemap);
        f.speed = txt(D.speed);
        f.samples = txt(D.samples);
        f.tile = txt(D.tile);
        f.dpi = txt(D.dpiEdit);
    }
    return f;
}

// Les valeurs sont appliquees a g.base et ecrites dans config.ini ; faux (message affiche) si l'une est invalide
bool applyAndClose(int next) {
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
    if (g.player && g.player->running()) startPlayer();  // l'apercu en lecture reprend les nouveaux reglages (transparence, HDR)
    D.next = next;
    DestroyWindow(D.wnd);
    return true;
}

// Apercu de la courbe de temps : l'instant du shader (ordonnee) selon l'instant de la video (abscisse), avec la diagonale du temps reel en pointille ;
// suit les champs au fur et a mesure de la saisie (rouge si le texte de la courbe est invalide)
void drawCurve(HDC dc, const RECT& r) {
    HBRUSH bg = CreateSolidBrush(cref(FIELD));
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    const int pad = S(6), w = r.right - r.left - 2 * pad, h = r.bottom - r.top - 2 * pad;
    if (w < 10 || h < 10) return;
    OutputFields f;
    f.timemap = txt(D.timemap);
    f.speed = txt(D.speed);
    f.loopMode = (int)SendMessageW(D.loopMode, CB_GETCURSEL, 0, 0);
    std::string curve;
    for (char c : U8(f.timemap))
        if (!isspace((unsigned char)c)) curve += c;
    double speed = 1.0;
    {
        std::wstring sp = f.speed;
        for (auto& c : sp)
            if (c == L',') c = L'.';
        wchar_t* end = nullptr;
        const double v = wcstod(sp.c_str(), &end);
        if (!sp.empty() && end != sp.c_str() && *end == 0 && v != 0 && std::fabs(v) <= 100) speed = v;
    }
    std::vector<double> y;
    const bool ok = timeCurveSamples(curve, speed, f.loopMode == 2, std::max(0.1, g.base.duration), 120, y);
    HPEN grid = CreatePen(PS_DOT, 1, cref(FAINT));
    HGDIOBJ old = SelectObject(dc, grid);
    SetBkMode(dc, TRANSPARENT);
    if (!ok) {
        SetTextColor(dc, cref(ERR));
        std::wstring m = TX("error.timemap_invalid_short");
        TextOutW(dc, r.left + pad, r.top + pad, m.c_str(), (int)m.size());
        SelectObject(dc, old);
        DeleteObject(grid);
        return;
    }
    double lo = 0, hi = std::max(0.1, g.base.duration);  // l'echelle contient toujours le temps reel
    for (double v : y) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    auto px = [&](int i) { return r.left + pad + (int)((double)w * i / (double)(y.size() - 1)); };
    auto py = [&](double v) { return r.top + pad + h - (int)((v - lo) / (hi - lo) * h); };
    MoveToEx(dc, px(0), py(0), nullptr);  // temps reel : de 0 a la duree
    LineTo(dc, px((int)y.size() - 1), py(std::max(0.1, g.base.duration)));
    SelectObject(dc, old);
    DeleteObject(grid);
    HPEN line = CreatePen(PS_SOLID, std::max(1, S(2)), cref(ACCENT));
    old = SelectObject(dc, line);
    MoveToEx(dc, px(0), py(y[0]), nullptr);
    for (size_t i = 1; i < y.size(); i++) LineTo(dc, px((int)i), py(y[i]));
    SelectObject(dc, old);
    DeleteObject(line);
    SetTextColor(dc, cref(MUTED));
    wchar_t lab[64];
    swprintf(lab, 64, L"%.4g s", hi);
    TextOutW(dc, r.left + pad + 2, r.top + pad, lab, (int)wcslen(lab));
    swprintf(lab, 64, L"%.4g s", lo);
    SIZE sz{};
    GetTextExtentPoint32W(dc, lab, (int)wcslen(lab), &sz);
    TextOutW(dc, r.left + pad + 2, r.bottom - pad - sz.cy, lab, (int)wcslen(lab));
}

// Fenetre de l'apercu de la courbe (classe propre : un STATIC en dessin parent ne recevait pas WM_DRAWITEM dans cette boite)
LRESULT CALLBACK curveProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        drawCurve(dc, r);
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(h, msg, wp, lp);
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
            if (LOWORD(wp) == ID_OD_CLOSE || LOWORD(wp) == IDCANCEL) applyAndClose(-1);
            else if (LOWORD(wp) == ID_OD_NEXT) applyAndClose(D.page == 1 || D.page == 3 ? 0 : 1);
            else if (LOWORD(wp) == ID_OD_PUBLISH) applyAndClose(3);
            else if (LOWORD(wp) == ID_OD_PURGE) {
                const uint64_t freed = cachePurge(g.exeDir);
                MessageBoxW(D.wnd, TXF("log.cache_purged", (double)freed / 1e6).c_str(), TX("ui.output.publish_title").c_str(), MB_ICONINFORMATION);
            }
            else if (LOWORD(wp) == ID_OD_TIME) applyAndClose(2);
            else if (D.page == 2 && (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == CBN_SELCHANGE) && D.curve) InvalidateRect(D.curve, nullptr, FALSE);
            else if (LOWORD(wp) == ID_OD_FINDLOOP) applyAndClose(-2);
            else if (LOWORD(wp) == ID_OD_HDR && HIWORD(wp) == CBN_SELCHANGE) updateEnabled();
            return 0;
        case WM_CLOSE: applyAndClose(-1); return 0;
        case WM_DESTROY: D.done = true; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND button(const char* key, int id, int icon, ui::BtnStyle style, int x, int y, int w, HINSTANCE hi) {
    HWND b = CreateWindowExW(0, L"BUTTON", TX(key).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, S(x), S(y), S(w), S(36), D.wnd, (HMENU)(INT_PTR)id, hi,
                             nullptr);
    ui::setupButton(b, icon, style, CARD);
    SendMessageW(b, WM_SETFONT, (WPARAM)g.font, TRUE);
    return b;
}

// Affiche une page et rend la page suivante a ouvrir (-1 = fin)
int runPage(HWND parent, int page) {
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
        WNDCLASSEXW cc{};
        cc.cbSize = sizeof(cc);
        cc.lpfnWndProc = curveProc;
        cc.hInstance = hi;
        cc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        cc.lpszClassName = L"ShaderToMP4Curve";
        RegisterClassExW(&cc);
    }
    D = Dlg();
    D.dpi = theme::dpiFor(parent);
    D.page = page;

    OutputFields f;
    outputFieldsFrom(g.base, f);

    const DWORD style = WS_CAPTION | WS_SYSMENU;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int clientH = page == 0 ? 620 : page == 1 ? 452 : page == 3 ? 470 : 450;
    RECT want{0, 0, S(600), S(clientH)};
    AdjustWindowRectExForDpi(&want, style, FALSE, WS_EX_DLGMODALFRAME, D.dpi);
    RECT pr;
    GetWindowRect(parent, &pr);
    const int ww = want.right - want.left, wh = want.bottom - want.top;
    const int x = pr.left + ((pr.right - pr.left) - ww) / 2, y = std::max((int)work.top, (int)(pr.top + ((pr.bottom - pr.top) - wh) / 2));
    D.wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kDlgClass, TX(page == 0 ? "ui.output.title" : page == 1 ? "ui.output.adv_title" : page == 3 ? "ui.output.publish_title" : "ui.output.time_title").c_str(), style | WS_VISIBLE | WS_CLIPCHILDREN, x, y,
                            ww, wh, parent, nullptr, hi, nullptr);
    if (!D.wnd) return -1;
    ui::darkTitleBar(D.wnd);

    const int lx = 20, fx = 152, rx = 330, rf = 462;  // colonne gauche : libellé / champ ; colonne droite : libellé / champ
    if (page == 0) {
        const std::vector<std::wstring> offOn = {TX("ui.output.off"), TX("ui.output.on")};

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
    } else if (page == 2) {
        const std::vector<std::wstring> offOn = {TX("ui.output.off"), TX("ui.output.on")};
        header(TX("ui.output.sec_time"), 12);
        label(TX("ui.output.loopmode"), lx, 40, 130);
        D.loopMode = combo(ID_OD_LOOPMODE, {TX("ui.loopmode.none"), TX("ui.loopmode.crossfade"), TX("ui.loopmode.pingpong")}, f.loopMode, fx, 40, 150);
        SendMessageW(D.loopMode, CB_SETDROPPEDWIDTH, S(220), 0);
        label(TX("ui.output.loopfade"), rx, 40, 130);
        D.loopFade = edit(ID_OD_LOOPFADE, f.loopFade, rf, 40, 118);
        label(TX("ui.output.timemap"), lx, 72, 130);
        D.timemap = edit(ID_OD_TIMEMAP, f.timemap, fx, 72, 428);
        label(TX("ui.output.speed"), lx, 104, 130);
        D.speed = edit(ID_OD_SPEED, f.speed, fx, 104, 150);
        label(TX("ui.output.audiofollow"), rx, 104, 130);
        D.audioF = combo(ID_OD_AUDIOF, offOn, f.audioFollow ? 1 : 0, rf, 104, 118);
        D.curve = make(L"ShaderToMP4Curve", L"", 0, ID_OD_CURVE, 20, 140, 560, 76);
        hint(TX("ui.output.hint_time"), 222, 50);
        header(TX("ui.output.sec_still"), 282);
        label(TX("ui.output.samples"), lx, 310, 130);
        D.samples = edit(ID_OD_SAMPLES, f.samples, fx, 310, 150);
        label(TX("ui.output.tile"), rx, 310, 130);
        D.tile = edit(ID_OD_TILE, f.tile, rf, 310, 118);
        label(TX("ui.output.dpi"), lx, 342, 130);
        D.dpiEdit = edit(ID_OD_DPI, f.dpi, fx, 342, 150);
        label(TX("ui.output.stillformat"), rx, 342, 130);
        D.stillF = combo(ID_OD_STILLF, {L"PNG", L"TIFF"}, f.stillFormat, rf, 342, 118);
    } else if (page == 3) {
        const std::vector<std::wstring> offOn = {TX("ui.output.off"), TX("ui.output.on")};
        header(TX("ui.output.sec_publish"), 12);
        label(TX("ui.output.target"), lx, 40, 130);
        D.target = edit(ID_OD_TARGET, f.target, fx, 40, 150);
        label(TX("ui.output.targetsize"), rx, 40, 130);
        D.tsize = edit(ID_OD_TSIZE, f.targetSize, rf, 40, 118);
        label(TX("ui.output.crop"), lx, 72, 130);
        D.crop = edit(ID_OD_CROP, f.crop, fx, 72, 150);
        label(TX("ui.output.autocredit"), rx, 72, 130);
        D.aCredit = combo(ID_OD_ACREDIT, offOn, f.autoCredit ? 1 : 0, rf, 72, 118);
        label(TX("ui.output.animpreview"), lx, 104, 130);
        D.animP = combo(ID_OD_ANIMP, {TX("ui.output.off"), L"GIF", L"WebP"}, f.animPreview, fx, 104, 150);
        hint(TX("ui.output.hint_publish"), 140, 66);
        header(TX("ui.output.sec_trace"), 220);
        label(TX("ui.output.embed"), lx, 248, 130);
        D.embed = combo(ID_OD_EMBED, offOn, f.embed ? 1 : 0, fx, 248, 150);
        label(TX("ui.output.mastercache"), rx, 248, 130);
        D.master = combo(ID_OD_MASTER, offOn, f.masterCache ? 1 : 0, rf, 248, 118);
        label(TX("ui.output.cachemax"), lx, 280, 130);
        D.cacheMax = edit(ID_OD_CACHEMAX, f.cacheMax, fx, 280, 150);
        hint(TX("ui.output.hint_trace"), 316, 66);
    } else {
        const std::vector<std::wstring> offOn = {TX("ui.output.off"), TX("ui.output.on")};

        header(TX("ui.output.sec_files"), 12);
        label(TX("ui.output.name"), lx, 40, 130);
        D.name = edit(ID_OD_NAME, f.name, fx, 40, 428);
        label(TX("ui.output.exists"), lx, 72, 130);
        D.exists = combo(ID_OD_EXISTS, {TX("ui.output.exists_overwrite"), TX("ui.output.exists_number"), TX("ui.output.exists_skip")}, f.exists, fx, 72, 150);
        SendMessageW(D.exists, CB_SETDROPPEDWIDTH, S(220), 0);
        label(TX("ui.output.metadata"), rx, 72, 130);
        D.meta = combo(ID_OD_META, offOn, f.metadata ? 1 : 0, rf, 72, 118);
        label(TX("ui.output.endaction"), lx, 104, 130);
        D.endAct = combo(ID_OD_ENDACT, {TX("ui.output.end_none"), TX("ui.output.end_folder"), TX("ui.output.end_sleep")}, f.endAction, fx, 104, 150);
        SendMessageW(D.endAct, CB_SETDROPPEDWIDTH, S(240), 0);
        label(TX("ui.output.variants"), lx, 136, 130);
        D.variants = edit(ID_OD_VARIANTS, f.variants, fx, 136, 428);
        hint(TX("ui.output.hint_files"), 172, 50);

        header(TX("ui.output.sec_engine"), 242);
        label(TX("ui.output.chunk"), lx, 270, 130);
        D.chunk = edit(ID_OD_CHUNK, f.chunk, fx, 270, 150);
        label(TX("ui.output.pbo"), rx, 270, 130);
        D.pbo = edit(ID_OD_PBO, f.pbo, rf, 270, 118);
        label(TX("ui.output.shutter"), lx, 302, 130);
        D.shutter = edit(ID_OD_SHUTTER, f.shutter, fx, 302, 150);
        label(TX("ui.output.precheck"), rx, 302, 130);
        D.precheck = combo(ID_OD_PRECHECK, offOn, f.precheck ? 1 : 0, rf, 302, 118);
        hint(TX("ui.output.hint_engine"), 338, 50);
    }

    // noms accessibles : chaque champ est annonce par son libelle (lecteurs d'ecran)
    if (page == 0) {
        const struct { HWND c; const char* key; } names[] = {
            {D.alpha, "ui.output.alpha"}, {D.hdr, "ui.output.hdr"}, {D.peak, "ui.output.peak"}, {D.fall, "ui.output.fall"}, {D.white, "ui.output.white"},
            {D.scale, "ui.output.scale"}, {D.filter, "ui.output.filter"}, {D.interp, "ui.output.interp"}, {D.loud, "ui.output.loudnorm"},
            {D.title, "ui.output.text_title"}, {D.credit, "ui.output.credit"}, {D.titleDur, "ui.output.titledur"}};
        for (const auto& n : names) setAccessibleName(n.c, n.key);
    } else if (page == 1) {
        const struct { HWND c; const char* key; } names[] = {
            {D.name, "ui.output.name"}, {D.exists, "ui.output.exists"}, {D.meta, "ui.output.metadata"}, {D.endAct, "ui.output.endaction"},
            {D.chunk, "ui.output.chunk"}, {D.pbo, "ui.output.pbo"}, {D.shutter, "ui.output.shutter"}, {D.variants, "ui.output.variants"},
            {D.precheck, "ui.output.precheck"}};
        for (const auto& n : names) setAccessibleName(n.c, n.key);
    } else if (page == 3) {
        const struct { HWND c; const char* key; } names[] = {
            {D.target, "ui.output.target"}, {D.tsize, "ui.output.targetsize"}, {D.crop, "ui.output.crop"}, {D.aCredit, "ui.output.autocredit"},
            {D.animP, "ui.output.animpreview"}, {D.embed, "ui.output.embed"}, {D.master, "ui.output.mastercache"}, {D.cacheMax, "ui.output.cachemax"}};
        for (const auto& n : names) setAccessibleName(n.c, n.key);
    } else {
        const struct { HWND c; const char* key; } names[] = {
            {D.loopMode, "ui.output.loopmode"}, {D.loopFade, "ui.output.loopfade"}, {D.timemap, "ui.output.timemap"}, {D.speed, "ui.output.speed"},
            {D.audioF, "ui.output.audiofollow"}, {D.curve, "ui.output.curve_name"}, {D.samples, "ui.output.samples"}, {D.tile, "ui.output.tile"},
            {D.dpiEdit, "ui.output.dpi"}, {D.stillF, "ui.output.stillformat"}};
        for (const auto& n : names) setAccessibleName(n.c, n.key);
    }
    D.bNext = button(page == 1 || page == 3 ? "ui.output.back_btn" : "ui.output.adv_btn", ID_OD_NEXT, IC_SLIDERS, ui::BtnStyle::Secondary, 20, clientH - 50, 150, hi);
    D.bClose = button("ui.params.close", ID_OD_CLOSE, IC_CHECK, ui::BtnStyle::Primary, 600 - 20 - 130, clientH - 50, 130, hi);
    if (page == 0) button("ui.output.publish_btn", ID_OD_PUBLISH, IC_GLOBE, ui::BtnStyle::Secondary, 190, clientH - 50, 240, hi);
    if (page == 3) button("ui.output.purge_btn", ID_OD_PURGE, IC_TRASH, ui::BtnStyle::Secondary, 190, clientH - 50, 240, hi);
    if (page == 1) button("ui.output.time_btn", ID_OD_TIME, IC_FILM, ui::BtnStyle::Secondary, 190, clientH - 50, 240, hi);
    if (page == 2) button("ui.output.findloop_btn", ID_OD_FINDLOOP, IC_EYE, ui::BtnStyle::Secondary, 190, clientH - 50, 240, hi);
    updateEnabled();

    // Boîte modale : la fenêtre principale est désactivée pendant l'affichage
    EnableWindow(parent, FALSE);
    SetFocus(page == 0 ? D.alpha : page == 1 ? D.name : page == 3 ? D.target : D.loopMode);
    MSG m;
    while (!D.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(D.wnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return D.next;
}

}  // namespace

void showOutput(HWND parent) {
    int page = 0;
    while (page >= 0) page = runPage(parent, page);
    if (page == -2) PostMessageW(parent, WM_COMMAND, MAKEWPARAM(ID_FINDLOOP, 0), 0);  // « Chercher la boucle » : les champs sont enregistres, la recherche demarre
}
