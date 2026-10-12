// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Fenêtre « Paramètres et post-traitement » : effets de post-traitement et LUT couleur (réglages communs à tous les shaders), puis un
// curseur, une case ou un sélecteur de couleur par paramètre déclaré (// uniform ...) dans le shader sélectionné. Les valeurs des paramètres
// sont enregistrées dans params/ (dossier de l'application) et reprises tout de suite par l'aperçu en lecture.
#include "app.h"
#include "core/i18n.h"
#include "core/params.h"
#include "core/postfx.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"

using namespace theme;

namespace {

enum { ID_PD_CLOSE = IDCANCEL, ID_PD_RESET = 801, ID_PD_LUT, ID_PD_BROWSE, ID_PD_SLIDER = 1000 };
constexpr UINT_PTR kRestartTimer = 1;  // l'aperçu en lecture est relancé 400 ms après le dernier changement de post-traitement

struct Row {
    enum Kind { Header, Effect, Lut, Param, Note } kind = Header;
    int idx = 0;                // effet ou parametre
    int y = 0;                  // haut de la ligne, en dp du contenu
    std::wstring text;          // titre, libelle ou note
    std::vector<HWND> sliders;  // un par composante (aucun pour une case ou une couleur)
};

struct Dlg {
    HWND wnd = nullptr, view = nullptr, bClose = nullptr, bReset = nullptr, edLut = nullptr, bBrowse = nullptr;
    UINT dpi = 96;
    bool done = false;
    std::vector<Row> rows;
    int contentH = 0;  // dp
    int scrollY = 0;   // pixels
    bool fxOn[8] = {};
    double fxAmt[8] = {};
    std::vector<ParamInfo> infos;
    std::wstring file, error, shaderName;
    bool hasShader = false;
    bool ready = false;  // la fenêtre est construite : les changements de champ viennent de l'utilisateur
    int nextId = ID_PD_SLIDER;
    HBRUSH brField = nullptr;
} D;

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }
float F(int v) { return (float)v * D.dpi / 96.0f; }

const wchar_t* kDlgClass = L"ShaderToMP4Params";
const wchar_t* kViewClass = L"ShaderToMP4ParamsView";

int toPos(double v, double lo, double hi) { return (int)std::lround(std::min(1.0, std::max(0.0, (v - lo) / (hi - lo))) * 1000.0); }
double fromPos(int p, double lo, double hi) { return lo + (hi - lo) * p / 1000.0; }

std::wstring fmt(double v) { return wfmt(L"%.3g", v); }

// Valeur, bornes et composantes d'un curseur
struct SliderRef {
    double lo = 0, hi = 1;
    int comps = 1;
};

HWND makeSlider() {
    HWND s = CreateWindowExW(0, L"STSlider", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, D.view, (HMENU)(INT_PTR)D.nextId++,
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(s, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
    return s;
}

void clearRows() {
    for (Row& r : D.rows)
        for (HWND s : r.sliders) DestroyWindow(s);
    D.rows.clear();
}

std::wstring postString() {  // « bloom:0.35,grain:0.06 » d'après les cases cochées
    const auto& fx = postEffectInfos();
    std::string out;
    for (size_t i = 0; i < fx.size() && i < 8; i++)
        if (D.fxOn[i]) out += (out.empty() ? "" : ",") + std::string(fx[i].name) + ":" + std::to_string(D.fxAmt[i]).substr(0, 8);
    return W(out);
}

void scheduleRestart() { SetTimer(D.wnd, kRestartTimer, 400, nullptr); }

void postChanged() {  // effets ou LUT modifies : reglages enregistres, apercu en lecture relance
    g.base.post = U8(postString());
    saveSettings();
    scheduleRestart();
}

void saveParam(const ParamInfo& p) { saveParamValue(D.file, p.name, formatParamValue(p, p.value)); }

// --- Construction des lignes -----------------------------------------------------------------------------------------------
int viewWidthDp() {
    RECT rc;
    GetClientRect(D.view, &rc);
    return MulDiv(rc.right, 96, (int)D.dpi);
}

void positionChildren() {
    const int vw = viewWidthDp();
    const int sx = 206, sw = std::max(60, vw - sx - 20 - 90);
    for (Row& r : D.rows) {
        const int n = (int)r.sliders.size();
        // un parametre anime ou lie au son : sa note (« animé par images clés ») prend la place de la valeur, le curseur est raccourci
        const bool locked = r.kind == Row::Param && (D.infos[(size_t)r.idx].animated || !D.infos[(size_t)r.idx].audio.empty());
        const int rowW = locked ? std::max(60, vw - sx - 20 - 190) : sw;
        for (int i = 0; i < n; i++) {
            const int w = (rowW - (n - 1) * 6) / n;
            MoveWindow(r.sliders[(size_t)i], S(sx + i * (w + 6)), S(r.y + 4) - D.scrollY, S(w), S(28), TRUE);
        }
        if (r.kind == Row::Lut) {
            MoveWindow(D.edLut, S(sx), S(r.y + 5) - D.scrollY, S(std::max(60, vw - sx - 20 - 104)), S(26), TRUE);
            MoveWindow(D.bBrowse, S(vw - 20 - 96), S(r.y + 3) - D.scrollY, S(96), S(30), TRUE);
        }
    }
}

void updateScrollBar() {
    RECT rc;
    GetClientRect(D.view, &rc);
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = S(D.contentH);
    si.nPage = (UINT)rc.bottom;
    si.nPos = D.scrollY;
    SetScrollInfo(D.view, SB_VERT, &si, TRUE);
}

void buildRows() {
    clearRows();
    D.scrollY = 0;
    auto add = [&](Row::Kind k, int idx, int y, const std::wstring& text) {
        Row r;
        r.kind = k;
        r.idx = idx;
        r.y = y;
        r.text = text;
        D.rows.push_back(r);
        return &D.rows.back();
    };
    int y = 12;
    add(Row::Header, 0, y, TX("ui.lbl.post"));
    y += 42;
    const auto& fx = postEffectInfos();
    for (size_t i = 0; i < fx.size() && i < 8; i++) {
        Row* r = add(Row::Effect, (int)i, y, TX(fx[i].labelKey));
        r->sliders.push_back(makeSlider());
        SendMessageW(r->sliders[0], TBM_SETPOS, TRUE, toPos(D.fxAmt[i], fx[i].lo, fx[i].hi));
        EnableWindow(r->sliders[0], D.fxOn[i]);
        y += 36;
    }
    add(Row::Lut, 0, y, TX("ui.lbl.lut"));
    y += 46;
    if (!D.hasShader) {
        add(Row::Header, 0, y, TX("ui.lbl.params"));
        y += 42;
        add(Row::Note, 0, y, TX("ui.params.no_shader"));
        y += 56;
    } else {
        add(Row::Header, 0, y, TX("ui.lbl.params") + L" - " + D.shaderName);
        y += 42;
        if (!D.error.empty()) {
            add(Row::Note, 0, y, D.error);
            y += 92;
        } else if (D.infos.empty()) {
            add(Row::Note, 0, y, TX("ui.params.none"));
            y += 56;
        }
        for (size_t i = 0; i < D.infos.size(); i++) {
            const ParamInfo& p = D.infos[i];
            Row* r = add(Row::Param, (int)i, y, W(p.name));
            const bool locked = p.animated || !p.audio.empty();
            int comps = 0;
            if (p.type == "float" || p.type == "int") comps = 1;
            else if (p.type == "vec2") comps = 2;
            else if (p.type == "vec3") comps = 3;
            else if (p.type == "vec4") comps = 4;
            double lo = 0, hi = 1;
            paramRange(p, lo, hi);
            for (int c = 0; c < comps; c++) {
                HWND s = makeSlider();
                SendMessageW(s, TBM_SETPOS, TRUE, toPos(p.value[c], lo, hi));
                EnableWindow(s, !locked);
                r->sliders.push_back(s);
            }
            y += 36;
        }
    }
    D.contentH = y + 12;
    D.rows.shrink_to_fit();
    // un seul niveau de barre de defilement : on la montre seulement si le contenu depasse
    RECT rc;
    GetClientRect(D.view, &rc);
    ShowScrollBar(D.view, SB_VERT, S(D.contentH) > rc.bottom);
    updateScrollBar();
    positionChildren();
    InvalidateRect(D.view, nullptr, FALSE);
}

void reloadShader() {
    D.infos.clear();
    D.error.clear();
    D.shaderName.clear();
    D.hasShader = false;
    int sel = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (sel < 0) sel = 0;
    if (sel >= (int)g.paths.size()) return;
    const std::wstring& path = g.paths[(size_t)sel];
    D.hasShader = true;
    D.shaderName = path.substr(path.find_last_of(L"\\/") == std::wstring::npos ? 0 : path.find_last_of(L"\\/") + 1);
    D.file = paramsFile(g.exeDir, path);
    std::wstring err;
    if (!loadShaderParams(g.exeDir, path, D.infos, err)) D.error = err;
}

// --- Peinture ----------------------------------------------------------------------------------------------------------------
RECT checkRect(const Row& r, int xdp) {
    return {S(xdp), S(r.y + 7) - D.scrollY, S(xdp + 22), S(r.y + 29) - D.scrollY};
}

void paintView() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(D.view, &ps);
    RECT rc;
    GetClientRect(D.view, &rc);
    ui::Frame f(rc.right, rc.bottom, ui::rgb(CARD));
    const float w = (float)rc.right;
    auto box = [&](const RECT& r, bool on) {
        D2D1_RECT_F q = ui::rectf(r);
        if (on) f.fillRoundGradient(q, F(6), ui::rgb(ACCENT_A), ui::rgb(ACCENT_B));
        else f.fillRound(q, F(6), ui::rgb(FIELD));
        f.strokeRound(q, F(6), ui::rgb(on ? ACCENT : FIELD_BORDER), 1.0f);
        if (on) f.icon(IC_CHECK, ui::rectf((float)r.left + F(3), (float)r.top + F(3), (float)r.right - F(3), (float)r.bottom - F(3)), true, ui::rgb(0xFFFFFF));
    };
    const float valX = w - F(20 + 84);
    for (const Row& r : D.rows) {
        const int yy = S(r.y) - D.scrollY;
        if (yy > rc.bottom || yy + S(100) < 0) continue;
        switch (r.kind) {
            case Row::Header:
                f.text(r.text, ui::rectf(F(20), (float)yy, w - F(20), (float)yy + F(30)), F(15), true, ui::rgb(ACCENT));
                f.fillRound(ui::rectf(F(20), (float)yy + F(31), w - F(20), (float)yy + F(32)), 0.5f, ui::rgb(CARD_BORDER));
                break;
            case Row::Effect: {
                box(checkRect(r, 20), D.fxOn[r.idx]);
                f.text(r.text, ui::rectf(F(54), (float)yy, F(200), (float)yy + F(36)), F(13), false, ui::rgb(D.fxOn[r.idx] ? TEXT : MUTED));
                f.text(fmt(D.fxAmt[r.idx]), ui::rectf(valX, (float)yy, w - F(20), (float)yy + F(36)), F(13), false, ui::rgb(D.fxOn[r.idx] ? TEXT : FAINT), ui::Right);
                break;
            }
            case Row::Lut:
                f.text(r.text, ui::rectf(F(20), (float)yy, F(200), (float)yy + F(36)), F(13), false, ui::rgb(MUTED));
                break;
            case Row::Note:
                f.text(r.text, ui::rectf(F(20), (float)yy, w - F(20), (float)yy + F(88)), F(13), false, ui::rgb(r.text == D.error ? ERR : MUTED), ui::Left, false, true);
                break;
            case Row::Param: {
                const ParamInfo& p = D.infos[(size_t)r.idx];
                const bool locked = p.animated || !p.audio.empty();
                f.text(r.text, ui::rectf(F(20), (float)yy, F(200), (float)yy + F(36)), F(13), false, ui::rgb(locked ? FAINT : TEXT));
                if (p.type == "bool") {
                    box(checkRect(r, 206), p.value[0] >= 0.5);
                } else if (p.type == "color") {
                    const auto c = [&](int i) { return (unsigned)std::lround(std::min(1.0, std::max(0.0, p.value[i])) * 255); };
                    const D2D1_RECT_F sw = ui::rectf(F(206), (float)yy + F(6), F(270), (float)yy + F(30));
                    f.fillRound(sw, F(6), ui::rgb((c(0) << 16) | (c(1) << 8) | c(2)));
                    f.strokeRound(sw, F(6), ui::rgb(FIELD_BORDER), 1.0f);
                    f.text(W(formatParamValue(p, p.value)), ui::rectf(F(280), (float)yy, w - F(20), (float)yy + F(36)), F(13), false, ui::rgb(MUTED));
                }
                if (locked) {
                    f.text(TX(p.animated ? "ui.params.animated" : "ui.params.audio"), ui::rectf(w - F(20 + 180), (float)yy, w - F(20), (float)yy + F(36)), F(12), false,
                           ui::rgb(FAINT), ui::Right);
                } else if (p.type == "float") {
                    f.text(fmt(p.value[0]), ui::rectf(valX, (float)yy, w - F(20), (float)yy + F(36)), F(13), false, ui::rgb(MUTED), ui::Right);
                } else if (p.type == "int") {
                    f.text(std::to_wstring((long long)std::llround(p.value[0])), ui::rectf(valX, (float)yy, w - F(20), (float)yy + F(36)), F(13), false, ui::rgb(MUTED), ui::Right);
                }
                break;
            }
        }
    }
    f.present(hdc);
    EndPaint(D.view, &ps);
}

// --- Actions -----------------------------------------------------------------------------------------------------------------
void toggleBool(Row& r) {
    if (r.kind == Row::Effect) {
        D.fxOn[r.idx] = !D.fxOn[r.idx];
        EnableWindow(r.sliders[0], D.fxOn[r.idx]);
        postChanged();
    } else {
        ParamInfo& p = D.infos[(size_t)r.idx];
        p.value[0] = p.value[0] >= 0.5 ? 0.0 : 1.0;
        saveParam(p);
    }
    InvalidateRect(D.view, nullptr, FALSE);
}

void pickColor(ParamInfo& p) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = D.wnd;
    cc.rgbResult = RGB((int)std::lround(p.value[0] * 255), (int)std::lround(p.value[1] * 255), (int)std::lround(p.value[2] * 255));
    cc.lpCustColors = custom;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorW(&cc)) return;
    p.value[0] = GetRValue(cc.rgbResult) / 255.0;
    p.value[1] = GetGValue(cc.rgbResult) / 255.0;
    p.value[2] = GetBValue(cc.rgbResult) / 255.0;
    saveParam(p);
    InvalidateRect(D.view, nullptr, FALSE);
}

void onSlider(HWND s) {
    for (Row& r : D.rows)
        for (size_t c = 0; c < r.sliders.size(); c++) {
            if (r.sliders[c] != s) continue;
            const int pos = (int)SendMessageW(s, TBM_GETPOS, 0, 0);
            if (r.kind == Row::Effect) {
                const auto& fx = postEffectInfos()[(size_t)r.idx];
                D.fxAmt[r.idx] = fromPos(pos, fx.lo, fx.hi);
                postChanged();
            } else {
                ParamInfo& p = D.infos[(size_t)r.idx];
                double lo = 0, hi = 1;
                paramRange(p, lo, hi);
                double v = fromPos(pos, lo, hi);
                if (p.type == "int") v = std::floor(v + 0.5);
                p.value[c] = v;
                saveParam(p);
            }
            InvalidateRect(D.view, nullptr, FALSE);
            return;
        }
}

void browseLut() {
    std::vector<wchar_t> buf(MAX_PATH * 2, 0);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = D.wnd;
    const std::wstring filter = makeFilter({{TX("dialog.lut_files"), L"*.cube"}, {TX("dialog.all_files"), L"*.*"}});
    o.lpstrFilter = filter.c_str();
    o.lpstrFile = buf.data();
    o.nMaxFile = (DWORD)buf.size();
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&o)) SetWindowTextW(D.edLut, buf.data());
}

void scrollTo(int pos) {
    RECT rc;
    GetClientRect(D.view, &rc);
    const int maxPos = std::max(0, S(D.contentH) - (int)rc.bottom);
    pos = std::min(maxPos, std::max(0, pos));
    if (pos == D.scrollY) return;
    const int dy = D.scrollY - pos;
    D.scrollY = pos;
    ScrollWindowEx(D.view, 0, dy, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    updateScrollBar();
    InvalidateRect(D.view, nullptr, FALSE);
}

LRESULT CALLBACK viewProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: paintView(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* d = (const DRAWITEMSTRUCT*)lp;
            if (d->CtlType == ODT_BUTTON) {
                ui::drawButton(d, D.dpi);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, cref(TEXT));
            SetBkColor(dc, cref(FIELD));
            return (LRESULT)D.brField;
        }
        case WM_HSCROLL: onSlider((HWND)lp); return 0;
        case WM_VSCROLL: {
            RECT rc;
            GetClientRect(h, &rc);
            int pos = D.scrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= S(36); break;
                case SB_LINEDOWN: pos += S(36); break;
                case SB_PAGEUP: pos -= rc.bottom; break;
                case SB_PAGEDOWN: pos += rc.bottom; break;
                case SB_THUMBTRACK: case SB_THUMBPOSITION: {
                    SCROLLINFO si{};
                    si.cbSize = sizeof(si);
                    si.fMask = SIF_TRACKPOS;
                    GetScrollInfo(h, SB_VERT, &si);
                    pos = si.nTrackPos;
                    break;
                }
            }
            scrollTo(pos);
            return 0;
        }
        case WM_MOUSEWHEEL: scrollTo(D.scrollY - (int)((short)HIWORD(wp)) * S(60) / WHEEL_DELTA); return 0;
        case WM_LBUTTONUP: {
            const POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
            for (Row& r : D.rows) {
                if (r.kind == Row::Effect) {
                    RECT c = checkRect(r, 20);
                    InflateRect(&c, S(4), S(4));
                    if (PtInRect(&c, pt)) { toggleBool(r); return 0; }
                } else if (r.kind == Row::Param) {
                    ParamInfo& p = D.infos[(size_t)r.idx];
                    if (p.animated || !p.audio.empty()) continue;
                    if (p.type == "bool") {
                        RECT c = checkRect(r, 206);
                        InflateRect(&c, S(4), S(4));
                        if (PtInRect(&c, pt)) { toggleBool(r); return 0; }
                    } else if (p.type == "color") {
                        const RECT sw{S(206), S(r.y + 6) - D.scrollY, S(270), S(r.y + 30) - D.scrollY};
                        if (PtInRect(&sw, pt)) { pickColor(p); return 0; }
                    }
                }
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == ID_PD_BROWSE) browseLut();
            else if (LOWORD(wp) == ID_PD_LUT && HIWORD(wp) == EN_CHANGE && D.ready) {
                g.base.lut = getText(D.edLut);
                postChanged();
            }
            return 0;
    }
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
        case WM_COMMAND:
            if (LOWORD(wp) == ID_PD_CLOSE) DestroyWindow(h);
            else if (LOWORD(wp) == ID_PD_RESET) {
                resetParamValues(D.file);
                reloadShader();
                buildRows();
            }
            return 0;
        case WM_TIMER:
            if (wp == kRestartTimer) {
                KillTimer(h, kRestartTimer);
                if (g.player && g.player->running()) startPlayer();  // le post-traitement se règle au démarrage de l'aperçu
            }
            return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY:
            KillTimer(h, kRestartTimer);
            D.done = true;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void showParams(HWND parent) {
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
        wc.lpfnWndProc = viewProc;
        wc.hIcon = nullptr;
        wc.lpszClassName = kViewClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    {  // etat de depart : post-traitement du dernier reglage, quantites par defaut sinon
        HBRUSH keep = D.brField;
        D = Dlg();
        D.brField = keep ? keep : CreateSolidBrush(cref(FIELD));
    }
    D.dpi = theme::dpiFor(parent);
    const auto& fx = postEffectInfos();
    for (size_t i = 0; i < fx.size() && i < 8; i++) D.fxAmt[i] = fx[i].def;
    std::vector<PostEffect> on;
    std::wstring bad;
    if (parsePostEffects(g.base.post, on, bad))
        for (const PostEffect& e : on) {
            D.fxOn[e.index] = true;
            D.fxAmt[e.index] = e.amount;
        }
    reloadShader();

    const DWORD style = WS_CAPTION | WS_SYSMENU;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int barH = S(64);
    const int clientH = std::min(S(640), (int)((work.bottom - work.top) * 85 / 100) - barH - S(40));
    RECT want{0, 0, S(640), std::max(S(320), clientH) + barH};
    AdjustWindowRectExForDpi(&want, style, FALSE, WS_EX_DLGMODALFRAME, D.dpi);
    RECT pr;
    GetWindowRect(parent, &pr);
    const int ww = want.right - want.left, wh = want.bottom - want.top;
    const int x = pr.left + ((pr.right - pr.left) - ww) / 2, y = std::max((int)work.top, (int)(pr.top + ((pr.bottom - pr.top) - wh) / 2));
    D.wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kDlgClass, TX("ui.params.title").c_str(), style | WS_VISIBLE | WS_CLIPCHILDREN, x, y, ww, wh, parent,
                            nullptr, hi, nullptr);
    if (!D.wnd) return;
    ui::darkTitleBar(D.wnd);
    RECT cr;
    GetClientRect(D.wnd, &cr);
    D.view = CreateWindowExW(0, kViewClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, cr.right, cr.bottom - barH, D.wnd,
                             nullptr, hi, nullptr);
    ui::darkTheme(D.view, L"DarkMode_Explorer");
    D.edLut = CreateWindowExW(0, L"EDIT", g.base.lut.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER, 0, 0, 10, 10, D.view,
                              (HMENU)(INT_PTR)ID_PD_LUT, hi, nullptr);
    SendMessageW(D.edLut, WM_SETFONT, (WPARAM)g.font, TRUE);
    ui::darkTheme(D.edLut, L"DarkMode_CFD");
    D.bBrowse = CreateWindowExW(0, L"BUTTON", TX("ui.btn.browse").c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, D.view,
                                (HMENU)(INT_PTR)ID_PD_BROWSE, hi, nullptr);
    ui::setupButton(D.bBrowse, IC_FOLDER, ui::BtnStyle::Secondary, CARD);
    SendMessageW(D.bBrowse, WM_SETFONT, (WPARAM)g.font, TRUE);
    auto button = [&](const char* key, int id, int icon, ui::BtnStyle st, int bx, int bw) {
        HWND b = CreateWindowExW(0, L"BUTTON", TX(key).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, bx, cr.bottom - barH + S(14), bw,
                                 S(36), D.wnd, (HMENU)(INT_PTR)id, hi, nullptr);
        ui::setupButton(b, icon, st, CARD);
        SendMessageW(b, WM_SETFONT, (WPARAM)g.font, TRUE);
        return b;
    };
    D.bReset = button("ui.btn.reset_params", ID_PD_RESET, IC_CLEAR, ui::BtnStyle::Secondary, S(20), S(190));
    D.bClose = button("ui.params.close", ID_PD_CLOSE, IC_CHECK, ui::BtnStyle::Primary, cr.right - S(20) - S(130), S(130));
    EnableWindow(D.bReset, D.hasShader && !D.infos.empty());
    buildRows();
    D.ready = true;

    // Boîte modale : la fenêtre principale est désactivée pendant l'affichage
    EnableWindow(parent, FALSE);
    SetFocus(D.bClose);
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
