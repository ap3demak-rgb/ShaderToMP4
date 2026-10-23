// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Galerie d'exemples : vignettes par categorie, explication dans la langue de l'interface, ajout du shader a la liste (double-clic, Entree ou bouton).
// Fenetre non modale faite de controles Windows (pastilles = boutons, vignettes = liste en grandes icones, textes = controles statiques) : clavier complet
// (Tab, fleches, Entree, Echap) et lecteurs d'ecran. Les vignettes sont des BMP 24 bits livres a cote des shaders (core/gallery).
#include "accessible.h"
#include "app.h"
#include "core/gallery.h"
#include "core/i18n.h"
#include "core/util.h"
#include "platform/platform.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"
#include <commctrl.h>

using namespace theme;

namespace {

enum { ID_GAL_LIST = 950, ID_GAL_ADD, ID_GAL_FOLDER, ID_GAL_TITLE, ID_GAL_TEXT, ID_GAL_CHIP0 = 960, ID_GAL_CLOSE = IDCANCEL };
constexpr int kThumbW = 128, kThumbH = 72;

struct Dialog {
    HWND wnd = nullptr, list = nullptr, bAdd = nullptr, bFolder = nullptr, bClose = nullptr, title = nullptr, text = nullptr;
    HWND chips[11] = {};  // 0 = toutes, 1.. = categories
    HIMAGELIST images = nullptr;
    UINT dpi = 96;
    std::vector<GalleryItem> items;
    int category = -1;  // -1 = toutes
    std::string lang;
    HBRUSH brCard = nullptr;
} D;

const wchar_t* kClass = L"ShaderToMP4Gallery";

const char* kCategoryKeys[] = {"gallery.cat.maths", "gallery.cat.bruit", "gallery.cat.fractales", "gallery.cat.raymarching", "gallery.cat.2d",
                               "gallery.cat.post",  "gallery.cat.boucles", "gallery.cat.son",       "gallery.cat.vr",          "gallery.cat.passes"};

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }
float F(int v) { return (float)v * D.dpi / 96.0f; }

int infoHeight() { return S(150); }

// Pastilles sur une ou plusieurs lignes selon la largeur ; rend la hauteur occupee
int layoutChips(int width, bool move) {
    int x = S(16), y = S(14), bottom = y;
    for (int i = 0; i < 11; i++) {
        wchar_t buf[96] = L"";
        GetWindowTextW(D.chips[i], buf, 96);
        const int w = (int)ui::measureText(buf, F(13), true) + S(30);
        if (x + w > width - S(16) && x > S(16)) {
            x = S(16);
            y += S(36);
        }
        if (move) MoveWindow(D.chips[i], x, y, w, S(30), TRUE);
        bottom = y + S(30);
        x += w + S(8);
    }
    return bottom + S(12);
}

void layoutControls() {
    RECT rc;
    GetClientRect(D.wnd, &rc);
    const int top = layoutChips(rc.right, true), info = infoHeight();
    MoveWindow(D.list, S(12), top, rc.right - S(24), std::max(S(80), (int)rc.bottom - top - info - S(8)), TRUE);
    const int py = rc.bottom - info;  // panneau d'explication
    MoveWindow(D.title, S(28), py + S(14), rc.right - S(56), S(26), TRUE);
    MoveWindow(D.text, S(28), py + S(44), rc.right - S(56), S(56), TRUE);
    const int bh = S(34), y = rc.bottom - S(16) - bh;
    int x = rc.right - S(24);
    auto put = [&](HWND b, int w) {
        x -= S(w);
        MoveWindow(b, x, y, S(w), bh, TRUE);
        x -= S(8);
    };
    put(D.bClose, 100);
    put(D.bFolder, 150);
    put(D.bAdd, 170);
}

int selectedItem() {
    const int row = (int)SendMessageW(D.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (row < 0) return -1;
    LVITEMW it{};
    it.mask = LVIF_PARAM;
    it.iItem = row;
    SendMessageW(D.list, LVM_GETITEMW, 0, (LPARAM)&it);
    return (int)it.lParam;
}

void showSelection() {
    const int idx = selectedItem();
    if (idx < 0) {
        SetWindowTextW(D.title, L"");
        SetWindowTextW(D.text, D.items.empty() ? TX("gallery.empty").c_str() : L"");
    } else {
        SetWindowTextW(D.title, galleryTitle(D.items[(size_t)idx], D.lang).c_str());
        SetWindowTextW(D.text, galleryText(D.items[(size_t)idx], D.lang).c_str());
    }
    EnableWindow(D.bAdd, idx >= 0 && !g.running);
}

void fillList() {
    SendMessageW(D.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(D.list, LVM_DELETEALLITEMS, 0, 0);
    int row = 0;
    for (size_t i = 0; i < D.items.size(); i++) {
        if (D.category >= 0 && D.items[i].category != galleryCategories()[(size_t)D.category]) continue;
        const std::wstring title = galleryTitle(D.items[i], D.lang);
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        it.iItem = row++;
        it.pszText = const_cast<wchar_t*>(title.c_str());
        it.iImage = (int)i;
        it.lParam = (LPARAM)i;
        SendMessageW(D.list, LVM_INSERTITEMW, 0, (LPARAM)&it);
    }
    SendMessageW(D.list, WM_SETREDRAW, TRUE, 0);
    if (row > 0) {
        LVITEMW st{};
        st.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        st.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(D.list, LVM_SETITEMSTATE, 0, (LPARAM)&st);
    }
    for (int i = 0; i < 11; i++) ui::setButtonStyle(D.chips[i], i - 1 == D.category ? ui::BtnStyle::Primary : ui::BtnStyle::Chip);
    showSelection();
}

void addSelected() {
    const int idx = selectedItem();
    if (idx < 0 || g.running) return;
    const GalleryItem& it = D.items[(size_t)idx];
    addFiles({it.path});
    appendLog(TXF("gallery.added", galleryTitle(it, D.lang).c_str()));
}

void buildImages() {
    D.images = ImageList_Create(kThumbW, kThumbH, ILC_COLOR24, (int)D.items.size() + 1, 8);
    for (const GalleryItem& it : D.items) {
        HBITMAP bmp = it.thumb.empty() ? nullptr : (HBITMAP)LoadImageW(nullptr, it.thumb.c_str(), IMAGE_BITMAP, kThumbW, kThumbH, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        if (!bmp) {  // vignette absente ou illisible : carre uni
            HDC dc = GetDC(nullptr);
            bmp = CreateCompatibleBitmap(dc, kThumbW, kThumbH);
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, bmp);
            HBRUSH br = CreateSolidBrush(cref(FIELD));
            RECT r{0, 0, kThumbW, kThumbH};
            FillRect(mem, &r, br);
            DeleteObject(br);
            SelectObject(mem, old);
            DeleteDC(mem);
            ReleaseDC(nullptr, dc);
        }
        ImageList_Add(D.images, bmp, nullptr);
        DeleteObject(bmp);
    }
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {  // la carte arrondie du panneau d'explication
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            ui::Frame f(rc.right, rc.bottom, ui::rgb(BG));
            f.fillRound(ui::rectf(F(8), (float)(rc.bottom - infoHeight()) + F(4), (float)rc.right - F(8), (float)rc.bottom - F(8)), F(12), ui::rgb(CARD));
            f.present(hdc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: layoutControls(); InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, cref((HWND)lp == D.title ? TEXT : MUTED));
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
        case WM_NOTIFY: {
            const NMHDR* nh = (const NMHDR*)lp;
            if (nh->idFrom == ID_GAL_LIST) {
                if (nh->code == LVN_ITEMCHANGED) {
                    const NMLISTVIEW* nl = (const NMLISTVIEW*)lp;
                    if ((nl->uChanged & LVIF_STATE) && ((nl->uNewState ^ nl->uOldState) & LVIS_SELECTED)) showSelection();
                } else if (nh->code == NM_DBLCLK) {
                    addSelected();
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id >= ID_GAL_CHIP0 && id < ID_GAL_CHIP0 + 11) {
                D.category = id - ID_GAL_CHIP0 - 1;
                fillList();
                SetFocus(D.list);
            } else if (id == ID_GAL_ADD || id == IDOK) {  // Entree (bouton par defaut du gestionnaire de dialogue) ajoute le shader choisi
                addSelected();
            } else if (id == ID_GAL_FOLDER) {
                const std::wstring dir = galleryDir(g.exeDir);
                if (!dir.empty()) platform::openWithDefault(dir);
            } else if (id == ID_GAL_CLOSE) {
                DestroyWindow(h);
            }
            return 0;
        }
        case WM_SETFOCUS: SetFocus(D.list); return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = S(640);
            mm->ptMinTrackSize.y = S(520);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY:
            if (D.images) ImageList_Destroy(D.images);
            if (D.brCard) DeleteObject(D.brCard);
            D.images = nullptr;
            D.brCard = nullptr;
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
    auto button = [&](const wchar_t* label, int id, int icon, ui::BtnStyle st, unsigned bg) {
        HWND b = CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, D.wnd, (HMENU)(INT_PTR)id, hi, nullptr);
        ui::setupButton(b, icon, st, bg);
        SendMessageW(b, WM_SETFONT, (WPARAM)g.font, TRUE);
        return b;
    };
    D.chips[0] = button(TX("gallery.all").c_str(), ID_GAL_CHIP0, 0, ui::BtnStyle::Chip, BG);
    for (int i = 0; i < 10; i++) D.chips[i + 1] = button(TX(kCategoryKeys[i]).c_str(), ID_GAL_CHIP0 + 1 + i, 0, ui::BtnStyle::Chip, BG);
    D.list = CreateWindowExW(0, L"SysListView32", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_AUTOARRANGE, 0, 0, 10, 10, D.wnd,
                             (HMENU)(INT_PTR)ID_GAL_LIST, hi, nullptr);
    SendMessageW(D.list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
    SendMessageW(D.list, LVM_SETBKCOLOR, 0, cref(CARD));
    SendMessageW(D.list, LVM_SETTEXTBKCOLOR, 0, cref(CARD));
    SendMessageW(D.list, LVM_SETTEXTCOLOR, 0, cref(TEXT));
    SendMessageW(D.list, WM_SETFONT, (WPARAM)g.font, TRUE);
    ui::darkTheme(D.list, L"DarkMode_Explorer");
    buildImages();
    SendMessageW(D.list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)D.images);
    SendMessageW(D.list, LVM_SETICONSPACING, 0, MAKELPARAM(kThumbW + S(36), kThumbH + S(50)));
    D.title = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, D.wnd, (HMENU)(INT_PTR)ID_GAL_TITLE, hi, nullptr);
    SendMessageW(D.title, WM_SETFONT, (WPARAM)g.fontBold, TRUE);
    D.text = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, D.wnd, (HMENU)(INT_PTR)ID_GAL_TEXT, hi, nullptr);
    SendMessageW(D.text, WM_SETFONT, (WPARAM)g.font, TRUE);
    D.bAdd = button(TX("gallery.btn.add").c_str(), ID_GAL_ADD, IC_ADD, ui::BtnStyle::Primary, CARD);
    D.bFolder = button(TX("gallery.btn.folder").c_str(), ID_GAL_FOLDER, IC_FOLDER, ui::BtnStyle::Secondary, CARD);
    D.bClose = button(TX("gallery.btn.close").c_str(), ID_GAL_CLOSE, IC_CHECK, ui::BtnStyle::Secondary, CARD);
    setAccessibleName(D.list, "gallery.list");
    layoutControls();
    fillList();
    SetFocus(D.list);
}
