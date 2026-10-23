// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Visite guidee du premier lancement : une petite carte non modale presente cinq etapes (ajouter, regler, apercu, lancer, aide) et entoure l'element
// concerne dans la fenetre principale. Elle ne bloque jamais : la fenetre principale reste utilisable. Fermer la carte, passer ou terminer la
// visite l'arrete pour de bon (window.ini : tour=0) ; « Revoir la visite » dans l'aide la relance.
#include "app.h"
#include "core/i18n.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"

using namespace theme;

namespace {

enum { ID_TOUR_NEXT = 961, ID_TOUR_BACK, ID_TOUR_SKIP };

struct Dialog {
    HWND wnd = nullptr, owner = nullptr, bNext = nullptr, bBack = nullptr, bSkip = nullptr;
    UINT dpi = 96;
    int step = 0;
} D;

const wchar_t* kClass = L"ShaderToMP4Tour";
const char* kText[5] = {"tour.step1", "tour.step2", "tour.step3", "tour.step4", "tour.step5"};
constexpr int kSteps = 5;

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }
float F(int v) { return (float)v * D.dpi / 96.0f; }

RECT clientRectOf(HWND ctl) {  // rectangle d'un controle de la fenetre principale, en coordonnees client de celle-ci
    RECT r{};
    if (!ctl || !IsWindowVisible(ctl)) return r;
    GetWindowRect(ctl, &r);
    POINT a{r.left, r.top}, b{r.right, r.bottom};
    ScreenToClient(g.wnd, &a);
    ScreenToClient(g.wnd, &b);
    return {a.x, a.y, b.x, b.y};
}

RECT targetRect() {
    switch (D.step) {
        case 0: {
            RECT l = clientRectOf(g.list), a = clientRectOf(g.bAdd), u{};
            UnionRect(&u, &l, &a);
            return u;
        }
        case 1: return g.cards.empty() ? RECT{} : g.cards[0].r;
        case 2: return clientRectOf(g.bPrev);
        case 3: return clientRectOf(g.bStart);
        default: return RECT{};
    }
}

void place() {  // la carte se met a cote de l'element presente, jamais dessus, et reste dans l'ecran
    RECT mw;
    GetWindowRect(g.wnd, &mw);
    const int cw = S(420), ch = S(210);
    RECT t = targetRect();
    int x, y;
    if (t.right > t.left) {
        POINT tl{t.left, t.top}, br{t.right, t.bottom};
        ClientToScreen(g.wnd, &tl);
        ClientToScreen(g.wnd, &br);
        x = tl.x;
        y = br.y + S(18);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(g.wnd, MONITOR_DEFAULTTONEAREST), &mi);
        if (y + ch > mi.rcWork.bottom) y = tl.y - ch - S(18);  // pas la place dessous : au-dessus
        if (y < mi.rcWork.top) y = std::min<int>((int)mi.rcWork.bottom - ch, (int)br.y - ch - S(24));  // ni dessus : sur l'element, le plus bas possible
    } else {
        x = mw.left + ((mw.right - mw.left) - cw) / 2;
        y = mw.top + ((mw.bottom - mw.top) - ch) / 2;
    }
    const RECT r = theme::fitToWorkArea({x, y, x + cw, y + ch});
    SetWindowPos(D.wnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    g.tourRect = t;
    InvalidateRect(g.wnd, nullptr, FALSE);
}

void layoutButtons() {
    RECT rc;
    GetClientRect(D.wnd, &rc);
    const int bh = S(34), y = rc.bottom - S(16) - bh;
    int x = rc.right - S(16);
    x -= S(110);
    MoveWindow(D.bNext, x, y, S(110), bh, TRUE);
    x -= S(8) + S(90);
    MoveWindow(D.bBack, x, y, S(90), bh, TRUE);
    MoveWindow(D.bSkip, S(16), y, S(110), bh, TRUE);
}

void refreshButtons() {
    SetWindowTextW(D.bNext, TX(D.step == kSteps - 1 ? "tour.finish" : "tour.next").c_str());
    ShowWindow(D.bBack, D.step > 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(D.bSkip, D.step < kSteps - 1 ? SW_SHOW : SW_HIDE);
    InvalidateRect(D.bNext, nullptr, FALSE);
}

void goTo(int s) {
    D.step = std::max(0, std::min(kSteps - 1, s));
    refreshButtons();
    SetWindowTextW(D.wnd, TX(kText[D.step]).c_str());  // le texte de l'etape est le nom de la fenetre : lu par les lecteurs d'ecran
    place();
    InvalidateRect(D.wnd, nullptr, FALSE);
}

void stop() {  // fin de la visite (passee, terminee ou fermee) : plus jamais au demarrage
    g.tourEnabled = false;
    g.tourOn = false;
    g.tourRect = RECT{};
    InvalidateRect(g.wnd, nullptr, FALSE);
    DestroyWindow(D.wnd);
}

void paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(D.wnd, &ps);
    RECT rc;
    GetClientRect(D.wnd, &rc);
    ui::Frame f(rc.right, rc.bottom, ui::rgb(ACCENT));  // le fond opaque fait la bordure de 2 px
    const float w = (float)rc.right, h = (float)rc.bottom;
    f.fillRound(ui::rectf(2, 2, w - 2, h - 2), 0, ui::rgb(CARD));
    f.text(TXF("tour.title", D.step + 1, kSteps), ui::rectf(F(20), F(14), w - F(20), F(38)), F(12.5f), true, ui::rgb(ACCENT));
    f.text(TX(kText[D.step]), ui::rectf(F(20), F(44), w - F(20), h - F(70)), F(14.5f), false, ui::rgb(TEXT), ui::Left, false, true);
    for (int i = 0; i < kSteps; i++)  // points d'avancement
        f.fillCircle(w - F(24) - (float)(kSteps - 1 - i) * F(14), F(26), F(3.5f), ui::rgb(i == D.step ? ACCENT : FIELD_BORDER));
    f.present(hdc);
    EndPaint(D.wnd, &ps);
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
            if (d->CtlType == ODT_BUTTON) {
                ui::drawButton(d, D.dpi);
                return TRUE;
            }
            break;
        }
        case WM_TIMER:  // la fenetre principale a pu etre deplacee ou redimensionnee
            if (wp == 1) {
                const RECT t = targetRect();
                if (memcmp(&t, &g.tourRect, sizeof(RECT)) != 0) place();
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) stop();
            else if (wp == VK_RETURN || wp == VK_RIGHT) D.step == kSteps - 1 ? stop() : goTo(D.step + 1);
            else if (wp == VK_LEFT) goTo(D.step - 1);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_TOUR_NEXT: D.step == kSteps - 1 ? stop() : goTo(D.step + 1); break;
                case ID_TOUR_BACK: goTo(D.step - 1); break;
                case ID_TOUR_SKIP: stop(); break;
                case IDCANCEL: stop(); break;  // Echap
                case IDOK: D.step == kSteps - 1 ? stop() : goTo(D.step + 1); break;  // Entree
            }
            return 0;
        case WM_CLOSE: stop(); return 0;
        case WM_DESTROY:
            KillTimer(h, 1);
            g.tourOn = false;
            g.tourRect = RECT{};
            InvalidateRect(g.wnd, nullptr, FALSE);
            D.wnd = nullptr;
            return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void showTour(HWND owner, bool force) {
    if (!force && !g.tourEnabled) return;
    if (D.wnd) {
        goTo(0);
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
        wc.hbrBackground = CreateSolidBrush(cref(CARD));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    D = Dialog();
    D.owner = owner;
    D.dpi = theme::dpiFor(owner);
    g.tourOn = true;
    D.wnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, TX("tour.window_title").c_str(), WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, S(420), S(210), owner, nullptr, hi, nullptr);
    if (!D.wnd) {
        g.tourOn = false;
        return;
    }
    auto button = [&](const char* key, int id, int icon, ui::BtnStyle st) {
        HWND b = CreateWindowExW(0, L"BUTTON", TX(key).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, D.wnd, (HMENU)(INT_PTR)id, hi, nullptr);
        ui::setupButton(b, icon, st, CARD);
        SendMessageW(b, WM_SETFONT, (WPARAM)g.font, TRUE);
        return b;
    };
    D.bNext = button("tour.next", ID_TOUR_NEXT, IC_CHECK, ui::BtnStyle::Primary);
    D.bBack = button("tour.back", ID_TOUR_BACK, 0, ui::BtnStyle::Secondary);
    D.bSkip = button("tour.skip", ID_TOUR_SKIP, 0, ui::BtnStyle::Ghost);
    layoutButtons();
    SetTimer(D.wnd, 1, 400, nullptr);
    goTo(0);
    SetFocus(D.wnd);
}
