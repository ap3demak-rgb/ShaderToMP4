// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include <dwmapi.h>
#include <uxtheme.h>

#ifdef _MSC_VER
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#endif

namespace ui {
namespace {

using namespace theme;

float px(int dip, UINT dpi) { return (float)dip * dpi / 96.0f; }

D2D1_COLOR_F mix(unsigned a, unsigned b, float t) {
    auto ch = [&](int sh) { return (((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t) / 255.0f; };
    return D2D1::ColorF(ch(16), ch(8), ch(0), 1.0f);
}

// ---------------------------------------------------------------------------
//  Boutons
// ---------------------------------------------------------------------------
struct BtnSpec {
    int icon = 0;
    BtnStyle style = BtnStyle::Secondary;
    unsigned bg = CARD;
    bool hot = false, tracking = false;
    float amt = 0;       // intensite du survol 0..1, animee (fondu de 120 ms)
    ULONGLONG last = 0;  // horodatage de la derniere etape d'animation
};
std::map<HWND, BtnSpec> g_buttons;
constexpr UINT_PTR kFadeTimer = 77;
constexpr float kFadeMs = 120.0f;

void startFade(HWND h, BtnSpec& s) {
    s.last = GetTickCount64();
    SetTimer(h, kFadeTimer, 15, nullptr);
}

LRESULT CALLBACK btnProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    BtnSpec& s = g_buttons[h];
    switch (msg) {
        case WM_MOUSEMOVE:
            if (!s.tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                s.tracking = true;
            }
            if (!s.hot) {
                s.hot = true;
                startFade(h, s);
            }
            break;
        case WM_MOUSELEAVE:
            s.tracking = false;
            s.hot = false;
            startFade(h, s);
            break;
        case WM_ENABLE:
            s.hot = false;
            s.amt = 0;
            InvalidateRect(h, nullptr, FALSE);
            break;
        case WM_TIMER:
            if (wp == kFadeTimer) {
                ULONGLONG now = GetTickCount64();
                float step = (float)(now - s.last) / kFadeMs;
                s.last = now;
                float target = s.hot && IsWindowEnabled(h) ? 1.0f : 0.0f;
                if (s.amt < target) s.amt = std::min(target, s.amt + step);
                else s.amt = std::max(target, s.amt - step);
                InvalidateRect(h, nullptr, FALSE);
                if (s.amt == target) KillTimer(h, kFadeTimer);
                return 0;
            }
            break;
        case WM_NCDESTROY:
            KillTimer(h, kFadeTimer);
            g_buttons.erase(h);
            RemoveWindowSubclass(h, btnProc, 1);
            break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//  Curseur "STSlider"
// ---------------------------------------------------------------------------
struct Slider {
    int min = 0, max = 100, pos = 0;
    bool drag = false, hot = false, tracking = false;
    std::vector<float> marks;  // reperes sur la piste, positions dans 0..1
};

void sliderNotify(HWND h, int code) {
    SendMessageW(GetParent(h), WM_HSCROLL, MAKEWPARAM(code, 0), (LPARAM)h);
}

int sliderFromX(HWND h, Slider* s, int x) {
    UINT dpi = theme::dpiFor(h);
    RECT rc;
    GetClientRect(h, &rc);
    float pad = px(10, dpi);
    float t = ((float)x - pad) / std::max(1.0f, (float)rc.right - 2 * pad);
    t = std::min(1.0f, std::max(0.0f, t));
    return s->min + (int)std::lround(t * (s->max - s->min));
}

void sliderPaint(HWND h, Slider* s) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    UINT dpi = theme::dpiFor(h);
    Frame f(rc.right, rc.bottom, rgb(CARD));
    float w = (float)rc.right, mid = rc.bottom / 2.0f;
    float pad = px(10, dpi), trackH = px(4, dpi);
    float x0 = pad, x1 = w - pad;
    float t = s->max > s->min ? (float)(s->pos - s->min) / (s->max - s->min) : 0.0f;
    float xt = x0 + t * (x1 - x0);
    bool enabled = IsWindowEnabled(h) != 0;
    f.fillRound(rectf(x0, mid - trackH / 2, x1, mid + trackH / 2), trackH / 2, rgb(FIELD_BORDER));
    if (xt > x0)
        f.fillRoundGradient(rectf(x0, mid - trackH / 2, xt, mid + trackH / 2), trackH / 2,
                            enabled ? rgb(ACCENT_A) : rgb(FAINT), enabled ? rgb(ACCENT_B) : rgb(FAINT));
    for (float m : s->marks) {  // images cles : un trait fin de part et d'autre de la piste
        const float mx = x0 + std::min(1.0f, std::max(0.0f, m)) * (x1 - x0);
        f.fillRound(rectf(mx - px(1, dpi), mid - px(8, dpi), mx + px(1, dpi), mid + px(8, dpi)), px(1, dpi), rgb(ACCENT));
    }
    float r = px(s->drag || s->hot ? 8 : 7, dpi);
    if (enabled) {
        f.fillCircle(xt, mid, r + px(2, dpi), rgb(ACCENT, 0.25f));
        f.fillCircle(xt, mid, r, rgb(0xFFFFFF));
    } else {
        f.fillCircle(xt, mid, r, rgb(FAINT));
    }
    if (GetFocus() == h) f.strokeRound(rectf(1, 1, w - 1, (float)rc.bottom - 1), px(6, dpi), rgb(ACCENT, 0.7f), 1.5f);
    f.present(hdc);
    EndPaint(h, &ps);
}

LRESULT CALLBACK sliderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Slider* s = (Slider*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
        case WM_NCCREATE:
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR) new Slider());
            return TRUE;
        case WM_NCDESTROY:
            delete s;
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: sliderPaint(h, s); return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS;
        case TBM_SETRANGE:
            s->min = (short)LOWORD(lp);
            s->max = (short)HIWORD(lp);
            s->pos = std::min(s->max, std::max(s->min, s->pos));
            if (wp) InvalidateRect(h, nullptr, FALSE);
            return 0;
        case TBM_SETPOS:
            s->pos = std::min(s->max, std::max(s->min, (int)lp));
            if (wp) InvalidateRect(h, nullptr, FALSE);
            return 0;
        case TBM_GETPOS: return s->pos;
        case STSL_SETMARKS:
            s->marks.assign((const float*)lp, (const float*)lp + (lp ? wp : 0));
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_ENABLE: InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS: InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN:
            SetFocus(h);
            SetCapture(h);
            s->drag = true;
            s->pos = sliderFromX(h, s, (short)LOWORD(lp));
            InvalidateRect(h, nullptr, FALSE);
            sliderNotify(h, TB_THUMBTRACK);
            return 0;
        case WM_MOUSEMOVE:
            if (!s->tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                s->tracking = true;
            }
            if (!s->hot) {
                s->hot = true;
                InvalidateRect(h, nullptr, FALSE);
            }
            if (s->drag) {
                int p = sliderFromX(h, s, (short)LOWORD(lp));
                if (p != s->pos) {
                    s->pos = p;
                    InvalidateRect(h, nullptr, FALSE);
                    sliderNotify(h, TB_THUMBTRACK);
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            s->tracking = false;
            s->hot = false;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            if (s->drag) {
                s->drag = false;
                ReleaseCapture();
                InvalidateRect(h, nullptr, FALSE);
                sliderNotify(h, TB_ENDTRACK);
            }
            return 0;
        case WM_KEYDOWN: {
            int p = s->pos, span = std::max(1, s->max - s->min);
            switch (wp) {
                case VK_LEFT:
                case VK_DOWN: p -= 1; break;
                case VK_RIGHT:
                case VK_UP: p += 1; break;
                case VK_PRIOR: p += std::max(1, span / 10); break;
                case VK_NEXT: p -= std::max(1, span / 10); break;
                case VK_HOME: p = s->min; break;
                case VK_END: p = s->max; break;
                default: return DefWindowProcW(h, msg, wp, lp);
            }
            p = std::min(s->max, std::max(s->min, p));
            if (p != s->pos) {
                s->pos = p;
                InvalidateRect(h, nullptr, FALSE);
                sliderNotify(h, TB_THUMBTRACK);
                sliderNotify(h, TB_ENDTRACK);
            }
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//  Barre de progression "STProgress"
// ---------------------------------------------------------------------------
struct Progress {
    int min = 0, max = 100, pos = 0, state = 0;
};

LRESULT CALLBACK progressProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Progress* p = (Progress*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
        case WM_NCCREATE:
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR) new Progress());
            return TRUE;
        case WM_NCDESTROY:
            delete p;
            return 0;
        case WM_ERASEBKGND: return 1;
        case PBM_SETRANGE32:
            p->min = (int)wp;
            p->max = (int)lp;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case PBM_SETPOS: {
            int old = p->pos;
            p->pos = std::min(p->max, std::max(p->min, (int)wp));
            InvalidateRect(h, nullptr, FALSE);
            return old;
        }
        case PBM_GETPOS: return p->pos;
        case STPB_SETSTATE:
            p->state = (int)wp;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            UINT dpi = theme::dpiFor(h);
            Frame f(rc.right, rc.bottom, rgb(CARD));
            float rad = rc.bottom / 2.0f;
            f.fillRound(rectf(0, 0, (float)rc.right, (float)rc.bottom), rad, rgb(FIELD));
            float t = p->max > p->min ? (float)(p->pos - p->min) / (p->max - p->min) : 0.0f;
            float xw = std::max(t > 0 ? (float)rc.bottom : 0.0f, t * rc.right);
            if (xw > 0) {
                unsigned a = ACCENT_A, b = ACCENT_B;
                if (p->state == 1) a = b = WARN;
                if (p->state == 2) a = b = ERR;
                f.fillRoundGradient(rectf(0, 0, xw, (float)rc.bottom), rad, rgb(a), rgb(b));
            }
            (void)dpi;
            f.present(hdc);
            EndPaint(h, &ps);
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

bool registerControls(HINSTANCE hi) {
    WNDCLASSW wc{};
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpfnWndProc = sliderProc;
    wc.lpszClassName = L"STSlider";
    bool ok = RegisterClassW(&wc) != 0;
    wc.lpfnWndProc = progressProc;
    wc.lpszClassName = L"STProgress";
    ok = RegisterClassW(&wc) != 0 && ok;
    return ok;
}

void setupButton(HWND btn, int iconId, BtnStyle style, unsigned bgColor) {
    BtnSpec& s = g_buttons[btn];
    s.icon = iconId;
    s.style = style;
    s.bg = bgColor;
    SetWindowSubclass(btn, btnProc, 1, 0);
}

void drawButton(const DRAWITEMSTRUCT* d, UINT dpi) {
    HWND h = d->hwndItem;
    auto it = g_buttons.find(h);
    if (it == g_buttons.end()) return;
    const BtnSpec& s = it->second;
    RECT rc = d->rcItem;
    int w = rc.right - rc.left, hgt = rc.bottom - rc.top;
    const bool disabled = (d->itemState & ODS_DISABLED) != 0;
    const bool down = (d->itemState & ODS_SELECTED) != 0 && !disabled;
    const float a_ = disabled ? 0.0f : s.amt;  // survol anime
    const bool hot = a_ > 0.01f;
    const bool focus = (d->itemState & ODS_FOCUS) != 0 && !(d->itemState & ODS_NOFOCUSRECT);

    wchar_t buf[128] = {0};
    GetWindowTextW(h, buf, 128);
    std::wstring label = buf;

    Frame f(w, hgt, rgb(s.bg));
    D2D1_RECT_F r = rectf(0, 0, (float)w, (float)hgt);
    float rad = s.style == BtnStyle::Chip ? hgt / 2.0f : px(8, dpi);
    D2D1_COLOR_F textCol = rgb(TEXT);
    float iconAlpha = 1.0f;
    bool bold = false;

    switch (s.style) {
        case BtnStyle::Primary: {
            D2D1_COLOR_F a = rgb(ACCENT_A), b = rgb(ACCENT_B);
            if (disabled) {
                a = rgb(FIELD);
                b = rgb(FIELD);
                textCol = rgb(FAINT);
                iconAlpha = 0.5f;
            } else if (down) {
                a = mix(ACCENT_A, 0x000000, 0.25f);
                b = mix(ACCENT_B, 0x000000, 0.25f);
            } else if (hot) {
                a = mix(ACCENT_A, 0xFFFFFF, 0.14f * a_);
                b = mix(ACCENT_B, 0xFFFFFF, 0.14f * a_);
            }
            f.fillRoundGradient(r, rad, a, b);
            if (!disabled) textCol = rgb(0xFFFFFF);
            bold = true;
            break;
        }
        case BtnStyle::Danger: {
            unsigned c = disabled ? FAINT : ERR;
            if (hot || down) f.fillRound(r, rad, rgb(ERR, down ? 0.28f : 0.16f * a_));
            f.strokeRound(r, rad, rgb(c, disabled ? 0.4f : 0.8f), 1.0f);
            textCol = rgb(c);
            iconAlpha = disabled ? 0.5f : 1.0f;
            break;
        }
        case BtnStyle::Ghost: {
            if (hot || down) f.fillRound(r, rad, down ? rgb(FIELD_DOWN) : mix(s.bg, FIELD_HOT, a_));
            if (disabled) textCol = rgb(FAINT);
            break;
        }
        case BtnStyle::Chip:
        case BtnStyle::Secondary:
        default: {
            D2D1_COLOR_F fill = disabled || down ? rgb(FIELD_DOWN) : mix(FIELD, FIELD_HOT, a_);
            f.fillRound(r, rad, fill);
            f.strokeRound(r, rad, mix(FIELD_BORDER, BORDER_HOT, a_), 1.0f);
            if (disabled) {
                textCol = rgb(FAINT);
                iconAlpha = 0.5f;
            }
            break;
        }
    }
    if (focus) f.strokeRound(rectf(1, 1, (float)w - 1, (float)hgt - 1), rad, rgb(ACCENT, 0.9f), 1.5f);

    // Icone + texte centres
    float fs = px(13, dpi);
    float isz = s.icon ? px(s.style == BtnStyle::Chip ? 14 : 16, dpi) : 0;
    float gap = s.icon && !label.empty() ? px(7, dpi) : 0;
    float tw = label.empty() ? 0 : measureText(label, fs, bold);
    float total = isz + gap + tw;
    float x = std::max(px(6, dpi), ((float)w - total) / 2.0f);
    if (s.icon) {
        D2D1_COLOR_F ic = textCol;
        ic.a = iconAlpha;
        f.icon(s.icon, rectf(x, (hgt - isz) / 2, x + isz, (hgt + isz) / 2), true, ic);
    }
    if (!label.empty())
        f.text(label, rectf(x + isz + gap, 0, (float)w - px(4, dpi), (float)hgt), fs, bold, textCol, Left);
    f.present(d->hDC, rc.left, rc.top);
}

void darkTheme(HWND h, const wchar_t* theme) { SetWindowTheme(h, theme, nullptr); }

void darkTitleBar(HWND wnd) {
    BOOL on = TRUE;
    if (FAILED(DwmSetWindowAttribute(wnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &on, sizeof(on))))
        DwmSetWindowAttribute(wnd, 19, &on, sizeof(on));
    COLORREF cap = cref(BG), txt = cref(TEXT), brd = cref(CARD_BORDER);  // Windows 11 : couleurs du cadre
    DwmSetWindowAttribute(wnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
    DwmSetWindowAttribute(wnd, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof(txt));
    DwmSetWindowAttribute(wnd, 34 /*DWMWA_BORDER_COLOR*/, &brd, sizeof(brd));
}

}  // namespace ui

namespace ui {
void setButtonIcon(HWND btn, int iconId) {
    auto it = g_buttons.find(btn);
    if (it == g_buttons.end()) return;
    it->second.icon = iconId;
    InvalidateRect(btn, nullptr, FALSE);
}
}  // namespace ui
