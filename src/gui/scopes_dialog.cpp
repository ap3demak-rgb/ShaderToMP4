// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Oscilloscopes de l'apercu : histogramme, forme d'onde et vectorscope de l'image affichee (calcul dans core/scopes.cpp). La fenetre se met a jour a chaque
// nouvelle image d'apercu, au plus dix fois par seconde ; une ligne de texte resume les valeurs (utile aux lecteurs d'ecran).
#include "accessible.h"
#include "app.h"
#include "core/i18n.h"
#include "core/scopes.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_theme.h"

using namespace theme;

namespace {

struct Panel {
    std::vector<uint8_t> px;
    int w = 0, h = 0;
};

struct Dialog {
    HWND wnd = nullptr, summary = nullptr;
    UINT dpi = 96;
    Panel hist, wave, vec;
    std::wstring summaryText;
    std::vector<uint8_t> pending;  // derniere image recue pendant le temps de repos : traitee par le minuteur
    int pendingW = 0, pendingH = 0;
    std::chrono::steady_clock::time_point last;
    bool has = false;
} D;

const wchar_t* kClass = L"ShaderToMP4Scopes";
constexpr UINT_PTR kTimer = 1;

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }

void compute(const uint8_t* bgra, int w, int h) {
    Scopes s;
    computeScopes(bgra, w, h, s);
    renderHistogram(s, D.hist.px, D.hist.w, D.hist.h);
    renderWaveform(s, D.wave.px, D.wave.w, D.wave.h);
    renderVectorscope(s, D.vec.px, D.vec.w, D.vec.h);
    // resume : luminance minimale, maximale, moyenne et part des pixels ecretes
    int lo = 255, hi = 0;
    double sum = 0;
    uint64_t clipHi = 0, clipLo = 0;
    for (int l = 0; l < 256; l++) {
        if (!s.hist[3][l]) continue;
        lo = std::min(lo, l);
        hi = std::max(hi, l);
        sum += (double)l * s.hist[3][l];
        if (l >= 254) clipHi += s.hist[3][l];
        if (l <= 1) clipLo += s.hist[3][l];
    }
    const double n = (double)std::max<uint64_t>(1, s.pixels);
    D.summaryText = TXF("ui.scopes.summary", lo, hi, i18n::fixed(sum / n, 1).c_str(), i18n::fixed(100.0 * clipLo / n, 1).c_str(), i18n::fixed(100.0 * clipHi / n, 1).c_str());
    SetWindowTextW(D.summary, D.summaryText.c_str());
    D.has = true;
    D.last = std::chrono::steady_clock::now();
    InvalidateRect(D.wnd, nullptr, FALSE);
}

void drawPanel(HDC dc, const Panel& p, const RECT& r, const std::wstring& caption) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, cref(MUTED));
    SelectObject(dc, g.font);
    RECT cap{r.left, r.top, r.right, r.top + S(20)};
    DrawTextW(dc, caption.c_str(), -1, &cap, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    RECT img{r.left, r.top + S(22), r.right, r.bottom};
    HBRUSH frame = CreateSolidBrush(cref(CARD_BORDER));
    FrameRect(dc, &img, frame);
    DeleteObject(frame);
    if (p.px.empty() || p.w <= 0 || p.h <= 0) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = p.w;
    bi.bmiHeader.biHeight = -p.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, img.left + 1, img.top + 1, img.right - img.left - 2, img.bottom - img.top - 2, 0, 0, p.w, p.h, p.px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
}

void layoutPanels(RECT& a, RECT& b, RECT& c) {
    RECT cl;
    GetClientRect(D.wnd, &cl);
    const int pad = S(14), gap = S(12), top = pad, bottom = cl.bottom - pad - S(26);
    const int w = (cl.right - 2 * pad - 2 * gap) / 3;
    a = {pad, top, pad + w, bottom};
    b = {pad + w + gap, top, pad + 2 * w + gap, bottom};
    c = {pad + 2 * (w + gap), top, cl.right - pad, bottom};
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT cl;
            GetClientRect(h, &cl);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, cl.right, cl.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            HBRUSH bg = CreateSolidBrush(cref(BG));
            FillRect(mem, &cl, bg);
            DeleteObject(bg);
            RECT a, b, c;
            layoutPanels(a, b, c);
            drawPanel(mem, D.hist, a, TX("ui.scopes.histogram"));
            drawPanel(mem, D.wave, b, TX("ui.scopes.waveform"));
            drawPanel(mem, D.vec, c, TX("ui.scopes.vectorscope"));
            if (!D.has) {
                SetBkMode(mem, TRANSPARENT);
                SetTextColor(mem, cref(FAINT));
                SelectObject(mem, g.font);
                DrawTextW(mem, TX("ui.scopes.none").c_str(), -1, &cl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            BitBlt(dc, 0, 0, cl.right, cl.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: {
            RECT cl;
            GetClientRect(h, &cl);
            MoveWindow(D.summary, S(14), cl.bottom - S(14) - S(22), cl.right - S(28), S(22), TRUE);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, cref(MUTED));
            SetBkColor(hdc, cref(BG));
            static HBRUSH br = CreateSolidBrush(cref(BG));
            return (LRESULT)br;
        }
        case WM_TIMER:
            if (wp == kTimer) {
                KillTimer(h, kTimer);
                if (!D.pending.empty()) {
                    std::vector<uint8_t> px;
                    px.swap(D.pending);
                    compute(px.data(), D.pendingW, D.pendingH);
                }
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) DestroyWindow(h);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL) DestroyWindow(h);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = S(620);
            mm->ptMinTrackSize.y = S(260);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: D.wnd = nullptr; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

// Appele a chaque changement de l'image affichee : rien si la fenetre est fermee
void scopesUpdate(const uint8_t* bgra, int w, int h) {
    if (!D.wnd || !bgra || w <= 0 || h <= 0) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - D.last < std::chrono::milliseconds(100)) {  // trop tot : l'image est gardee, le minuteur la traitera
        D.pending.assign(bgra, bgra + (size_t)w * h * 4);
        D.pendingW = w;
        D.pendingH = h;
        SetTimer(D.wnd, kTimer, 120, nullptr);
        return;
    }
    D.pending.clear();
    compute(bgra, w, h);
}

void showScopes(HWND owner) {
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
        wc.hbrBackground = nullptr;
        wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    D = Dialog();
    D.dpi = dpiFor(owner ? owner : g.wnd);
    RECT want{0, 0, S(900), S(380)};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectExForDpi(&want, style, FALSE, 0, D.dpi);
    RECT pr{};
    GetWindowRect(owner ? owner : g.wnd, &pr);
    const int ww = want.right - want.left, wh = want.bottom - want.top;
    RECT place = fitToWorkArea({pr.left + 60, pr.top + 60, pr.left + 60 + ww, pr.top + 60 + wh});
    D.wnd = CreateWindowExW(0, kClass, TX("ui.scopes.title").c_str(), style | WS_VISIBLE | WS_CLIPCHILDREN, place.left, place.top, place.right - place.left, place.bottom - place.top,
                            owner ? owner : g.wnd, nullptr, hi, nullptr);
    if (!D.wnd) return;
    ui::darkTitleBar(D.wnd);
    D.summary = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, D.wnd, nullptr, hi, nullptr);
    SendMessageW(D.summary, WM_SETFONT, (WPARAM)g.font, TRUE);
    setAccessibleName(D.summary, "ui.scopes.title");
    RECT cl;
    GetClientRect(D.wnd, &cl);
    MoveWindow(D.summary, S(14), cl.bottom - S(14) - S(22), cl.right - S(28), S(22), TRUE);
    D.last = std::chrono::steady_clock::time_point();
    if (g.lastBgraW > 0 && g.lastBgraH > 0 && !g.lastBgra.empty()) compute(g.lastBgra.data(), g.lastBgraW, g.lastBgraH);  // l'image deja affichee
    SetFocus(D.wnd);
}
