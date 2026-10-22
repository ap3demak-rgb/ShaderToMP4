// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Fenêtre d'achat de l'édition Pro : trois montants au choix (cases exclusives), « Ouvrir PayPal » ouvre la page PayPal.me de l'auteur dans le
// navigateur. Le programme n'envoie rien et ne traite aucun paiement ; la fenêtre n'existe qu'en version d'essai (le bouton qui l'ouvre disparaît
// avec une licence valide).
#include "app.h"
#include "core/i18n.h"
#include "core/prolicense.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"

using namespace theme;

namespace {

enum { ID_BUY_AMOUNT0 = 801, ID_BUY_PAY = 810, ID_BUY_CLOSE = IDCANCEL };

struct Dialog {
    HWND wnd = nullptr, amount[3] = {nullptr, nullptr, nullptr}, bPay = nullptr, bClose = nullptr;
    UINT dpi = 96;
    int sel = 0;  // indice dans purchaseAmounts() : 5 euros par défaut
    bool done = false;
} D;

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }
float F(int v) { return (float)v * D.dpi / 96.0f; }

const wchar_t* kClass = L"ShaderToMP4Buy";

void applySelection() {
    const auto& amounts = purchaseAmounts();
    for (size_t i = 0; i < amounts.size() && i < 3; i++) {
        const bool on = (int)i == D.sel;
        ui::setButtonStyle(D.amount[i], on ? ui::BtnStyle::Primary : ui::BtnStyle::Chip);
        ui::setButtonIcon(D.amount[i], on ? IC_CHECK : 0);
        SendMessageW(D.amount[i], WM_SETTEXT, 0, (LPARAM)TXF("buy.amount", amounts[i]).c_str());
        InvalidateRect(D.amount[i], nullptr, TRUE);
    }
}

void layoutControls() {
    RECT rc;
    GetClientRect(D.wnd, &rc);
    const int w = rc.right, bh = S(40), gap = S(12);
    const int aw = S(110);
    int x = (w - (3 * aw + 2 * gap)) / 2;
    const int y = S(196);
    for (int i = 0; i < 3; i++) {
        MoveWindow(D.amount[i], x, y, aw, bh, TRUE);
        x += aw + gap;
    }
    const int pw = S(170), cw = S(110), by = rc.bottom - S(24) - S(36);
    x = (w - (pw + cw + gap)) / 2;
    MoveWindow(D.bPay, x, by, pw, S(36), TRUE);
    MoveWindow(D.bClose, x + pw + gap, by, cw, S(36), TRUE);
}

void paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(D.wnd, &ps);
    RECT rc;
    GetClientRect(D.wnd, &rc);
    const float w = (float)rc.right;
    ui::Frame f(rc.right, rc.bottom, ui::rgb(BG));
    D2D1_RECT_F card = ui::rectf(F(16), F(16), w - F(16), (float)rc.bottom - F(16));
    f.fillRound(card, F(16), ui::rgb(CARD));
    f.strokeRound(card, F(16), ui::rgb(CARD_BORDER), 1.0f);
    float cy = F(32);
    f.text(TXF("buy.title", APP_NAME_WSTR), ui::rectf(F(30), cy, w - F(30), cy + F(34)), F(22), true, ui::rgb(TEXT), ui::Center);
    cy += F(48);
    f.text(TX("buy.text"), ui::rectf(F(40), cy, w - F(40), cy + F(76)), F(13), false, ui::rgb(MUTED), ui::Left, false, true);
    cy += F(96);
    f.text(TX("buy.heading"), ui::rectf(F(40), cy, w - F(40), cy + F(24)), F(14), true, ui::rgb(TEXT), ui::Center);
    // note sous les cases (les boutons de montant sont des contrôles enfants posés à y = 196 dp)
    f.text(TXF("buy.note", licenseFileName()), ui::rectf(F(40), F(256), w - F(40), F(256 + 96)), F(11.5f), false, ui::rgb(FAINT), ui::Left, false, true);
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
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id >= ID_BUY_AMOUNT0 && id < ID_BUY_AMOUNT0 + 3) {
                D.sel = id - ID_BUY_AMOUNT0;
                applySelection();
            } else if (id == ID_BUY_PAY) {  // uniquement sur clic : l'adresse porte le montant coché
                const auto& amounts = purchaseAmounts();
                ShellExecuteW(h, L"open", purchaseUrl(amounts[(size_t)D.sel]).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (id == ID_BUY_CLOSE) {
                DestroyWindow(h);
            }
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: D.done = true; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void showBuy(HWND parent) {
    static bool registered = false;
    HINSTANCE hi = GetModuleHandleW(nullptr);
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
    D.dpi = theme::dpiFor(parent);
    RECT want{0, 0, S(520), S(440)};
    const DWORD style = WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&want, style, FALSE, WS_EX_DLGMODALFRAME, D.dpi);
    RECT pr;
    GetWindowRect(parent, &pr);
    const int ww = want.right - want.left, wh = want.bottom - want.top;
    const int x = pr.left + ((pr.right - pr.left) - ww) / 2, y = pr.top + ((pr.bottom - pr.top) - wh) / 2;
    const std::wstring title = TXF("buy.title", APP_NAME_WSTR);
    D.wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, title.c_str(), style | WS_VISIBLE | WS_CLIPCHILDREN, x, y, ww, wh, parent, nullptr, hi, nullptr);
    if (!D.wnd) return;
    ui::darkTitleBar(D.wnd);
    auto button = [&](const char* key, int id, int icon, ui::BtnStyle st) {
        HWND b = CreateWindowExW(0, L"BUTTON", key ? TX(key).c_str() : L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, D.wnd,
                                 (HMENU)(INT_PTR)id, hi, nullptr);
        ui::setupButton(b, icon, st, CARD);
        SendMessageW(b, WM_SETFONT, (WPARAM)g.font, TRUE);
        return b;
    };
    for (int i = 0; i < 3; i++) D.amount[i] = button(nullptr, ID_BUY_AMOUNT0 + i, 0, ui::BtnStyle::Chip);
    D.bPay = button("buy.btn.pay", ID_BUY_PAY, IC_ZAP, ui::BtnStyle::Primary);
    D.bClose = button("buy.btn.close", ID_BUY_CLOSE, IC_CHECK, ui::BtnStyle::Secondary);
    applySelection();
    layoutControls();

    EnableWindow(parent, FALSE);
    SetFocus(D.amount[0]);
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
