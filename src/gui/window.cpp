// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "app.h"
#include "core/ffmpeg_download.h"
#include "core/i18n.h"
#include "core/util.h"
#include "accessible.h"
#include "drop_target.h"
#include "taskbar.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"

using namespace theme;

namespace {

int S(int v) { return MulDiv(v, (int)g.dpi, 96); }
float F(int v) { return (float)v * g.dpi / 96.0f; }

// Hauteur d'un champ (edit / liste deroulante) et d'une ligne de reglages
int fieldH() { return S(24); }
int rowH() { return S(34); }

// ---------------------------------------------------------------------------
//  Mise en page (toutes les valeurs en dp, mises a l'echelle par S())
// ---------------------------------------------------------------------------
void layout() {
    RECT rc;
    GetClientRect(g.wnd, &rc);
    const int w = rc.right, h = rc.bottom, m = S(16), gap = S(8), pad = S(16);
    g.cards.clear();

    // --- En-tete et barre d'outils ---
    g.headerRect = {0, 0, w, S(64)};
    {  // boutons « langue » et « À propos » à droite de l'en-tête ; la pastille de version se place à leur gauche
        const int hh = S(32), hy = S(16);
        int hx = w - m - S(40);
        MoveWindow(g.bAbout, hx, hy, S(40), hh, TRUE);
        hx -= gap + S(64);
        MoveWindow(g.bLang, hx, hy, S(64), hh, TRUE);
        hx -= gap + S(40);
        MoveWindow(g.bParams, hx, hy, S(40), hh, TRUE);
        g.chipRight = hx - gap;
    }
    int y = S(64);
    const int bh = S(36);
    int x = m;
    auto lb = [&](HWND b, int bw) {
        MoveWindow(b, x, y, S(bw), bh, TRUE);
        x += S(bw) + gap;
    };
    lb(g.bAdd, 112);
    lb(g.bRemove, 100);
    lb(g.bClear, 84);
    lb(g.bFolder, 104);
    int rx = w - m;
    auto rb = [&](HWND b, int bw) {
        rx -= S(bw);
        MoveWindow(b, rx, y, S(bw), bh, TRUE);
        rx -= gap;
    };
    rb(g.bCancel, 104);
    rb(g.bPause, 108);
    rb(g.bStart, 124);
    rb(g.bSnap, 116);
    y += bh + S(14);

    // --- Carte des reglages ---
    const int titleH = S(48), rows = 6;
    RECT sc{m, y, w - m, y + titleH + rows * rowH() + S(8)};
    g.cards.push_back({sc, TX("ui.card.settings"), IC_SLIDERS});
    {  // preselections rapides, alignees a droite du titre
        static const int cw[5] = {112, 100, 76, 96, 84};
        int cx = sc.right - pad;
        for (int i = 4; i >= 0; i--) {
            cx -= S(cw[i]);
            MoveWindow(g.chips[i], cx, sc.top + S(10), S(cw[i]), S(28), TRUE);
            cx -= S(6);
        }
        const int fl = sc.left + pad + S(200);  // entre le titre de la carte et les pastilles
        MoveWindow(g.lblForced, fl, sc.top + S(15), std::max(0, (int)(cx - fl - S(6))), S(20), TRUE);
    }
    int fx = sc.left + pad, fy = sc.top + titleH;
    auto put = [&](HWND c, int cw, int kind = 0) {  // kind 0 : champ ; 1 : liste deroulante ; 2 : libelle ; 3 : curseur
        const int fh = fieldH();
        int top = fy + (rowH() - fh) / 2;
        if (kind == 1) MoveWindow(c, fx, top, S(cw), fh + S(220), TRUE);
        else if (kind == 2) MoveWindow(c, fx, fy + (rowH() - S(18)) / 2, S(cw), S(18), TRUE);
        else if (kind == 3) MoveWindow(c, fx, fy + (rowH() - S(28)) / 2, S(cw), S(28), TRUE);
        else MoveWindow(c, fx, top, S(cw), fh, TRUE);
        fx += S(cw) + S(6);
    };
    auto nextRow = [&] {
        fx = sc.left + pad;
        fy += rowH();
    };
    put(g.labels[0], 80, 2);
    put(g.cmbRes, 192, 1);
    put(g.edW, 60);
    put(g.labels[1], 12, 2);
    put(g.edH, 60);
    put(g.labels[2], 30, 2);
    put(g.cmbFps, 72, 1);
    put(g.labels[9], 70, 2);
    put(g.cmbSS, 80, 1);
    nextRow();
    put(g.labels[3], 80, 2);
    put(g.edDur, 72);
    put(g.labels[4], 52, 2);
    put(g.edStart, 72);
    put(g.labels[5], 48, 2);
    put(g.cmbCodec, 112, 1);
    put(g.labels[6], 52, 2);
    put(g.cmbPreset, 104, 1);
    put(g.lblEst, 190, 2);
    nextRow();
    put(g.labels[7], 80, 2);
    put(g.trkCrf, 170, 3);
    put(g.lblCrf, 124, 2);
    put(g.labels[8], 76, 2);
    put(g.cmbContainer, 84, 1);
    put(g.labels[18], 62, 2);
    put(g.cmbEncoder, 150, 1);
    nextRow();
    put(g.labels[11], 80, 2);
    put(g.edBitrate, 72);
    put(g.labels[12], 58, 2);
    put(g.cmbColor, 150, 1);
    put(g.labels[13], 34, 2);
    put(g.cmbBlur, 80, 1);
    put(g.labels[14], 66, 2);
    put(g.edLoop, 64);
    put(g.labels[19], 40, 2);
    put(g.cmbDepth, 64, 1);
    nextRow();
    put(g.labels[15], 80, 2);
    put(g.edAudio, std::max(100, (int)((w - 2 * m - 2 * pad) * 96 / (int)g.dpi) - 80 - 100 - 82 - 54 - 48 - 54 - 7 * 6));
    put(g.bAudio, 100, 0);
    put(g.labels[16], 82, 2);
    put(g.edFadeIn, 54);
    put(g.labels[17], 48, 2);
    put(g.edFadeOut, 54);
    nextRow();
    put(g.labels[10], 80, 2);
    put(g.edOut, std::max(100, (int)((w - 2 * m - 2 * pad) * 96 / (int)g.dpi) - 80 - 110 - 100 - 130 - 5 * 6));
    put(g.bBrowse, 110, 0);
    put(g.bLoadP, 100, 0);
    put(g.bSaveP, 130, 0);
    // les boutons de la carte sont un peu plus hauts que les champs
    for (HWND b : {g.bAudio, g.bBrowse, g.bLoadP, g.bSaveP}) {
        RECT r;
        GetWindowRect(b, &r);
        POINT p{r.left, r.top};
        ScreenToClient(g.wnd, &p);
        MoveWindow(b, p.x, p.y - S(2), r.right - r.left, fieldH() + S(4), TRUE);
    }
    y = sc.bottom + S(14);

    // --- Fichiers (gauche) et apercu (droite) ---
    const int progH = S(68), logMin = S(96);
    int avail = h - y - m - progH - 2 * S(14) - logMin;
    int bandH = std::max(S(222), avail * 60 / 100);
    int listW = (w - 2 * m - S(14)) * 55 / 100;
    RECT fr{m, y, m + listW, y + bandH}, pr{m + listW + S(14), y, w - m, y + bandH};
    g.cards.push_back({fr, TX("ui.card.shaders"), IC_FILM});
    g.cards.push_back({pr, TX("ui.card.preview"), IC_EYE});
    g.bandRect = fr;
    RECT lr{fr.left + S(8), fr.top + S(46), fr.right - S(8), fr.bottom - S(10)};
    g.dropRect = lr;
    MoveWindow(g.list, lr.left, lr.top, lr.right - lr.left, lr.bottom - lr.top, TRUE);
    SendMessageW(g.list, LVM_SETCOLUMNWIDTH, 0, std::max(60, (int)(lr.right - lr.left) - S(130) - GetSystemMetrics(SM_CXVSCROLL)));
    SendMessageW(g.list, LVM_SETCOLUMNWIDTH, 1, S(130));
    int px0 = pr.left + pad, pw = pr.right - pr.left - 2 * pad;
    int imgBottom = pr.bottom - S(10) - S(70);
    MoveWindow(g.preview, px0, pr.top + S(46), pw, std::max<int>(S(40), imgBottom - (int)(pr.top + S(46))), TRUE);
    MoveWindow(g.trkPrev, px0, imgBottom + S(6), pw, S(28), TRUE);
    MoveWindow(g.lblPrev, px0, imgBottom + S(40), std::max(40, pw - S(222)), S(20), TRUE);
    MoveWindow(g.bPlay, px0 + pw - S(104) - S(8) - S(104), imgBottom + S(38), S(104), S(28), TRUE);
    MoveWindow(g.bPrev, px0 + pw - S(104), imgBottom + S(38), S(104), S(28), TRUE);
    y += bandH + S(14);

    // --- Progression ---
    RECT pc{m, y, w - m, y + progH};
    g.cards.push_back({pc, L"", 0});
    MoveWindow(g.status, pc.left + pad, pc.top + S(10), pc.right - pc.left - 2 * pad, S(22), TRUE);
    MoveWindow(g.prog, pc.left + pad, pc.top + S(40), pc.right - pc.left - 2 * pad, S(10), TRUE);
    y += progH + S(14);

    // --- Journal ---
    RECT lc{m, y, w - m, std::max((int)y + S(60), h - m)};
    g.cards.push_back({lc, TX("ui.card.log"), IC_TERMINAL});
    MoveWindow(g.log, lc.left + S(8), lc.top + S(44), lc.right - lc.left - S(16), std::max(S(20), (int)(lc.bottom - lc.top) - S(52)), TRUE);
    InvalidateRect(g.wnd, nullptr, FALSE);
}

BOOL CALLBACK setFontProc(HWND c, LPARAM f) {
    SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
    return TRUE;
}

void createFonts() {
    if (g.font) DeleteObject(g.font);
    if (g.fontBold) DeleteObject(g.fontBold);
    g.font = CreateFontW(-MulDiv(9, (int)g.dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g.fontBold = CreateFontW(-MulDiv(9, (int)g.dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Semibold");
    if (g.font) EnumChildWindows(g.wnd, setFontProc, (LPARAM)g.font);
    for (HWND c : {g.cmbRes, g.cmbFps, g.cmbCodec, g.cmbPreset, g.cmbContainer, g.cmbSS, g.cmbColor, g.cmbBlur, g.cmbEncoder, g.cmbDepth})
        SendMessageW(c, CB_SETITEMHEIGHT, (WPARAM)-1, S(18));
    // ligne plus haute dans la liste des shaders (image-liste vide de la bonne hauteur)
    HIMAGELIST il = ImageList_Create(1, S(30), ILC_COLOR32, 1, 1);
    HIMAGELIST old = ListView_SetImageList(g.list, il, LVSIL_SMALL);
    if (old) ImageList_Destroy(old);
    // police mono du journal
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    cf.yHeight = 180;  // 9 pt
    cf.crTextColor = cref(MUTED);
    wcscpy_s(cf.szFaceName, L"Cascadia Mono");
    SendMessageW(g.log, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
}


// ---------------------------------------------------------------------------
//  Peinture de la fenetre : en-tete, cartes, zone de depot
// ---------------------------------------------------------------------------
void paintMain(HWND hw) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hw, &ps);
    RECT rc;
    GetClientRect(hw, &rc);
    ui::Frame f(rc.right, rc.bottom, ui::rgb(BG));
    const int m = S(16), pad = S(16);

    // En-tete
    f.icon(IC_LOGO, ui::rectf((float)m, F(14), (float)m + F(38), F(14) + F(38)), false, ui::rgb(0));
    f.text(APP_NAME_WSTR, ui::rectf(m + F(50), F(10), m + F(300), F(36)), F(20), true, ui::rgb(TEXT));
    f.text(TX("ui.tagline"), ui::rectf(m + F(50), F(35), m + F(520), F(56)), F(12), false, ui::rgb(MUTED));
    {
        std::wstring ver = L"v" APP_VER_WSTR;
        float tw = ui::measureText(ver, F(12), false) + F(20);
        float x1 = (float)g.chipRight, x0 = x1 - tw;
        f.fillRound(ui::rectf(x0, F(20), x1, F(44)), F(12), ui::rgb(FIELD));
        f.strokeRound(ui::rectf(x0, F(20), x1, F(44)), F(12), ui::rgb(FIELD_BORDER), 1.0f);
        f.text(ver, ui::rectf(x0, F(20), x1, F(44)), F(12), false, ui::rgb(MUTED), ui::Center);
    }

    // Cartes
    for (const auto& c : g.cards) {
        D2D1_RECT_F r = ui::rectf(c.r);
        f.fillRound(r, F(14), ui::rgb(CARD));
        f.strokeRound(r, F(14), ui::rgb(CARD_BORDER), 1.0f);
        if (!c.title.empty()) {
            f.icon(c.icon, ui::rectf(r.left + pad, r.top + F(15), r.left + pad + F(18), r.top + F(33)), true, ui::rgb(MUTED));
            f.text(c.title, ui::rectf(r.left + pad + F(28), r.top + F(8), r.right - F(120), r.top + F(40)), F(14), true, ui::rgb(TEXT));
        }
    }
    // Nombre de fichiers dans l'en-tete de la carte des shaders
    for (const auto& c : g.cards) {
        if (c.icon == IC_FILM && !g.paths.empty()) {
            std::wstring n = TXF(g.paths.size() == 1 ? "ui.files_one" : "ui.files_many", (int)g.paths.size());
            f.text(n, ui::rectf((float)c.r.right - F(160), (float)c.r.top + F(8), (float)c.r.right - pad, (float)c.r.top + F(40)), F(12), false,
                   ui::rgb(MUTED), ui::Right);
        }
    }
    // Illumination pendant un glisser-deposer (fondu)
    const float da = g.dropAmt;
    if (da > 0.01f) {
        for (const auto& c : g.cards) {
            if (c.icon != IC_FILM) continue;
            D2D1_RECT_F r = ui::rectf(c.r);
            f.fillRound(r, F(14), ui::rgb(ACCENT, 0.08f * da));
            f.strokeRound(r, F(14), ui::rgb(ACCENT, 0.9f * da), 2.0f);
        }
    }
    // Etat vide : zone de depot illustree
    if (g.paths.empty()) {
        RECT d = g.dropRect;
        float cx = (d.left + d.right) / 2.0f, cy = (d.top + d.bottom) / 2.0f;
        D2D1_RECT_F dr = ui::rectf((float)d.left + F(4), (float)d.top + F(4), (float)d.right - F(4), (float)d.bottom - F(4));
        f.strokeRound(dr, F(12), ui::rgb(FIELD_BORDER), 1.0f);
        if (da > 0.01f) f.strokeRound(dr, F(12), ui::rgb(ACCENT, 0.95f * da), 1.5f);
        f.icon(IC_DROP, ui::rectf(cx - F(36), cy - F(62) - F(4) * da, cx + F(36), cy + F(10) - F(4) * da), true,
               da > 0.01f ? ui::rgb(ACCENT, 0.7f + 0.3f * da) : ui::rgb(MUTED, 0.7f));
        f.text(TX(da > 0.5f ? "ui.drop.release" : "ui.drop.title"),
               ui::rectf(d.left + F(10), cy + F(14), d.right - F(10), cy + F(38)), F(15), true, ui::rgb(TEXT), ui::Center);
        f.text(TX("ui.drop.hint"), ui::rectf(d.left + F(10), cy + F(38), d.right - F(10), cy + F(58)), F(12), false,
               ui::rgb(MUTED), ui::Center);
    }
    f.present(hdc);
    EndPaint(hw, &ps);
}

void drawPreview(const DRAWITEMSTRUCT* d) {
    RECT r = d->rcItem;
    int cw = r.right - r.left, ch = r.bottom - r.top;
    {
        ui::Frame f(cw, ch, ui::rgb(CARD));
        f.fillRound(ui::rectf(0, 0, (float)cw, (float)ch), F(10), ui::rgb(0x090A0D));
        if (!(g.prevBmp && g.prevW > 0 && g.prevH > 0)) {
            f.icon(IC_EYE, ui::rectf(cw / 2.0f - F(20), ch / 2.0f - F(34), cw / 2.0f + F(20), ch / 2.0f + F(6)), true, ui::rgb(FAINT));
            f.text(TX("ui.preview.none"), ui::rectf(0, ch / 2.0f + F(8), (float)cw, ch / 2.0f + F(28)), F(13), true, ui::rgb(MUTED), ui::Center);
            f.text(TX("ui.preview.hint"), ui::rectf(F(8), ch / 2.0f + F(28), cw - F(8), ch / 2.0f + F(48)), F(11),
                   false, ui::rgb(FAINT), ui::Center);
        }
        f.present(d->hDC, r.left, r.top);
    }
    if (g.prevBmp && g.prevW > 0 && g.prevH > 0) {
        int in = S(4), aw = cw - 2 * in, ah = ch - 2 * in;
        double sc = std::min((double)aw / g.prevW, (double)ah / g.prevH);
        int dw = std::max(1, (int)(g.prevW * sc)), dh = std::max(1, (int)(g.prevH * sc));
        HDC mem = CreateCompatibleDC(d->hDC);
        HGDIOBJ old = SelectObject(mem, g.prevBmp);
        SetStretchBltMode(d->hDC, HALFTONE);
        SetBrushOrgEx(d->hDC, 0, 0, nullptr);
        StretchBlt(d->hDC, r.left + (cw - dw) / 2, r.top + (ch - dh) / 2, dw, dh, mem, 0, 0, g.prevW, g.prevH, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    }
}

COLORREF statusColor(int st) {
    switch (st) {
        case ST_RUNNING: return cref(ACCENT);
        case ST_DONE: return cref(OK);
        case ST_FAILED: return cref(ERR);
        case ST_CANCELLED: return cref(WARN);
        default: return cref(MUTED);
    }
}

// Textes dépendant de la langue : clés enregistrées à la création, appliquées par applyLanguage()
const char* const kLabelKeys[20] = {
    "ui.lbl.resolution", "ui.lbl.times",    "ui.lbl.fps",      "ui.lbl.duration", "ui.lbl.start",
    "ui.lbl.codec",      "ui.lbl.preset",   "ui.lbl.quality",  "ui.lbl.container", "ui.lbl.smoothing",
    "ui.lbl.output",     "ui.lbl.bitrate",  "ui.lbl.color",    "ui.lbl.blur",     "ui.lbl.loop",
    "ui.lbl.audio",      "ui.lbl.fade_in",  "ui.lbl.fade_out", "ui.lbl.encoder", "ui.lbl.depth"};
std::vector<std::pair<HWND, const char*>> g_btnKeys;  // boutons : texte
std::vector<std::pair<HWND, const char*>> g_tips;     // info-bulles : texte

void addTip(HWND ctl, const char* key) {
    g_tips.push_back({ctl, key});
    TTTOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g.wnd;
    ti.uId = (UINT_PTR)ctl;
    ti.lpszText = LPSTR_TEXTCALLBACKW;  // le texte est demandé à l'affichage (TTN_GETDISPINFO) : suit la langue
    SendMessageW(g.tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

std::wstring langLabel() {
    std::wstring s = W(i18n::language());
    for (auto& c : s) c = (wchar_t)towupper(c);
    return s;
}

void fillCombo(HWND c, const std::vector<std::wstring>& items) {
    int sel = (int)SendMessageW(c, CB_GETCURSEL, 0, 0);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (const auto& t : items) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t.c_str());
    if (sel >= 0) SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
}

void fillCombos() {
    std::vector<std::wstring> res;
    for (const auto& r : kRes) res.push_back(resName(r));
    res.push_back(TX("ui.res.custom"));
    fillCombo(g.cmbRes, res);
    fillCombo(g.cmbSS, {TX("ui.opt.none"), L"x2", L"x3", L"x4"});
    fillCombo(g.cmbColor, {TX("ui.color.bt709tv"), TX("ui.color.bt709pc"), TX("ui.color.srgb"), TX("ui.color.bt601")});
    fillCombo(g.cmbBlur, {TX("ui.opt.none"), L"x4", L"x8", L"x16"});
    fillCombo(g.cmbEncoder, {TX("ui.enc.cpu"), TX("ui.enc.auto"), L"NVIDIA NVENC", L"AMD AMF", L"Intel QuickSync"});
    fillCombo(g.cmbDepth, {L"8", L"10"});
    // Codecs et conteneurs : ceux que ce ffmpeg ne sait pas produire sont signales dans la liste
    const std::wstring ff = findFfmpeg(g.exeDir);
    const std::wstring na = L"  (" + TX("ui.opt.unavailable") + L")";
    std::vector<std::wstring> codecs, containers;
    for (int i = 0; i < codecCount(); i++)
        codecs.push_back(codecAt(i).label + (codecAvailable(ff, codecAt(i).id) ? L"" : na));
    for (int i = 0; i < containerCount(); i++)
        containers.push_back(containerAt(i).label + (containerAvailable(ff, containerAt(i).id) ? L"" : na));
    fillCombo(g.cmbCodec, codecs);
    fillCombo(g.cmbContainer, containers);
    SendMessageW(g.cmbCodec, CB_SETDROPPEDWIDTH, S(210), 0);
    SendMessageW(g.cmbContainer, CB_SETDROPPEDWIDTH, S(170), 0);
}

HWND mk(HWND parent, const wchar_t* cls, const wchar_t* txt, DWORD style, int id, DWORD ex = 0) {
    return CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
}

void createControls(HWND hw) {
    using ui::BtnStyle;
    auto button = [&](const char* key, int id, int icon, BtnStyle st, unsigned bg) {
        HWND b = mk(hw, L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW, id);
        ui::setupButton(b, icon, st, bg);
        if (key) g_btnKeys.push_back({b, key});
        return b;
    };
    g.bAbout = button(nullptr, ID_ABOUT, IC_INFO, BtnStyle::Ghost, BG);
    g.bLang = button(nullptr, ID_LANG, IC_GLOBE, BtnStyle::Ghost, BG);
    g.bParams = button(nullptr, ID_PARAMS, IC_SLIDERS, BtnStyle::Ghost, BG);
    g.bAdd = button("ui.btn.add", ID_ADD, IC_ADD, BtnStyle::Secondary, BG);
    g.bRemove = button("ui.btn.remove", ID_REMOVE, IC_TRASH, BtnStyle::Secondary, BG);
    g.bClear = button("ui.btn.clear", ID_CLEAR, IC_CLEAR, BtnStyle::Secondary, BG);
    g.bFolder = button("ui.btn.folder", ID_FOLDER, IC_FOLDER, BtnStyle::Secondary, BG);
    g.bSnap = button("ui.btn.png", ID_SNAP, IC_IMAGE, BtnStyle::Secondary, BG);
    g.bStart = button("ui.btn.start", ID_START, IC_PLAY, BtnStyle::Primary, BG);
    g.bPause = button(nullptr, ID_PAUSE, IC_PAUSE, BtnStyle::Secondary, BG);  // texte : Pause / Reprendre
    g.bCancel = button("ui.btn.cancel", ID_CANCEL, IC_STOP, BtnStyle::Danger, BG);

    // Liste des shaders (sans en-tête, colonnes : fichier / statut)
    g.list = mk(hw, L"SysListView32", L"", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER, ID_LIST);
    SendMessageW(g.list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageW(g.list, LVM_SETBKCOLOR, 0, cref(CARD));
    SendMessageW(g.list, LVM_SETTEXTBKCOLOR, 0, cref(CARD));
    SendMessageW(g.list, LVM_SETTEXTCOLOR, 0, cref(TEXT));
    ui::darkTheme(g.list, L"DarkMode_Explorer");
    LVCOLUMNW col{};
    col.mask = LVCF_WIDTH;
    col.cx = 400;
    SendMessageW(g.list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
    col.cx = 130;
    SendMessageW(g.list, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);

    auto label = [&]() {
        HWND l = mk(hw, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
        g.labels.push_back(l);
        return l;
    };
    auto edit = [&](int id, DWORD extra = 0) {
        HWND e = mk(hw, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | extra, id, WS_EX_CLIENTEDGE);
        ui::darkTheme(e, L"DarkMode_CFD");
        SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
        return e;
    };
    auto combo = [&](int id, DWORD style = CBS_DROPDOWNLIST) {
        HWND c = mk(hw, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | style, id);
        ui::darkTheme(c, L"DarkMode_CFD");
        return c;
    };

    label();  // 0 résolution
    g.cmbRes = combo(ID_RES);
    g.edW = edit(ID_W, ES_NUMBER);
    label();  // 1 ×
    g.edH = edit(ID_H, ES_NUMBER);
    label();  // 2 FPS
    g.cmbFps = combo(ID_FPS, CBS_DROPDOWN | CBS_AUTOHSCROLL);
    for (const wchar_t* f : {L"24", L"30", L"60", L"120"}) SendMessageW(g.cmbFps, CB_ADDSTRING, 0, (LPARAM)f);
    label();  // 3 durée
    g.edDur = edit(ID_DUR);
    label();  // 4 début
    g.edStart = edit(ID_START_T);
    label();  // 5 codec
    g.cmbCodec = combo(ID_CODEC);
    label();  // 6 preset
    g.cmbPreset = combo(ID_PRESET);
    for (auto p : kPresets) SendMessageW(g.cmbPreset, CB_ADDSTRING, 0, (LPARAM)p);
    label();  // 7 qualité
    g.trkCrf = mk(hw, L"STSlider", L"", WS_TABSTOP, ID_CRF);
    SendMessageW(g.trkCrf, TBM_SETRANGE, TRUE, MAKELPARAM(0, 35));
    g.lblCrf = mk(hw, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    label();  // 8 conteneur
    g.cmbContainer = combo(ID_CONTAINER);
    label();  // 9 lissage
    g.cmbSS = combo(ID_SS);
    label();  // 10 sortie
    g.edOut = edit(ID_OUT);
    g.bBrowse = button("ui.btn.browse", ID_BROWSE, IC_FOLDER, BtnStyle::Secondary, CARD);
    g.bLoadP = button("ui.btn.profile", ID_LOADP, IC_DOWNLOAD, BtnStyle::Secondary, CARD);
    g.bSaveP = button("ui.btn.save", ID_SAVEP, IC_SAVE, BtnStyle::Secondary, CARD);
    label();  // 11 débit
    g.edBitrate = edit(ID_BITRATE, ES_NUMBER);
    label();  // 12 couleur
    g.cmbColor = combo(ID_COLOR);
    label();  // 13 flou
    g.cmbBlur = combo(ID_BLUR);
    label();  // 14 boucle
    g.edLoop = edit(ID_LOOP);
    label();  // 15 audio
    g.edAudio = edit(ID_AUDIO);
    g.bAudio = button("ui.btn.browse", ID_AUDIOB, IC_AUDIO, BtnStyle::Secondary, CARD);
    label();  // 16 fondu entrée
    g.edFadeIn = edit(ID_FADEIN);
    label();  // 17 fondu sortie
    g.edFadeOut = edit(ID_FADEOUT);
    label();  // 18 encodeur
    g.cmbEncoder = combo(ID_ENCODER);
    label();  // 19 profondeur (bits)
    g.cmbDepth = combo(ID_DEPTH);
    g.lblEst = mk(hw, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    g.lblForced = mk(hw, L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0);

    static const struct { const char* key; int id; int icon; } chips[5] = {
        {"ui.chip.draft", ID_PRE0, IC_ZAP}, {"ui.chip.youtube", ID_PRE1, IC_YOUTUBE},
        {"ui.chip.4k60", ID_PRE2, IC_MONITOR}, {"ui.chip.vertical", ID_PRE3, IC_PHONE},
        {"ui.chip.more", ID_PRE4, IC_SLIDERS}};
    for (int i = 0; i < 5; i++) g.chips[i] = button(chips[i].key, chips[i].id, chips[i].icon, BtnStyle::Chip, CARD);

    g.preview = mk(hw, L"STATIC", L"", SS_OWNERDRAW, ID_PREVIEW);
    g.trkPrev = mk(hw, L"STSlider", L"", WS_TABSTOP, ID_PREVTRK);
    SendMessageW(g.trkPrev, TBM_SETRANGE, TRUE, MAKELPARAM(0, kPrevSteps));
    g.lblPrev = mk(hw, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    g.bPrev = button("ui.btn.preview", ID_PREVBTN, IC_EYE, BtnStyle::Secondary, CARD);
    g.bPlay = button(nullptr, ID_PLAY, IC_PLAY, BtnStyle::Secondary, CARD);  // texte : Lecture / Arret (updatePlayButton)

    g.prog = mk(hw, L"STProgress", L"", 0, ID_PROG);
    SendMessageW(g.prog, PBM_SETRANGE32, 0, 1000);
    g.status = mk(hw, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX, ID_STATUS);
    LoadLibraryW(L"Msftedit.dll");
    g.log = mk(hw, L"RICHEDIT50W", L"", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL, ID_LOG);
    SendMessageW(g.log, EM_SETBKGNDCOLOR, 0, cref(0x0F1116));
    SendMessageW(g.log, EM_LIMITTEXT, 1 << 24, 0);
    SendMessageW(g.log, EM_SETEVENTMASK, 0, ENM_MOUSEEVENTS);  // double-clic sur « fichier:ligne: » : ouvrir a la ligne
    SendMessageW(g.log, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
    ui::darkTheme(g.log, L"DarkMode_Explorer");

    // Noms accessibles : champs, listes et curseurs annonces par leur libelle (lecteurs d'ecran)
    {
        const struct { HWND c; const char* key; } names[] = {
            {g.cmbRes, "ui.lbl.resolution"}, {g.edW, "ui.tip.width"}, {g.edH, "ui.tip.height"}, {g.cmbFps, "ui.lbl.fps"},
            {g.cmbSS, "ui.lbl.smoothing"}, {g.edDur, "ui.lbl.duration"}, {g.edStart, "ui.lbl.start"}, {g.cmbCodec, "ui.lbl.codec"},
            {g.cmbPreset, "ui.lbl.preset"}, {g.trkCrf, "ui.lbl.quality"}, {g.cmbContainer, "ui.lbl.container"},
            {g.cmbEncoder, "ui.lbl.encoder"}, {g.edBitrate, "ui.lbl.bitrate"}, {g.cmbColor, "ui.lbl.color"}, {g.cmbBlur, "ui.lbl.blur"},
            {g.edLoop, "ui.lbl.loop"}, {g.cmbDepth, "ui.lbl.depth"}, {g.edAudio, "ui.lbl.audio"}, {g.edFadeIn, "ui.lbl.fade_in"},
            {g.edFadeOut, "ui.tip.fade_out"}, {g.edOut, "ui.lbl.output"}, {g.trkPrev, "ui.tip.preview_time"},
            {g.list, "ui.card.shaders"}, {g.preview, "ui.card.preview"}, {g.prog, "ui.card.progress"}};
        for (const auto& n : names) setAccessibleName(n.c, n.key);
    }

    // Info-bulles (texte demandé à l'affichage, voir TTN_GETDISPINFO)
    g.tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hw,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g.tip, TTM_SETMAXTIPWIDTH, 0, S(320));
    SendMessageW(g.tip, TTM_SETDELAYTIME, TTDT_INITIAL, 450);
    struct Tip { HWND c; const char* key; };
    const Tip tips[] = {
        {g.bAbout, "ui.tip.about"}, {g.bLang, "ui.tip.language"}, {g.bParams, "ui.tip.params_btn"},
        {g.bAdd, "ui.tip.add"}, {g.bRemove, "ui.tip.remove"}, {g.bClear, "ui.tip.clear"}, {g.bFolder, "ui.tip.folder"},
        {g.bSnap, "ui.tip.png"}, {g.bStart, "ui.tip.start"}, {g.bPause, "ui.tip.pause"}, {g.bCancel, "ui.tip.cancel"},
        {g.cmbRes, "ui.tip.resolution"}, {g.edW, "ui.tip.width"}, {g.edH, "ui.tip.height"}, {g.cmbFps, "ui.tip.fps"},
        {g.cmbSS, "ui.tip.smoothing"}, {g.edDur, "ui.tip.duration"}, {g.edStart, "ui.tip.start_time"},
        {g.cmbCodec, "ui.tip.codec"}, {g.cmbPreset, "ui.tip.preset"}, {g.trkCrf, "ui.tip.quality"},
        {g.cmbContainer, "ui.tip.container"}, {g.cmbEncoder, "ui.tip.encoder"}, {g.edBitrate, "ui.tip.bitrate"},
        {g.cmbColor, "ui.tip.color"}, {g.cmbBlur, "ui.tip.blur"}, {g.edLoop, "ui.tip.loop"}, {g.edAudio, "ui.tip.audio"},
        {g.bAudio, "ui.tip.audio_browse"}, {g.edFadeIn, "ui.tip.fade_in"}, {g.edFadeOut, "ui.tip.fade_out"},
        {g.edOut, "ui.tip.output"}, {g.bBrowse, "ui.tip.output_browse"}, {g.bLoadP, "ui.tip.profile_load"},
        {g.bSaveP, "ui.tip.profile_save"}, {g.chips[0], "ui.tip.chip_draft"}, {g.chips[1], "ui.tip.chip_youtube"},
        {g.chips[2], "ui.tip.chip_4k60"}, {g.chips[3], "ui.tip.chip_vertical"}, {g.chips[4], "ui.tip.chip_more"},
        {g.cmbDepth, "ui.tip.depth"}, {g.lblEst, "ui.tip.estimate"}, {g.lblForced, "ui.tip.forced"}, {g.trkPrev, "ui.tip.preview_time"},
        {g.bPrev, "ui.tip.preview"}, {g.bPlay, "ui.tip.play"},
    };
    for (const auto& t : tips) addTip(t.c, t.key);
}

// Signale dans le journal toute clé de traduction demandée mais introuvable (fichier de langue incomplet)
void reportMissingKeys() {
    for (const auto& k : i18n::missingKeys()) appendLog(TXF("log.missing_key", W(k).c_str()));
}

// Applique la langue courante à tous les textes de la fenêtre (à la création et à chaque changement de langue)
void applyLanguage() {
    for (const auto& b : g_btnKeys) SetWindowTextW(b.first, TX(b.second).c_str());
    SetWindowTextW(g.bPause, TX(g.pause ? "ui.btn.resume" : "ui.btn.pause").c_str());
    SetWindowTextW(g.bLang, langLabel().c_str());
    for (size_t i = 0; i < g.labels.size() && i < 20; i++) SetWindowTextW(g.labels[i], TX(kLabelKeys[i]).c_str());
    for (HWND e : {g.edOut, g.edBitrate, g.edLoop, g.edAudio}) {
        const char* key = e == g.edOut ? "ui.cue.output" : e == g.edBitrate ? "ui.cue.bitrate" : e == g.edLoop ? "ui.cue.loop" : "ui.cue.audio";
        SendMessageW(e, EM_SETCUEBANNER, TRUE, (LPARAM)TX(key).c_str());
    }
    fillCombos();
    for (size_t i = 0; i < g.paths.size() && i < g.state.size(); i++)
        if (g.state[i] != ST_RUNNING) setItemText((int)i, 1, statusText(g.state[i]));
    updateCrfLabel();
    updatePrevLabel();
    updatePlayButton();
    if (!g.running && !(g.player && g.player->running())) setStatus(TX("ui.status.idle"));
    layout();
    InvalidateRect(g.preview, nullptr, FALSE);
    InvalidateRect(g.wnd, nullptr, FALSE);
    reportMissingKeys();
}

}  // namespace

// ---------------------------------------------------------------------------
//  Position et taille de la fenetre (window.ini a cote du .exe)
// ---------------------------------------------------------------------------
void restoreWindowPlacement(HWND wnd, int showCmd) {
    std::string text;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    int v[5] = {0, 0, 0, 0, 0};
    bool ok = false;
    if (readFile(g.exeDir + L"window.ini", text)) {
        ok = sscanf(text.c_str(), "x=%d y=%d w=%d h=%d max=%d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5;
    }
    if (ok && v[2] > 200 && v[3] > 200) {
        RECT r{v[0], v[1], v[0] + v[2], v[1] + v[3]};
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {  // jamais hors de tous les ecrans
            wp.rcNormalPosition = r;
            wp.showCmd = v[4] ? SW_SHOWMAXIMIZED : showCmd;
            SetWindowPlacement(wnd, &wp);
            return;
        }
    }
    ShowWindow(wnd, showCmd);
}

std::string readSavedLanguage() {
    std::string text;
    if (!readFile(g.exeDir + L"window.ini", text)) return "";
    size_t p = text.find("lang=");
    if (p == std::string::npos) return "";
    size_t e = text.find_first_of("\r\n", p);
    std::string code = trim(text.substr(p + 5, e == std::string::npos ? std::string::npos : e - p - 5));
    return code.size() <= 8 ? code : "";
}

void saveWindowPlacement(HWND wnd) {
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(wnd, &wp)) return;
    RECT r = wp.rcNormalPosition;
    FILE* f = _wfopen((g.exeDir + L"window.ini").c_str(), L"wb");
    if (!f) return;
    fprintf(f, "x=%d y=%d w=%d h=%d max=%d\nlang=%s\n", (int)r.left, (int)r.top, (int)(r.right - r.left),
            (int)(r.bottom - r.top), wp.showCmd == SW_SHOWMAXIMIZED ? 1 : 0, i18n::language().c_str());
    fclose(f);
}

// Illumination de la zone de depot : fondu de 120 ms vers l'etat voulu
void setDropHot(bool hot) {
    g.dropHot = hot;
    SetTimer(g.wnd, 9, 15, nullptr);
}

LRESULT CALLBACK wndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.wnd = hw;
            g.dpi = theme::dpiFor(hw);
            g.brCard = CreateSolidBrush(cref(CARD));
            g.brField = CreateSolidBrush(cref(FIELD));
            ui::darkTitleBar(hw);
            createControls(hw);
            createFonts();
            applyLanguage();
            settingsToUI(g.base);
            updatePrevLabel();
            setStatus(TX("ui.status.idle"));
            updateButtons();
            taskbar::init(hw);
            registerDropTarget(hw);
            return 0;
        }
        case WM_PAINT: paintMain(hw); return 0;
        case WM_TIMER:
            if (wp == 9) {  // fondu de l'illumination du depot
                static ULONGLONG last = 0;
                ULONGLONG now = GetTickCount64();
                float step = last ? (float)(now - last) / 120.0f : 0.2f;
                last = now;
                float target = g.dropHot ? 1.0f : 0.0f;
                if (g.dropAmt < target) g.dropAmt = std::min(target, g.dropAmt + step);
                else g.dropAmt = std::max(target, g.dropAmt - step);
                InvalidateRect(hw, &g.bandRect, FALSE);
                if (g.dropAmt == target) {
                    KillTimer(hw, 9);
                    last = 0;
                }
                return 0;
            }
            break;
        case WM_ERASEBKGND: return 1;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, IsWindowEnabled((HWND)lp) ? cref(TEXT) : cref(FAINT));
            SetBkColor(dc, cref(FIELD));
            return (LRESULT)g.brField;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            HWND c = (HWND)lp;
            SetBkColor(dc, cref(CARD));
            SetTextColor(dc, (c == g.status || c == g.lblCrf || c == g.lblPrev) ? cref(TEXT) : c == g.lblForced ? cref(ACCENT) : cref(MUTED));
            return (LRESULT)g.brCard;
        }
        case WM_DPICHANGED: {
            g.dpi = theme::dpiOverride ? theme::dpiOverride : HIWORD(wp);
            RECT* r = (RECT*)lp;
            SetWindowPos(hw, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            createFonts();
            layout();
            return 0;
        }
        case WM_NOTIFY: {
            NMHDR* nh = (NMHDR*)lp;
            if (nh->hwndFrom == g.tip && nh->code == TTN_GETDISPINFOW) {  // texte de l'info-bulle, dans la langue courante
                NMTTDISPINFOW* di = (NMTTDISPINFOW*)lp;
                HWND ctl = (HWND)di->hdr.idFrom;
                for (const auto& t : g_tips)
                    if (t.first == ctl) {
                        static std::wstring tipText;
                        tipText = TX(t.second);
                        di->lpszText = const_cast<LPWSTR>(tipText.c_str());
                        break;
                    }
                return 0;
            }
            if (nh->hwndFrom == g.log && nh->code == EN_MSGFILTER) {
                const MSGFILTER* mf = (const MSGFILTER*)lp;
                if (mf->msg == WM_LBUTTONDBLCLK) {
                    POINTL pt{(LONG)(short)LOWORD(mf->lParam), (LONG)(short)HIWORD(mf->lParam)};
                    const LRESULT ch = SendMessageW(g.log, EM_CHARFROMPOS, 0, (LPARAM)&pt);
                    const LRESULT ln = SendMessageW(g.log, EM_EXLINEFROMCHAR, 0, ch);
                    wchar_t buf[2048];
                    *(WORD*)buf = 2040;
                    const LRESULT n = SendMessageW(g.log, EM_GETLINE, (WPARAM)ln, (LPARAM)buf);
                    std::wstring path;
                    int line = 0;
                    if (n > 0 && parseLocation(std::wstring(buf, (size_t)n), path, line)) openAtLine(path, line);
                }
                return 0;
            }
            if (nh->hwndFrom == g.list) {
                if (nh->code == NM_DBLCLK) {  // double-clic : lire la video terminee
                    int i = ((NMITEMACTIVATE*)lp)->iItem;
                    if (i >= 0 && i < (int)g.paths.size() && g.state[i] == ST_DONE) {
                        std::wstring p = outputFile(g.paths[i], g.base, false);
                        if (fileExists(p)) ShellExecuteW(hw, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                } else if (nh->code == LVN_ITEMCHANGED) {
                    updateForced();
                    followSelection();  // autre shader selectionne : la lecture suit
                } else if (nh->code == LVN_KEYDOWN) {
                    if (((NMLVKEYDOWN*)lp)->wVKey == VK_DELETE) removeSelected();
                } else if (nh->code == NM_CUSTOMDRAW) {  // statut colore
                    NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
                    switch (cd->nmcd.dwDrawStage) {
                        case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
                        case CDDS_ITEMPREPAINT: return CDRF_NOTIFYSUBITEMDRAW;
                        case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
                            int i = (int)cd->nmcd.dwItemSpec;
                            cd->clrText = cd->iSubItem == 1 && i >= 0 && i < (int)g.state.size() ? statusColor(g.state[i]) : cref(TEXT);
                            return CDRF_NEWFONT;
                        }
                    }
                }
            }
            return 0;
        }
        case WM_HSCROLL:
            if ((HWND)lp == g.trkCrf) { updateCrfLabel(); updateEstimate(); }
            if ((HWND)lp == g.trkPrev) {
                updatePrevLabel();
                if (g.player && g.player->running()) {  // lecture en cours : le curseur deplace la position
                    Settings cur = g.base;
                    std::wstring e;
                    if (uiToSettings(cur, e)) g.player->seek(cur.start + SendMessageW(g.trkPrev, TBM_GETPOS, 0, 0) / (double)kPrevSteps * cur.duration);
                } else if (LOWORD(wp) == TB_ENDTRACK) {
                    runPreview();  // curseur relache -> nouvel apercu
                }
            }
            return 0;
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
            if (d->CtlType == ODT_BUTTON) {
                ui::drawButton(d, g.dpi);
                return TRUE;
            }
            if (d->CtlID == ID_PREVIEW) {
                drawPreview(d);
                return TRUE;
            }
            break;
        }
        case WM_APP_DL: {
            std::wstring* m = (std::wstring*)lp;
            SendMessageW(g.prog, PBM_SETPOS, (WPARAM)(wp * 10), 0);
            taskbar::normal();
            taskbar::progress((long long)wp, 100);
            setStatus(*m);
            delete m;
            return 0;
        }
        case WM_APP_DLDONE: {
            std::wstring* e = (std::wstring*)lp;
            if (g.worker.joinable()) g.worker.join();
            g.running = false;
            if (wp) {
                forgetFfmpegCapabilities();
                fillCombos();
                setStatus(TX("status.ffmpeg_installed"));
                appendLog(TX("log.ffmpeg_installed"));
                taskbar::clear();
            } else {
                setStatus(TX("status.ffmpeg_failed"));
                appendLog(TXF("log.error", e->c_str()));
                taskbar::error();
                MessageBoxW(hw, TXF("dialog.ffmpeg_failed", e->c_str()).c_str(), L"ffmpeg", MB_ICONWARNING);
                taskbar::clear();
            }
            delete e;
            updateButtons();
            return 0;
        }
        case WM_APP_PREVIEW: {
            PreviewData* p = (PreviewData*)lp;
            if (p->t >= 0) {  // image du lecteur : la position suit le temps du shader
                if (g.previewPending > 0) g.previewPending--;
                if (g.player && g.player->running()) {
                    Settings cur = g.base;
                    if (cur.duration > 0) {
                        const int pos = (int)std::lround((p->t - cur.start) / cur.duration * kPrevSteps);
                        SendMessageW(g.trkPrev, TBM_SETPOS, TRUE, std::min(kPrevSteps, std::max(0, pos)));
                        updatePrevLabel();
                    }
                }
            }
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = p->w;
            bi.bmiHeader.biHeight = -p->h;  // haut en bas
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP b = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (b && bits) {
                memcpy(bits, p->bgra.data(), p->bgra.size());
                if (g.prevBmp) DeleteObject(g.prevBmp);
                g.prevBmp = b;
                g.prevW = p->w;
                g.prevH = p->h;
                InvalidateRect(g.preview, nullptr, FALSE);
            }
            delete p;
            return 0;
        }
        case WM_SIZE:
            if (g.list && wp != SIZE_MINIMIZED) layout();
            return 0;
        case WM_GETMINMAXINFO: {
            RECT r{0, 0, S(800), S(790)};
            AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES, g.dpi);
            RECT wa;
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
            ((MINMAXINFO*)lp)->ptMinTrackSize = {r.right - r.left, std::min((int)(r.bottom - r.top), (int)(wa.bottom - wa.top))};
            return 0;
        }
        case WM_DROPFILES: {
            HDROP d = (HDROP)wp;
            UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> files;
            for (UINT i = 0; i < n; i++) {
                wchar_t p[MAX_PATH * 2];
                if (DragQueryFileW(d, i, p, MAX_PATH * 2)) files.push_back(p);
            }
            DragFinish(d);
            addFiles(files);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == CBN_EDITCHANGE) updateEstimate();
            if (id == ID_RES && HIWORD(wp) == CBN_SELCHANGE) {
                int i = (int)SendMessageW(g.cmbRes, CB_GETCURSEL, 0, 0);
                if (i >= 0 && i < kResCount) {
                    SetWindowTextW(g.edW, std::to_wstring(kRes[i].w).c_str());
                    SetWindowTextW(g.edH, std::to_wstring(kRes[i].h).c_str());
                }
                return 0;
            }
            if ((id == ID_W || id == ID_H) && HIWORD(wp) == EN_CHANGE) {
                syncResCombo();
                return 0;
            }
            if (id >= ID_PRE0 && id <= ID_PRE3) {
                if (!g.running) applyQuickPreset(id - ID_PRE0);
                return 0;
            }
            if (id == ID_PLAY) {
                if (g.player && g.player->running()) {
                    stopPlayer();
                    setStatus(TX("ui.status.idle"));
                } else {
                    startPlayer();
                }
                return 0;
            }
            if (id == ID_PRE4) {
                if (!g.running) showPresetMenu();
                return 0;
            }
            switch (id) {
                case ID_ADD: browseFiles(); break;
                case ID_REMOVE: removeSelected(); break;
                case ID_CLEAR: clearAll(); break;
                case ID_START: startJobs(); break;
                case ID_SNAP: startJobs(true); break;
                case ID_BROWSE: browseOutDir(); break;
                case ID_LOADP: loadProfile(); break;
                case ID_SAVEP: saveProfile(); break;
                case ID_CANCEL:
                    if (!g.running) break;
                    g.cancel = true;
                    g.pause = false;
                    EnableWindow(g.bCancel, FALSE);
                    EnableWindow(g.bPause, FALSE);
                    setStatus(TX("status.cancelling"));
                    break;
                case ID_PAUSE:
                    if (!g.running || g.previewRun) break;
                    g.pause = !g.pause;
                    SetWindowTextW(g.bPause, TX(g.pause ? "ui.btn.resume" : "ui.btn.pause").c_str());
                    ui::setButtonIcon(g.bPause, g.pause ? IC_PLAY : IC_PAUSE);
                    SendMessageW(g.prog, ui::STPB_SETSTATE, g.pause ? 1 : 0, 0);
                    if (g.pause) taskbar::paused(); else taskbar::normal();
                    setStatus(TX(g.pause ? "status.paused" : "status.rendering"));
                    break;
                case ID_ABOUT: showAbout(hw); break;
                case ID_PARAMS: showParams(hw); break;
                case ID_LANG: {  // langue suivante (en -> fr -> ...), appliquée tout de suite
                    std::vector<std::string> langs = i18n::available();
                    std::string cur = i18n::language(), next = langs.front();
                    for (size_t i = 0; i < langs.size(); i++)
                        if (langs[i] == cur) next = langs[(i + 1) % langs.size()];
                    if (i18n::setLanguage(next)) applyLanguage();
                    break;
                }
                case ID_FOLDER: openFolder(); break;
                case ID_AUDIOB: browseAudio(); break;
                case ID_PREVBTN: runPreview(); break;
            }
            return 0;
        }
        case WM_APP_LOG: {
            std::wstring* s = (std::wstring*)lp;
            appendLog(*s);
            delete s;
            return 0;
        }
        case WM_APP_PROGRESS: {
            ProgressInfo* p = (ProgressInfo*)lp;
            int idx = (int)wp;
            int pct = (int)(p->n * 100 / std::max(1LL, p->total));
            SendMessageW(g.prog, PBM_SETPOS, (WPARAM)(p->n * 1000 / std::max(1LL, p->total)), 0);
            if (!g.pause) taskbar::normal();
            taskbar::progress(p->n, p->total);
            if (idx >= 0 && idx < (int)g.paths.size()) setItemText(idx, 1, TXF("status.running_pct", pct));
            setStatus(TXF("status.progress", p->n, p->total, pct, i18n::fixed(p->speed, 1).c_str(),
                           fmtTime(p->remaining).c_str()));
            delete p;
            return 0;
        }
        case WM_APP_FILE: {
            int idx = (int)wp;
            if (idx >= 0 && idx < (int)g.paths.size()) {
                if (g.snapshotRun && lp == ST_DONE) {  // l'image ne compte pas comme rendu video termine
                    ShellExecuteW(hw, L"open", g.snapshotPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    lp = ST_PENDING;
                }
                g.state[idx] = (int)lp;
                setItemText(idx, 1, statusText((int)lp));
                saveQueueNow();
                if (lp == ST_RUNNING) SendMessageW(g.prog, PBM_SETPOS, 0, 0);
                if (lp == ST_DONE) SendMessageW(g.prog, PBM_SETPOS, 1000, 0);
                SendMessageW(g.prog, ui::STPB_SETSTATE, lp == ST_FAILED ? 2 : 0, 0);
                InvalidateRect(g.list, nullptr, FALSE);
            }
            return 0;
        }
        case WM_APP_DONE: {
            if (g.worker.joinable()) g.worker.join();
            g.running = false;
            SendMessageW(g.prog, ui::STPB_SETSTATE, 0, 0);
            if (g.previewRun) {
                g.previewRun = false;
                setStatus(TX("status.preview_ready"));
                updateButtons();
                return 0;
            }
            int fails = 0, cancelled = 0;
            for (int st : g.state) {
                if (st == ST_FAILED) fails++;
                if (st == ST_CANCELLED) cancelled++;
            }
            if (cancelled) setStatus(TX("status.cancelled_done"));
            else if (fails) setStatus(TXF("status.finished_errors", fails));
            else setStatus(TX(g.snapshotRun ? "status.image_created" : "status.all_done"));
            if (fails) SendMessageW(g.prog, ui::STPB_SETSTATE, 2, 0);
            fails ? taskbar::error() : taskbar::clear();
            taskbar::flash(hw);
            if (!fails && !cancelled && !g.snapshotRun && !g.paths.empty())  // action de fin facultative (config.ini : endaction)
                performEndAction(g.base.endaction, outputFile(g.paths.back(), g.base, false), [](const std::wstring& m) { appendLog(m); }, false);
            g.snapshotRun = false;
            appendLog(L"");
            updateButtons();
            return 0;
        }
        case WM_CLOSE:
            stopPlayer();
            if (g.running) {
                g.cancel = true;
                g.pause = false;
                if (g.worker.joinable()) g.worker.join();
                g.running = false;
            }
            {
                Settings cur = g.base;
                std::wstring e;
                if (uiToSettings(cur, e)) {
                    g.base = cur;
                    saveSettings();
                }
            }
            saveWindowPlacement(hw);
            DestroyWindow(hw);
            return 0;
        case WM_DESTROY:
            revokeDropTarget(hw);
            taskbar::shutdown();
            if (g.font) DeleteObject(g.font);
            if (g.fontBold) DeleteObject(g.fontBold);
            if (g.brCard) DeleteObject(g.brCard);
            if (g.brField) DeleteObject(g.brField);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}
