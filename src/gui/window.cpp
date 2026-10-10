#include "app.h"
#include "core/ffmpeg_download.h"
#include "core/util.h"
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
    g.cards.push_back({sc, L"Réglages vidéo", IC_SLIDERS});
    {  // preselections rapides, alignees a droite du titre
        static const int cw[4] = {112, 100, 76, 96};
        int cx = sc.right - pad;
        for (int i = 3; i >= 0; i--) {
            cx -= S(cw[i]);
            MoveWindow(g.chips[i], cx, sc.top + S(10), S(cw[i]), S(28), TRUE);
            cx -= S(6);
        }
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
    put(g.labels[9], 56, 2);
    put(g.cmbSS, 80, 1);
    nextRow();
    put(g.labels[3], 80, 2);
    put(g.edDur, 72);
    put(g.labels[4], 52, 2);
    put(g.edStart, 72);
    put(g.labels[5], 48, 2);
    put(g.cmbCodec, 84, 1);
    put(g.labels[6], 52, 2);
    put(g.cmbPreset, 104, 1);
    nextRow();
    put(g.labels[7], 80, 2);
    put(g.trkCrf, 170, 3);
    put(g.lblCrf, 124, 2);
    put(g.labels[8], 76, 2);
    put(g.cmbContainer, 72, 1);
    put(g.labels[18], 62, 2);
    put(g.cmbEncoder, 150, 1);
    nextRow();
    put(g.labels[11], 80, 2);
    put(g.edBitrate, 72);
    put(g.labels[12], 58, 2);
    put(g.cmbColor, 192, 1);
    put(g.labels[13], 34, 2);
    put(g.cmbBlur, 80, 1);
    put(g.labels[14], 66, 2);
    put(g.edLoop, 64);
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
    g.cards.push_back({fr, L"Shaders", IC_FILM});
    g.cards.push_back({pr, L"Aperçu", IC_EYE});
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
    MoveWindow(g.lblPrev, px0, imgBottom + S(40), std::max(40, pw - S(110)), S(20), TRUE);
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
    g.cards.push_back({lc, L"Journal", IC_TERMINAL});
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
    for (HWND c : {g.cmbRes, g.cmbFps, g.cmbCodec, g.cmbPreset, g.cmbContainer, g.cmbSS, g.cmbColor, g.cmbBlur, g.cmbEncoder})
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

void addTip(HWND ctl, const wchar_t* text) {
    TTTOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g.wnd;
    ti.uId = (UINT_PTR)ctl;
    ti.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(g.tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
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
    f.text(L"ShaderToMP4", ui::rectf(m + F(50), F(10), m + F(300), F(36)), F(20), true, ui::rgb(TEXT));
    f.text(L"Shadertoy vers vidéo, image par image", ui::rectf(m + F(50), F(35), m + F(420), F(56)), F(12), false, ui::rgb(MUTED));
    {
        std::wstring ver = L"v" APP_VER_WSTR;
        float tw = ui::measureText(ver, F(12), false) + F(20);
        float x1 = (float)rc.right - m, x0 = x1 - tw;
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
        if (c.title == L"Shaders" && !g.paths.empty()) {
            std::wstring n = wfmt(L"%d fichier%ls", (int)g.paths.size(), g.paths.size() > 1 ? L"s" : L"");
            f.text(n, ui::rectf((float)c.r.right - F(160), (float)c.r.top + F(8), (float)c.r.right - pad, (float)c.r.top + F(40)), F(12), false,
                   ui::rgb(MUTED), ui::Right);
        }
    }
    // Illumination pendant un glisser-deposer (fondu)
    const float da = g.dropAmt;
    if (da > 0.01f) {
        for (const auto& c : g.cards) {
            if (c.title != L"Shaders") continue;
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
        f.text(da > 0.5f ? L"Relâchez pour ajouter" : L"Déposez vos shaders ici",
               ui::rectf(d.left + F(10), cy + F(14), d.right - F(10), cy + F(38)), F(15), true, ui::rgb(TEXT), ui::Center);
        f.text(L"ou cliquez sur « Ajouter »  ·  .glsl, .txt", ui::rectf(d.left + F(10), cy + F(38), d.right - F(10), cy + F(58)), F(12), false,
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
            f.text(L"Aucun aperçu", ui::rectf(0, ch / 2.0f + F(8), (float)cw, ch / 2.0f + F(28)), F(13), true, ui::rgb(MUTED), ui::Center);
            f.text(L"Choisissez un instant puis cliquez sur « Aperçu »", ui::rectf(F(8), ch / 2.0f + F(28), cw - F(8), ch / 2.0f + F(48)), F(11),
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

// Cree un controle fils et le range dans g (voir WM_CREATE)
HWND mk(HWND parent, const wchar_t* cls, const wchar_t* txt, DWORD style, int id, DWORD ex = 0) {
    return CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
}

void createControls(HWND hw) {
    using ui::BtnStyle;
    auto button = [&](const wchar_t* txt, int id, int icon, BtnStyle st, unsigned bg) {
        HWND b = mk(hw, L"BUTTON", txt, WS_TABSTOP | BS_OWNERDRAW, id);
        ui::setupButton(b, icon, st, bg);
        return b;
    };
    g.bAdd = button(L"Ajouter", ID_ADD, IC_ADD, BtnStyle::Secondary, BG);
    g.bRemove = button(L"Retirer", ID_REMOVE, IC_TRASH, BtnStyle::Secondary, BG);
    g.bClear = button(L"Vider", ID_CLEAR, IC_CLEAR, BtnStyle::Secondary, BG);
    g.bFolder = button(L"Dossier", ID_FOLDER, IC_FOLDER, BtnStyle::Secondary, BG);
    g.bSnap = button(L"Image PNG", ID_SNAP, IC_IMAGE, BtnStyle::Secondary, BG);
    g.bStart = button(L"Démarrer", ID_START, IC_PLAY, BtnStyle::Primary, BG);
    g.bPause = button(L"Pause", ID_PAUSE, IC_PAUSE, BtnStyle::Secondary, BG);
    g.bCancel = button(L"Annuler", ID_CANCEL, IC_STOP, BtnStyle::Danger, BG);

    // Liste des shaders (sans en-tete, colonnes : fichier / statut)
    g.list = mk(hw, L"SysListView32", L"", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER, ID_LIST);
    SendMessageW(g.list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageW(g.list, LVM_SETBKCOLOR, 0, cref(CARD));
    SendMessageW(g.list, LVM_SETTEXTBKCOLOR, 0, cref(CARD));
    SendMessageW(g.list, LVM_SETTEXTCOLOR, 0, cref(TEXT));
    ui::darkTheme(g.list, L"DarkMode_Explorer");
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 400;
    col.pszText = const_cast<LPWSTR>(L"Fichier shader");
    SendMessageW(g.list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
    col.cx = 130;
    col.pszText = const_cast<LPWSTR>(L"Statut");
    SendMessageW(g.list, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);

    auto label = [&](const wchar_t* t) {
        HWND l = mk(hw, L"STATIC", t, SS_LEFT | SS_NOPREFIX, 0);
        g.labels.push_back(l);
        return l;
    };
    auto addItems = [&](HWND c, std::initializer_list<const wchar_t*> items) {
        for (auto t : items) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t);
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

    label(L"Résolution");  // 0
    g.cmbRes = combo(ID_RES);
    for (const auto& r : kRes) SendMessageW(g.cmbRes, CB_ADDSTRING, 0, (LPARAM)r.name);
    SendMessageW(g.cmbRes, CB_ADDSTRING, 0, (LPARAM)L"Personnalisé");
    g.edW = edit(ID_W, ES_NUMBER);
    label(L"×");  // 1
    g.edH = edit(ID_H, ES_NUMBER);
    label(L"FPS");  // 2
    g.cmbFps = combo(ID_FPS, CBS_DROPDOWN | CBS_AUTOHSCROLL);
    addItems(g.cmbFps, {L"24", L"30", L"60", L"120"});
    label(L"Durée (s)");  // 3
    g.edDur = edit(ID_DUR);
    label(L"Début (s)");  // 4
    g.edStart = edit(ID_START_T);
    label(L"Codec");  // 5
    g.cmbCodec = combo(ID_CODEC);
    addItems(g.cmbCodec, {L"H.264", L"H.265"});
    label(L"Preset");  // 6
    g.cmbPreset = combo(ID_PRESET);
    for (auto p : kPresets) SendMessageW(g.cmbPreset, CB_ADDSTRING, 0, (LPARAM)p);
    label(L"Qualité");  // 7
    g.trkCrf = mk(hw, L"STSlider", L"", WS_TABSTOP, ID_CRF);
    SendMessageW(g.trkCrf, TBM_SETRANGE, TRUE, MAKELPARAM(0, 35));
    g.lblCrf = mk(hw, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    label(L"Conteneur");  // 8
    g.cmbContainer = combo(ID_CONTAINER);
    addItems(g.cmbContainer, {L"MP4", L"MOV", L"MKV"});
    label(L"Lissage");  // 9
    g.cmbSS = combo(ID_SS);
    addItems(g.cmbSS, {L"Aucun", L"x2", L"x3", L"x4"});
    label(L"Sortie");  // 10
    g.edOut = edit(ID_OUT);
    SendMessageW(g.edOut, EM_SETCUEBANNER, TRUE, (LPARAM)L"vide = à côté du fichier shader");
    g.bBrowse = button(L"Parcourir", ID_BROWSE, IC_FOLDER, BtnStyle::Secondary, CARD);
    g.bLoadP = button(L"Profil", ID_LOADP, IC_DOWNLOAD, BtnStyle::Secondary, CARD);
    g.bSaveP = button(L"Enregistrer", ID_SAVEP, IC_SAVE, BtnStyle::Secondary, CARD);
    label(L"Débit (kb/s)");  // 11
    g.edBitrate = edit(ID_BITRATE, ES_NUMBER);
    SendMessageW(g.edBitrate, EM_SETCUEBANNER, TRUE, (LPARAM)L"0 = CRF");
    label(L"Couleur");  // 12
    g.cmbColor = combo(ID_COLOR);
    addItems(g.cmbColor, {L"BT.709 TV (standard)", L"BT.709 PC (pleine plage)", L"sRGB (TV)", L"BT.601 TV (ancien)"});
    label(L"Flou");  // 13
    g.cmbBlur = combo(ID_BLUR);
    addItems(g.cmbBlur, {L"Aucun", L"x4", L"x8", L"x16"});
    label(L"Boucle (s)");  // 14
    g.edLoop = edit(ID_LOOP);
    SendMessageW(g.edLoop, EM_SETCUEBANNER, TRUE, (LPARAM)L"0 = libre");
    label(L"Audio");  // 15
    g.edAudio = edit(ID_AUDIO);
    SendMessageW(g.edAudio, EM_SETCUEBANNER, TRUE, (LPARAM)L"fichier son (mp3, wav...) - vide = aucun");
    g.bAudio = button(L"Parcourir", ID_AUDIOB, IC_AUDIO, BtnStyle::Secondary, CARD);
    label(L"Fondu entrée");  // 16
    g.edFadeIn = edit(ID_FADEIN);
    label(L"sortie");  // 17
    g.edFadeOut = edit(ID_FADEOUT);
    label(L"Encodeur");  // 18
    g.cmbEncoder = combo(ID_ENCODER);
    addItems(g.cmbEncoder, {L"CPU (x264/x265)", L"Auto (matériel si dispo)", L"NVIDIA NVENC", L"AMD AMF", L"Intel QuickSync"});

    static const struct { const wchar_t* t; int id; int icon; } chips[4] = {
        {L"Brouillon", ID_PRE0, IC_ZAP}, {L"YouTube", ID_PRE1, IC_YOUTUBE}, {L"4K 60", ID_PRE2, IC_MONITOR}, {L"Vertical", ID_PRE3, IC_PHONE}};
    for (int i = 0; i < 4; i++) g.chips[i] = button(chips[i].t, chips[i].id, chips[i].icon, BtnStyle::Chip, CARD);

    g.preview = mk(hw, L"STATIC", L"", SS_OWNERDRAW, ID_PREVIEW);
    g.trkPrev = mk(hw, L"STSlider", L"", WS_TABSTOP, ID_PREVTRK);
    SendMessageW(g.trkPrev, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    g.lblPrev = mk(hw, L"STATIC", L"Aperçu : t = début", SS_LEFT | SS_NOPREFIX, 0);
    g.bPrev = button(L"Aperçu", ID_PREVBTN, IC_EYE, BtnStyle::Secondary, CARD);

    g.prog = mk(hw, L"STProgress", L"", 0, ID_PROG);
    SendMessageW(g.prog, PBM_SETRANGE32, 0, 1000);
    g.status = mk(hw, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX, ID_STATUS);
    LoadLibraryW(L"Msftedit.dll");
    g.log = mk(hw, L"RICHEDIT50W", L"", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL, ID_LOG);
    SendMessageW(g.log, EM_SETBKGNDCOLOR, 0, cref(0x0F1116));
    SendMessageW(g.log, EM_LIMITTEXT, 1 << 24, 0);
    SendMessageW(g.log, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
    ui::darkTheme(g.log, L"DarkMode_Explorer");

    // Info-bulles
    g.tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hw,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g.tip, TTM_SETMAXTIPWIDTH, 0, S(320));
    SendMessageW(g.tip, TTM_SETDELAYTIME, TTDT_INITIAL, 450);
    struct Tip { HWND c; const wchar_t* t; };
    const Tip tips[] = {
        {g.bAdd, L"Ajouter des shaders (.glsl, .txt)  -  Ctrl+O"},
        {g.bRemove, L"Retirer les shaders sélectionnés  -  Suppr"},
        {g.bClear, L"Vider la liste"},
        {g.bFolder, L"Ouvrir le dossier de la vidéo du shader sélectionné"},
        {g.bSnap, L"Rendre une seule image PNG au temps « Début » et l'ouvrir"},
        {g.bStart, L"Rendre tous les shaders de la liste  -  F5"},
        {g.bPause, L"Suspendre / reprendre le rendu  -  F6"},
        {g.bCancel, L"Annuler le rendu en cours  -  Échap"},
        {g.cmbRes, L"Taille de la vidéo en pixels. Choisis un préréglage ou saisis une taille libre"},
        {g.edW, L"Largeur en pixels (16 à 16384)"},
        {g.edH, L"Hauteur en pixels (16 à 16384)"},
        {g.cmbFps, L"Images par seconde (1 à 240) : tu peux saisir une valeur libre"},
        {g.cmbSS, L"Lissage : rendu à N fois la taille puis réduction. Bords plus propres, rendu plus lent"},
        {g.edDur, L"Durée en secondes ou mm:ss (ex. 90 ou 1:30)"},
        {g.edStart, L"Instant de départ dans le shader (iTime), en secondes ou mm:ss"},
        {g.cmbCodec, L"H.264 : compatible partout. H.265 : fichiers plus petits"},
        {g.cmbPreset, L"Vitesse d'encodage : plus lent = fichier plus petit à qualité égale"},
        {g.trkCrf, L"Qualité constante : 0 = sans perte, 12 = quasi parfait, 18 = très bon, 23 = correct"},
        {g.cmbContainer, L"Format du fichier : MP4 (universel), MOV ou MKV"},
        {g.cmbEncoder, L"CPU : meilleure qualité. NVENC / AMF / QuickSync : beaucoup plus rapide (repli sur CPU si indisponible)"},
        {g.edBitrate, L"Débit fixe en kb/s à la place de la qualité constante (0 = utiliser le CRF)"},
        {g.cmbColor, L"Espace colorimétrique et plage des couleurs de la vidéo"},
        {g.cmbBlur, L"Flou de mouvement : moyenne de plusieurs sous-images par image"},
        {g.edLoop, L"Période du shader en secondes : la durée devient un multiple entier pour une boucle parfaite (0 = libre)"},
        {g.edAudio, L"Fichier son à mixer, calé sur le temps « Début »"},
        {g.bAudio, L"Choisir un fichier son"},
        {g.edFadeIn, L"Fondu d'entrée en secondes (image et son)"},
        {g.edFadeOut, L"Fondu de sortie en secondes (image et son)"},
        {g.edOut, L"Dossier de sortie (vide = à côté du fichier shader)"},
        {g.bBrowse, L"Choisir le dossier de sortie"},
        {g.bLoadP, L"Charger un profil de réglages (.ini)"},
        {g.bSaveP, L"Enregistrer les réglages dans un profil (.ini)"},
        {g.chips[0], L"720p, 30 fps, qualité moyenne, encodage rapide : pour tester"},
        {g.chips[1], L"1080p, 60 fps, H.264, très bonne qualité : prêt pour YouTube"},
        {g.chips[2], L"4K, 60 fps, H.265 : haute définition"},
        {g.chips[3], L"1080×1920, 30 fps : vidéo verticale 9:16"},
        {g.trkPrev, L"Instant de l'aperçu, de « Début » à « Début + Durée »"},
        {g.bPrev, L"Afficher une image du shader à cet instant  -  Ctrl+P"},
    };
    for (const auto& t : tips) addTip(t.c, t.t);
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

void saveWindowPlacement(HWND wnd) {
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(wnd, &wp)) return;
    RECT r = wp.rcNormalPosition;
    FILE* f = _wfopen((g.exeDir + L"window.ini").c_str(), L"wb");
    if (!f) return;
    fprintf(f, "x=%d y=%d w=%d h=%d max=%d\n", (int)r.left, (int)r.top, (int)(r.right - r.left), (int)(r.bottom - r.top),
            wp.showCmd == SW_SHOWMAXIMIZED ? 1 : 0);
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
            g.dpi = GetDpiForWindow(hw);
            g.brCard = CreateSolidBrush(cref(CARD));
            g.brField = CreateSolidBrush(cref(FIELD));
            ui::darkTitleBar(hw);
            createControls(hw);
            createFonts();
            settingsToUI(g.base);
            updatePrevLabel();
            setStatus(L"Glisse des fichiers shader dans la fenêtre, ou clique sur « Ajouter ».");
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
            SetTextColor(dc, (c == g.status || c == g.lblCrf || c == g.lblPrev) ? cref(TEXT) : cref(MUTED));
            return (LRESULT)g.brCard;
        }
        case WM_DPICHANGED: {
            g.dpi = HIWORD(wp);
            RECT* r = (RECT*)lp;
            SetWindowPos(hw, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            createFonts();
            layout();
            return 0;
        }
        case WM_NOTIFY: {
            NMHDR* nh = (NMHDR*)lp;
            if (nh->hwndFrom == g.list) {
                if (nh->code == NM_DBLCLK) {  // double-clic : lire la video terminee
                    int i = ((NMITEMACTIVATE*)lp)->iItem;
                    if (i >= 0 && i < (int)g.paths.size() && g.state[i] == ST_DONE) {
                        std::wstring p = outputPath(g.paths[i], g.base.outdir, L"." + W(g.base.container));
                        if (fileExists(p)) ShellExecuteW(hw, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
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
            if ((HWND)lp == g.trkCrf) updateCrfLabel();
            if ((HWND)lp == g.trkPrev) {
                updatePrevLabel();
                if (LOWORD(wp) == TB_ENDTRACK) runPreview();  // curseur relache -> nouvel apercu
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
                setStatus(L"ffmpeg.exe installé : tu peux lancer les rendus.");
                appendLog(L"ffmpeg.exe installé.");
                taskbar::clear();
            } else {
                setStatus(L"Téléchargement de ffmpeg échoué.");
                appendLog(L"ERREUR : " + *e);
                taskbar::error();
                MessageBoxW(hw, (L"Le téléchargement de ffmpeg a échoué :\n\n" + *e +
                                 L"\n\nTu peux aussi placer ffmpeg.exe à la main à côté de ShaderToMP4.exe.").c_str(),
                            L"ffmpeg", MB_ICONWARNING);
                taskbar::clear();
            }
            delete e;
            updateButtons();
            return 0;
        }
        case WM_APP_PREVIEW: {
            PreviewData* p = (PreviewData*)lp;
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
                    setStatus(L"Annulation en cours...");
                    break;
                case ID_PAUSE:
                    if (!g.running || g.previewRun) break;
                    g.pause = !g.pause;
                    SetWindowTextW(g.bPause, g.pause ? L"Reprendre" : L"Pause");
                    ui::setButtonIcon(g.bPause, g.pause ? IC_PLAY : IC_PAUSE);
                    SendMessageW(g.prog, ui::STPB_SETSTATE, g.pause ? 1 : 0, 0);
                    if (g.pause) taskbar::paused(); else taskbar::normal();
                    setStatus(g.pause ? L"En pause." : L"Rendu en cours...");
                    break;
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
            if (idx >= 0 && idx < (int)g.paths.size()) setItemText(idx, 1, wfmt(L"En cours %d %%", pct));
            setStatus(wfmt(L"Image %lld / %lld  ·  %d %%  ·  %.1f img/s  ·  reste %ls", p->n, p->total, pct, p->speed,
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
                setStatus(L"Aperçu prêt.");
                updateButtons();
                return 0;
            }
            int fails = 0, cancelled = 0;
            for (int st : g.state) {
                if (st == ST_FAILED) fails++;
                if (st == ST_CANCELLED) cancelled++;
            }
            if (cancelled) setStatus(L"Annulé.");
            else if (fails) setStatus(wfmt(L"Terminé avec %d erreur(s).", fails));
            else setStatus(g.snapshotRun ? L"Image créée." : L"Tout est terminé.");
            if (fails) SendMessageW(g.prog, ui::STPB_SETSTATE, 2, 0);
            fails ? taskbar::error() : taskbar::clear();
            taskbar::flash(hw);
            g.snapshotRun = false;
            appendLog(L"");
            updateButtons();
            return 0;
        }
        case WM_CLOSE:
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
