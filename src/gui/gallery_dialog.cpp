// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Galerie d'exemples : vignettes par categorie, explication dans la langue de l'interface, ajout du shader a la liste (double-clic ou bouton).
// Fenetre non modale ; les vignettes sont des BMP 24 bits livres a cote des shaders (core/gallery).
#include "app.h"
#include "core/gallery.h"
#include "core/i18n.h"
#include "core/util.h"
#include "platform/platform.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"

using namespace theme;

namespace {

enum { ID_GAL_ADD = 951, ID_GAL_FOLDER, ID_GAL_CLOSE = IDCANCEL };

struct Thumb {
    std::vector<uint8_t> bgra;
    int w = 0, h = 0;
    bool tried = false;
};

struct Dialog {
    HWND wnd = nullptr, bAdd = nullptr, bFolder = nullptr, bClose = nullptr;
    UINT dpi = 96;
    std::vector<GalleryItem> items;
    std::vector<Thumb> thumbs;
    std::vector<int> shown;  // indices de items visibles (categorie choisie)
    int category = -1;       // -1 = toutes
    int selected = -1;       // index dans `shown`
    float scroll = 0, contentH = 0, viewH = 0;
    std::string lang;
} D;

const wchar_t* kClass = L"ShaderToMP4Gallery";

const char* kCategoryKeys[] = {"gallery.cat.maths", "gallery.cat.bruit", "gallery.cat.fractales", "gallery.cat.raymarching", "gallery.cat.2d",
                               "gallery.cat.post",  "gallery.cat.boucles", "gallery.cat.son",       "gallery.cat.vr",          "gallery.cat.passes"};

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }
float F(int v) { return (float)v * D.dpi / 96.0f; }

// BMP 24 bits non compresse, lignes du bas vers le haut (sortie de ffmpeg) -> BGRA
bool loadBmp(const std::wstring& path, Thumb& t) {
    std::string raw;
    if (!readFile(path, raw) || raw.size() < 54 || raw[0] != 'B' || raw[1] != 'M') return false;
    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, raw.data() + o, 4); return v; };
    const uint32_t off = u32(10);
    const int w = (int)u32(18), h0 = (int)u32(22);
    uint16_t bpp;
    memcpy(&bpp, raw.data() + 28, 2);
    const int h = h0 < 0 ? -h0 : h0;
    if (bpp != 24 || w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    const size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;
    if (off + stride * (size_t)h > raw.size()) return false;
    t.bgra.assign((size_t)w * h * 4, 255);
    for (int y = 0; y < h; y++) {
        const uint8_t* src = (const uint8_t*)raw.data() + off + stride * (size_t)(h0 < 0 ? y : h - 1 - y);
        uint8_t* dst = t.bgra.data() + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            dst[x * 4] = src[x * 3];
            dst[x * 4 + 1] = src[x * 3 + 1];
            dst[x * 4 + 2] = src[x * 3 + 2];
        }
    }
    t.w = w;
    t.h = h;
    return true;
}

void refilter() {
    D.shown.clear();
    for (size_t i = 0; i < D.items.size(); i++)
        if (D.category < 0 || D.items[i].category == galleryCategories()[(size_t)D.category]) D.shown.push_back((int)i);
    D.selected = D.shown.empty() ? -1 : 0;
    D.scroll = 0;
}

RECT chipRect(int i) {  // i = -1 : « Toutes » ; sinon la categorie i. Les pastilles s'alignent sur plusieurs lignes si la fenetre est etroite
    RECT rc;
    GetClientRect(D.wnd, &rc);
    int x = S(16), y = S(14);
    for (int k = -1; k <= i; k++) {
        const std::wstring label = k < 0 ? TX("gallery.all") : TX(kCategoryKeys[k]);
        const int wChip = (int)ui::measureText(label, F(13), true) + S(28);
        if (x + wChip > rc.right - S(16) && x > S(16)) {
            x = S(16);
            y += S(36);
        }
        if (k == i) return {x, y, x + wChip, y + S(30)};
        x += wChip + S(8);
    }
    return {};
}

int chipsBottom() {
    int bottom = 0;
    for (int i = -1; i < 10; i++) bottom = std::max<int>(bottom, chipRect(i).bottom);
    return bottom + S(12);
}

int infoHeight() { return S(150); }

int columns(int gridW) { return std::max(1, gridW / S(220)); }

RECT cardRect(int pos) {  // pos = rang dans `shown`, hors defilement (coordonnees client, y de la grille ajoute)
    RECT rc;
    GetClientRect(D.wnd, &rc);
    const int gridW = rc.right - S(32), cols = columns(gridW);
    const int cw = S(208), chh = S(148), gap = (gridW - cols * cw) / std::max(1, cols);
    const int col = pos % cols, row = pos / cols;
    const int x = S(16) + col * (cw + gap), y = chipsBottom() + row * (chh + S(12)) - (int)D.scroll;
    return {x, y, x + cw, y + chh};
}

RECT gridRect() {
    RECT rc;
    GetClientRect(D.wnd, &rc);
    return {0, chipsBottom(), rc.right, rc.bottom - infoHeight()};
}

void layoutControls() {
    RECT rc;
    GetClientRect(D.wnd, &rc);
    const int bh = S(34), y = rc.bottom - S(16) - bh;
    int x = rc.right - S(16);
    auto put = [&](HWND b, int w) {
        x -= S(w);
        MoveWindow(b, x, y, S(w), bh, TRUE);
        x -= S(8);
    };
    put(D.bClose, 100);
    put(D.bFolder, 150);
    put(D.bAdd, 170);
}

void clampScroll() { D.scroll = std::max(0.0f, std::min(D.scroll, std::max(0.0f, D.contentH - D.viewH))); }

void paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(D.wnd, &ps);
    RECT rc;
    GetClientRect(D.wnd, &rc);
    ui::Frame f(rc.right, rc.bottom, ui::rgb(BG));
    const float w = (float)rc.right, h = (float)rc.bottom;

    // Grille des vignettes (le fond de la carte recouvre ce qui depasse en haut)
    const RECT gr = gridRect();
    const int cols = columns(rc.right - S(32));
    const int rows = ((int)D.shown.size() + cols - 1) / cols;
    D.viewH = (float)(gr.bottom - gr.top);
    D.contentH = (float)rows * (F(148) + F(12));
    clampScroll();
    for (size_t pos = 0; pos < D.shown.size(); pos++) {
        const RECT r = cardRect((int)pos);
        if (r.bottom < gr.top || r.top > gr.bottom) continue;
        const int idx = D.shown[pos];
        Thumb& t = D.thumbs[(size_t)idx];
        if (!t.tried) {
            t.tried = true;
            if (!D.items[(size_t)idx].thumb.empty()) loadBmp(D.items[(size_t)idx].thumb, t);
        }
        const bool sel = (int)pos == D.selected;
        f.fillRound(ui::rectf(r), F(10), ui::rgb(sel ? FIELD_HOT : CARD));
        f.strokeRound(ui::rectf(r), F(10), ui::rgb(sel ? ACCENT : CARD_BORDER), sel ? 2.0f : 1.0f);
        const D2D1_RECT_F tr = ui::rectf((float)r.left + F(8), (float)r.top + F(8), (float)r.left + F(200), (float)r.top + F(116));
        if (t.w > 0) f.image(idx, t.bgra.data(), t.w, t.h, tr);
        else f.fillRound(tr, F(6), ui::rgb(FIELD));
        f.text(galleryTitle(D.items[(size_t)idx], D.lang), ui::rectf((float)r.left + F(10), (float)r.top + F(118), (float)r.right - F(8), (float)r.bottom - F(4)), F(12.5f), sel, ui::rgb(sel ? TEXT : MUTED));
    }
    // Les zones hors grille (au-dessus et au-dessous) sont repeintes par-dessus le defilement, puis les pastilles
    f.fillRound(ui::rectf(0, (float)gr.bottom, w, h), 0, ui::rgb(BG));
    f.fillRound(ui::rectf(0, 0, w, (float)gr.top), 0, ui::rgb(BG));
    for (int i = -1; i < 10; i++) {
        const RECT r = chipRect(i);
        const bool on = i == D.category;
        f.fillRound(ui::rectf(r), F(15), ui::rgb(on ? ACCENT : FIELD));
        f.text(i < 0 ? TX("gallery.all") : TX(kCategoryKeys[i]), ui::rectf(r), F(13), true, ui::rgb(on ? BG : MUTED), ui::Center);
    }

    // Panneau d'explication
    f.fillRound(ui::rectf(F(8), (float)gr.bottom + F(4), w - F(8), h - F(8)), F(12), ui::rgb(CARD));
    if (D.selected >= 0) {
        const GalleryItem& it = D.items[(size_t)D.shown[(size_t)D.selected]];
        f.text(galleryTitle(it, D.lang), ui::rectf(F(24), (float)gr.bottom + F(14), w - F(24), (float)gr.bottom + F(40)), F(16), true, ui::rgb(TEXT));
        f.text(galleryText(it, D.lang), ui::rectf(F(24), (float)gr.bottom + F(44), w - F(24), (float)gr.bottom + F(100)), F(13), false, ui::rgb(MUTED), ui::Left, false, true);
    } else {
        f.text(TX("gallery.empty"), ui::rectf(F(24), (float)gr.bottom + F(14), w - F(24), (float)gr.bottom + F(60)), F(14), false, ui::rgb(MUTED), ui::Left, false, true);
    }
    f.present(hdc);
    EndPaint(D.wnd, &ps);
}

void addSelected() {
    if (D.selected < 0) return;
    const GalleryItem& it = D.items[(size_t)D.shown[(size_t)D.selected]];
    if (g.running) return;
    addFiles({it.path});
    appendLog(TXF("gallery.added", galleryTitle(it, D.lang).c_str()));
}

int hitCard(POINT p) {
    const RECT gr = gridRect();
    if (p.y < gr.top || p.y > gr.bottom) return -1;
    for (size_t pos = 0; pos < D.shown.size(); pos++) {
        const RECT r = cardRect((int)pos);
        if (PtInRect(&r, p)) return (int)pos;
    }
    return -1;
}

int hitChip(POINT p) {
    for (int i = -1; i < 10; i++) {
        const RECT r = chipRect(i);
        if (PtInRect(&r, p)) return i;
    }
    return -2;
}

void scrollBy(float dy) {
    D.scroll += dy;
    clampScroll();
    InvalidateRect(D.wnd, nullptr, FALSE);
}

void refreshAddButton() { EnableWindow(D.bAdd, D.selected >= 0 && !g.running); }

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
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            const int chip = hitChip(p);
            if (chip != -2) {
                D.category = chip;
                refilter();
                refreshAddButton();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            const int c = hitCard(p);
            if (c >= 0) {
                D.selected = c;
                refreshAddButton();
                InvalidateRect(h, nullptr, FALSE);
                if (msg == WM_LBUTTONDBLCLK) addSelected();
            }
            return 0;
        }
        case WM_MOUSEWHEEL: scrollBy(-(float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * F(70)); return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                DestroyWindow(h);
                return 0;
            }
            if (wp == VK_RETURN) {
                addSelected();
                return 0;
            }
            if (!D.shown.empty() && (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN)) {
                RECT rc;
                GetClientRect(h, &rc);
                const int cols = columns(rc.right - S(32)), n = (int)D.shown.size();
                int s = D.selected < 0 ? 0 : D.selected;
                s += wp == VK_LEFT ? -1 : wp == VK_RIGHT ? 1 : wp == VK_UP ? -cols : cols;
                D.selected = std::max(0, std::min(n - 1, s));
                const RECT r = cardRect(D.selected), gr = gridRect();  // la carte choisie reste visible
                if (r.top < gr.top) scrollBy((float)(r.top - gr.top));
                else if (r.bottom > gr.bottom) scrollBy((float)(r.bottom - gr.bottom));
                refreshAddButton();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_GAL_ADD: addSelected(); break;
                case ID_GAL_FOLDER: {
                    const std::wstring dir = galleryDir(g.exeDir);
                    if (!dir.empty()) platform::openWithDefault(dir);
                    break;
                }
                case ID_GAL_CLOSE: DestroyWindow(h); break;
            }
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = S(640);
            mm->ptMinTrackSize.y = S(520);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY:
            ui::dropImages();
            D.wnd = nullptr;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void showGallery(HWND owner) {
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
        wc.style = CS_DBLCLKS;
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
    D.dpi = theme::dpiFor(owner);
    D.lang = i18n::language();
    D.items = listGallery(g.exeDir);
    D.thumbs.assign(D.items.size(), Thumb());
    refilter();
    RECT want{0, 0, S(960), S(700)};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectExForDpi(&want, style, FALSE, 0, D.dpi);
    RECT pr{};
    GetWindowRect(owner, &pr);
    const int ww = want.right - want.left, wh = want.bottom - want.top;
    const RECT place = theme::fitToWorkArea({pr.left + 30, pr.top + 30, pr.left + 30 + ww, pr.top + 30 + wh});
    D.wnd = CreateWindowExW(0, kClass, TXF("gallery.title", APP_NAME_WSTR).c_str(), style | WS_VISIBLE | WS_CLIPCHILDREN, place.left, place.top,
                            place.right - place.left, place.bottom - place.top, owner, nullptr, hi, nullptr);
    if (!D.wnd) return;
    ui::darkTitleBar(D.wnd);
    auto button = [&](const char* key, int id, int icon, ui::BtnStyle st) {
        HWND b = CreateWindowExW(0, L"BUTTON", TX(key).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, D.wnd, (HMENU)(INT_PTR)id, hi, nullptr);
        ui::setupButton(b, icon, st, CARD);  // le panneau d'explication est derriere les boutons
        SendMessageW(b, WM_SETFONT, (WPARAM)g.font, TRUE);
        return b;
    };
    D.bAdd = button("gallery.btn.add", ID_GAL_ADD, IC_ADD, ui::BtnStyle::Primary);
    D.bFolder = button("gallery.btn.folder", ID_GAL_FOLDER, IC_FOLDER, ui::BtnStyle::Secondary);
    D.bClose = button("gallery.btn.close", ID_GAL_CLOSE, IC_CHECK, ui::BtnStyle::Secondary);
    layoutControls();
    refreshAddButton();
    SetFocus(D.wnd);
}
