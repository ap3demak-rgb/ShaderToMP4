// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Fenêtre « À propos » : nom, version, copyright, licence, e-mail de contact et site web (liens cliquables).
#include "app.h"
#include "core/i18n.h"
#include "core/prolicense.h"
#include "core/updatecheck.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_render.h"
#include "ui_theme.h"
#include "../../res/resource.h"

using namespace theme;

namespace {

enum { ID_ABOUT_LICENSE = 701, ID_ABOUT_NOTICES, ID_ABOUT_BUY, ID_ABOUT_UPDATE, ID_ABOUT_CLOSE = IDCANCEL };
constexpr UINT WM_UPDATE_DONE = WM_APP + 40;  // lParam = UpdateMsg* (a liberer), wParam : 0 a jour, 1 plus recente, 2 erreur, 3 telechargee

struct Link {
    RECT r;
    std::wstring target;  // mailto:... ou https://...
};

struct UpdateMsg {
    std::wstring text;
    std::shared_ptr<UpdateInfo> info;  // version plus recente trouvee, sinon vide
};

struct Dialog {
    HWND wnd = nullptr, bLicense = nullptr, bNotices = nullptr, bClose = nullptr, bBuy = nullptr, bUpdate = nullptr;
    std::wstring updText;         // resultat de la derniere verification, affiche sous l'edition
    std::shared_ptr<UpdateInfo> upd;  // version plus recente trouvee (le bouton devient « Telecharger »)
    bool busy = false;
    UINT dpi = 96;
    std::vector<Link> links;
    int hover = -1;
    bool done = false;
} D;

int S(int v) { return MulDiv(v, (int)D.dpi, 96); }
float F(int v) { return (float)v * D.dpi / 96.0f; }

const wchar_t* kClass = L"ShaderToMP4About";
const wchar_t* kLicenseUrl = L"https://www.gnu.org/licenses/gpl-3.0.html";

// Ouvre un fichier texte situé dans le dossier de l'application ; sinon, l'adresse de secours.
void openTextFile(const std::wstring& name, const wchar_t* fallbackUrl) {
    const std::wstring path = g.exeDir + name;
    if (fileExists(path)) ShellExecuteW(D.wnd, L"open", L"notepad.exe", (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    else ShellExecuteW(D.wnd, L"open", fallbackUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

void layoutButtons() {
    RECT rc;
    GetClientRect(D.wnd, &rc);
    const int bw[3] = {130, 190, 110}, bh = S(34), gap = S(10);
    int total = S(bw[0]) + S(bw[1]) + S(bw[2]) + 2 * gap;
    int x = (rc.right - total) / 2, y = rc.bottom - S(24) - bh;
    HWND b[3] = {D.bLicense, D.bNotices, D.bClose};
    for (int i = 0; i < 3; i++) {
        MoveWindow(b[i], x, y, S(bw[i]), bh, TRUE);
        x += S(bw[i]) + gap;
    }
    {  // au-dessus de la rangee : « Verifier les mises a jour » et, en version d'essai, « Acheter Pro »
        const int updW = S(210), buyW = S(160);
        const int total2 = updW + (D.bBuy ? gap + buyW : 0);
        int x2 = (rc.right - total2) / 2;
        if (D.bBuy) {
            MoveWindow(D.bBuy, x2, y - gap - bh, buyW, bh, TRUE);
            x2 += buyW + gap;
        }
        MoveWindow(D.bUpdate, x2, y - gap - bh, updW, bh, TRUE);
    }
}

void paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(D.wnd, &ps);
    RECT rc;
    GetClientRect(D.wnd, &rc);
    const float w = (float)rc.right;
    ui::Frame f(rc.right, rc.bottom, ui::rgb(BG));
    D.links.clear();

    // Carte centrale
    D2D1_RECT_F card = ui::rectf(F(16), F(16), w - F(16), (float)rc.bottom - F(16));
    f.fillRound(card, F(16), ui::rgb(CARD));
    f.strokeRound(card, F(16), ui::rgb(CARD_BORDER), 1.0f);

    // Logo, nom, version
    float cy = F(34);
    f.icon(IC_LOGO, ui::rectf(w / 2 - F(36), cy, w / 2 + F(36), cy + F(72)), false, ui::rgb(0));
    cy += F(80);
    f.text(APP_NAME_WSTR, ui::rectf(0, cy, w, cy + F(34)), F(26), true, ui::rgb(TEXT), ui::Center);
    cy += F(34);
    f.text(TXF("about.version", APP_VER_WSTR), ui::rectf(0, cy, w, cy + F(22)), F(13), false, ui::rgb(MUTED), ui::Center);
    cy += F(26);
    f.text(TX("ui.tagline"), ui::rectf(F(30), cy, w - F(30), cy + F(24)), F(13), false, ui::rgb(MUTED), ui::Center);
    cy += F(40);

    // Séparateur
    f.fillRound(ui::rectf(F(40), cy, w - F(40), cy + 1.0f), 0.5f, ui::rgb(CARD_BORDER));
    cy += F(18);

    // Lignes d'informations : intitulé à gauche, valeur (éventuellement lien) à droite
    auto row = [&](const std::wstring& label, const std::wstring& value, const std::wstring& target) {
        const float lx = F(48), vx = F(150);
        f.text(label, ui::rectf(lx, cy, vx, cy + F(26)), F(13), false, ui::rgb(MUTED));
        D2D1_RECT_F vr = ui::rectf(vx, cy, w - F(40), cy + F(26));
        if (target.empty()) {
            f.text(value, vr, F(14), false, ui::rgb(TEXT));
        } else {
            const bool hot = (int)D.links.size() == D.hover;
            f.text(value, vr, F(14), false, ui::rgb(hot ? 0xFFFFFF : ACCENT));
            const float tw = ui::measureText(value, F(14), false);
            f.fillRound(ui::rectf(vx, cy + F(21), vx + tw, cy + F(22.2f)), 0.5f, ui::rgb(hot ? 0xFFFFFF : ACCENT, 0.8f));
            D.links.push_back({{(LONG)vx, (LONG)cy, (LONG)(vx + tw), (LONG)(cy + F(26))}, target});
        }
        cy += F(30);
    };
    row(TX("about.copyright"), L"© " APP_COPYRIGHT_YEAR_W L" " APP_COPYRIGHT_OWNER_WSTR, L"");
    row(TX("about.licence_label"), TX("about.license"), L"");
    row(TX("about.contact"), APP_CONTACT_EMAIL_WSTR, L"mailto:" APP_CONTACT_EMAIL_WSTR);
    row(TX("about.website"), APP_WEBSITE_URL_WSTR, APP_WEBSITE_URL_WSTR);
    {  // édition : version enregistrée (vert) ou d'essai (orange, avec le fichier attendu) ; rien d'autre n'apparaît quand la licence est valide
        const ProLicense li = currentLicense();
        const bool registered = li.state == ProLicenseState::Valid;
        f.text(TX("about.edition"), ui::rectf(F(48), cy, F(150), cy + F(26)), F(13), false, ui::rgb(MUTED));
        f.text(licenseStatusText(li), ui::rectf(F(150), cy, w - F(40), cy + F(44)), F(13), false, ui::rgb(registered ? OK : WARN), ui::Left, false, true);
        cy += F(46);
        if (!registered) {
            f.text(TXF("lic.info.file", (g.exeDir + licenseFileName()).c_str()), ui::rectf(F(48), cy, w - F(48), cy + F(36)), F(11.5f), false, ui::rgb(FAINT), ui::Left, false, true);
            cy += F(40);
        }
    }
    cy += F(8);

    // Avis GPL (retour à la ligne automatique)
    f.text(TX("about.warranty"), ui::rectf(F(48), cy, w - F(48), cy + F(80)), F(11.5f), false, ui::rgb(FAINT), ui::Left, false, true);
    cy += F(84);
    if (!D.updText.empty()) f.text(D.updText, ui::rectf(F(48), cy, w - F(48), cy + F(60)), F(12.5f), false, ui::rgb(D.upd ? ACCENT : MUTED), ui::Left, false, true);
    f.present(hdc);
    EndPaint(D.wnd, &ps);
}

int hitLink(POINT p) {
    for (size_t i = 0; i < D.links.size(); i++)
        if (PtInRect(&D.links[i].r, p)) return (int)i;
    return -1;
}

// Verification (premier clic) puis telechargement controle (second clic), sur un thread : uniquement sur demande de l'utilisateur
void startUpdate(HWND h) {
    if (D.busy) return;
    D.busy = true;
    EnableWindow(D.bUpdate, FALSE);
    D.updText = TX("update.checking");
    InvalidateRect(h, nullptr, FALSE);
    const std::shared_ptr<UpdateInfo> known = D.upd;
    const std::wstring exeDir = g.exeDir;
    std::thread([h, known, exeDir]() {
        auto post = [&](int state, const std::wstring& text, std::shared_ptr<UpdateInfo> keep) {
            if (!IsWindow(h)) return;
            PostMessageW(h, WM_UPDATE_DONE, (WPARAM)state, (LPARAM) new UpdateMsg{text, keep});
        };
        if (!known) {
            auto info = std::make_shared<UpdateInfo>(checkForUpdate(defaultUpdateUrl()));
            if (!info->ok) post(2, info->err, nullptr);
            else if (!info->newer) post(0, TXF("update.uptodate", W(info->current).c_str()), nullptr);
            else post(1, TXF("update.available", W(info->latest).c_str(), W(info->current).c_str()) + (info->assetName.empty() ? L"" : L"\n" + TXF("update.file", info->assetName.c_str())), info);
            return;
        }
        std::wstring saved, err;
        const bool ok = downloadUpdate(*known, exeDir + L"updates", [h, name = known->assetName](unsigned long long got, unsigned long long total) {
            if (!IsWindow(h)) return false;
            return true;
        }, saved, err);
        post(ok ? 3 : 2, ok ? TXF("update.saved", saved.c_str()) : err, ok ? nullptr : known);
    }).detach();
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
        case WM_MOUSEMOVE: {
            int hv = hitLink({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (hv != D.hover) {
                D.hover = hv;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && D.hover >= 0) {
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));  // main
                return TRUE;
            }
            break;
        case WM_LBUTTONUP: {
            int hv = hitLink({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (hv >= 0) ShellExecuteW(h, L"open", D.links[hv].target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_ABOUT_LICENSE: openTextFile(L"COPYING", kLicenseUrl); break;
                case ID_ABOUT_NOTICES:  // notices tierces dans la langue de l'interface (fichier .fr.md pour le français)
                    openTextFile(i18n::language() == "fr" ? L"THIRD_PARTY_NOTICES.fr.md" : L"THIRD_PARTY_NOTICES.md",
                                 APP_WEBSITE_URL_WSTR);
                    break;
                case ID_ABOUT_BUY: showBuy(h); break;
                case ID_ABOUT_UPDATE: startUpdate(h); break;
                case ID_ABOUT_CLOSE: DestroyWindow(h); break;
            }
            return 0;
        case WM_UPDATE_DONE: {
            std::unique_ptr<UpdateMsg> msg((UpdateMsg*)lp);
            D.busy = false;
            D.updText = msg->text;
            D.upd = msg->info;
            if (wp == 1) SetWindowTextW(D.bUpdate, TX("update.btn.download").c_str());
            else if (wp != 2) {  // a jour ou telechargee : on repart de « Verifier »
                SetWindowTextW(D.bUpdate, TX("update.btn.check").c_str());
            }
            EnableWindow(D.bUpdate, TRUE);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: D.done = true; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void showAbout(HWND parent) {
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
    RECT want{0, 0, S(540), S(showTrialUi() ? 760 : 690)};
    const DWORD style = WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&want, style, FALSE, WS_EX_DLGMODALFRAME, D.dpi);
    RECT pr;
    GetWindowRect(parent, &pr);
    const int ww = want.right - want.left, wh = want.bottom - want.top;
    const int x = pr.left + ((pr.right - pr.left) - ww) / 2, y = pr.top + ((pr.bottom - pr.top) - wh) / 2;
    const std::wstring title = TXF("about.title", APP_NAME_WSTR);
    D.wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, title.c_str(), style | WS_VISIBLE | WS_CLIPCHILDREN, x, y, ww, wh, parent,
                            nullptr, hi, nullptr);
    if (!D.wnd) return;
    ui::darkTitleBar(D.wnd);
    auto button = [&](const char* key, int id, int icon, ui::BtnStyle st) {
        HWND b = CreateWindowExW(0, L"BUTTON", TX(key).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, D.wnd,
                                 (HMENU)(INT_PTR)id, hi, nullptr);
        ui::setupButton(b, icon, st, CARD);
        SendMessageW(b, WM_SETFONT, (WPARAM)g.font, TRUE);
        return b;
    };
    D.bLicense = button("about.btn.license", ID_ABOUT_LICENSE, IC_SAVE, ui::BtnStyle::Secondary);
    D.bNotices = button("about.btn.notices", ID_ABOUT_NOTICES, IC_FILM, ui::BtnStyle::Secondary);
    D.bClose = button("about.btn.close", ID_ABOUT_CLOSE, IC_CHECK, ui::BtnStyle::Primary);
    if (showTrialUi()) D.bBuy = button("about.btn.buy", ID_ABOUT_BUY, IC_ZAP, ui::BtnStyle::Secondary);  // absent avec une licence valide
    D.bUpdate = button("update.btn.check", ID_ABOUT_UPDATE, IC_DOWNLOAD, ui::BtnStyle::Secondary);
    layoutButtons();

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
