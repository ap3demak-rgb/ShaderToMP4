#include "app.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_theme.h"
#include "../../res/resource.h"

App g;

const wchar_t* statusText(int st) {
    switch (st) {
        case ST_RUNNING: return L"En cours...";
        case ST_DONE: return L"Terminé";
        case ST_FAILED: return L"Erreur";
        case ST_CANCELLED: return L"Annulé";
        default: return L"En attente";
    }
}

void setItemText(int i, int sub, const std::wstring& t) {
    LVITEMW it{};
    it.iSubItem = sub;
    it.pszText = const_cast<LPWSTR>(t.c_str());
    SendMessageW(g.list, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&it);
}

void setStatus(const std::wstring& t) { SetWindowTextW(g.status, t.c_str()); }

// Couleur d'une ligne du journal selon son contenu
static COLORREF logColor(const std::wstring& s) {
    auto has = [&](const wchar_t* k) { return s.find(k) != std::wstring::npos; };
    if (has(L"ERREUR") || has(L"ECHEC") || has(L"echoue")) return theme::cref(theme::ERR);
    if (has(L"ATTENTION") || has(L"indisponible") || has(L"Annul") || has(L"Interrompu") || has(L"prioritaires") ||
        has(L"insuffisant"))
        return theme::cref(theme::WARN);
    if (has(L"OK ->") || has(L"installé") || has(L"Tout est terminé")) return theme::cref(theme::OK);
    if (s.rfind(L"[", 0) == 0 || s.rfind(L"ShaderToMP4", 0) == 0) return theme::cref(theme::TEXT);
    return theme::cref(theme::MUTED);
}

void appendLog(const std::wstring& text) {
    std::wstring t;
    t.reserve(text.size() + 8);
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == L'\r') continue;
        if (text[i] == L'\n') t += L'\r';
        t += text[i];
    }
    t += L"\r";
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = logColor(text);
    SendMessageW(g.log, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(g.log, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(g.log, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
    SendMessageW(g.log, WM_VSCROLL, SB_BOTTOM, 0);
}

void updateButtons() {
    const bool r = g.running.load();
    for (HWND c : {g.cmbRes, g.edW, g.edH, g.cmbFps, g.edDur, g.edStart, g.cmbCodec, g.cmbPreset, g.cmbContainer, g.cmbSS, g.edOut, g.bBrowse, g.bLoadP, g.bSaveP, g.trkCrf,
                   g.edBitrate, g.cmbColor, g.cmbBlur, g.edLoop, g.edAudio, g.bAudio, g.edFadeIn, g.edFadeOut, g.cmbEncoder,
                   g.chips[0], g.chips[1], g.chips[2], g.chips[3]})
        EnableWindow(c, !r);
    EnableWindow(g.bAdd, !r);
    EnableWindow(g.bRemove, !r);
    EnableWindow(g.bClear, !r);
    EnableWindow(g.bStart, !r && !g.paths.empty());
    EnableWindow(g.bSnap, !r && !g.paths.empty());
    EnableWindow(g.bCancel, r && !g.previewRun);
    EnableWindow(g.bPause, r && !g.previewRun);
    EnableWindow(g.bPrev, !r && !g.paths.empty());
    EnableWindow(g.bFolder, !g.paths.empty());
    if (!r) {
        g.pause = false;
        SetWindowTextW(g.bPause, L"Pause");
        ui::setButtonIcon(g.bPause, IC_PAUSE);
        SendMessageW(g.prog, ui::STPB_SETSTATE, 0, 0);
    }
    // Liste vide : on masque la liste et la fenetre dessine la zone de depot
    ShowWindow(g.list, g.paths.empty() ? SW_HIDE : SW_SHOW);
    InvalidateRect(g.wnd, nullptr, FALSE);
}
