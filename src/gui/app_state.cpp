// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "app.h"
#include "core/i18n.h"
#include "core/util.h"
#include "ui_controls.h"
#include "ui_theme.h"
#include "../../res/resource.h"

App g;

std::wstring statusText(int st) {
    switch (st) {
        case ST_RUNNING: return TX("status.running");
        case ST_DONE: return TX("status.done");
        case ST_FAILED: return TX("status.failed");
        case ST_CANCELLED: return TX("status.cancelled");
        default: return TX("status.pending");
    }
}

void setItemText(int i, int sub, const std::wstring& t) {
    LVITEMW it{};
    it.iSubItem = sub;
    it.pszText = const_cast<LPWSTR>(t.c_str());
    SendMessageW(g.list, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&it);
}

void setStatus(const std::wstring& t) { SetWindowTextW(g.status, t.c_str()); }

// Une ligne du journal contient-elle un des mots de la liste (séparés par « | ») fournie par le fichier de langue ?
static bool hasAnyWord(const std::wstring& line, const char* listKey) {
    const std::wstring list = TX(listKey);
    size_t pos = 0;
    while (pos <= list.size()) {
        size_t e = list.find(L'|', pos);
        if (e == std::wstring::npos) e = list.size();
        const std::wstring w = list.substr(pos, e - pos);
        if (!w.empty() && line.find(w) != std::wstring::npos) return true;
        pos = e + 1;
    }
    return false;
}

// Couleur d'une ligne du journal selon son contenu (listes de mots dans lang/*.json : log.words.*)
static COLORREF logColor(const std::wstring& s) {
    if (hasAnyWord(s, "log.words.error")) return theme::cref(theme::ERR);
    if (hasAnyWord(s, "log.words.warn")) return theme::cref(theme::WARN);
    if (hasAnyWord(s, "log.words.ok")) return theme::cref(theme::OK);
    if (s.rfind(L"[", 0) == 0 || s.rfind(APP_NAME_WSTR, 0) == 0) return theme::cref(theme::TEXT);
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

// Les shaders pas encore rendus (ni termines) sont notes pour la prochaine ouverture
void saveQueueNow() {
    std::vector<std::wstring> pending;
    for (size_t i = 0; i < g.paths.size() && i < g.state.size(); i++)
        if (g.state[i] != ST_DONE) pending.push_back(g.paths[i]);
    saveQueue(g.exeDir, pending);
}

// Reglages ecrits dans le shader selectionne (ou le premier) : ils priment sur la fenetre
void updateForced() {
    if (!g.lblForced) return;
    std::wstring text;
    if (!g.paths.empty()) {
        int sel = ListView_GetNextItem(g.list, -1, LVNI_SELECTED);
        if (sel < 0 || sel >= (int)g.paths.size()) sel = 0;
        const std::wstring o = shaderOverridesOf(g.paths[sel]);
        if (!o.empty()) text = TXF("ui.forced", o.c_str());
    }
    SetWindowTextW(g.lblForced, text.c_str());
}

void updateButtons() {
    const bool r = g.running.load();
    for (HWND c : {g.cmbRes, g.edW, g.edH, g.cmbFps, g.edDur, g.edStart, g.cmbCodec, g.cmbPreset, g.cmbContainer, g.cmbSS, g.edOut, g.bBrowse, g.bLoadP, g.bSaveP, g.trkCrf,
                   g.edBitrate, g.cmbColor, g.cmbBlur, g.edLoop, g.edAudio, g.bAudio, g.edFadeIn, g.edFadeOut, g.cmbEncoder, g.cmbDepth,
                   g.chips[0], g.chips[1], g.chips[2], g.chips[3], g.chips[4]})
        EnableWindow(c, !r);
    EnableWindow(g.bAdd, !r);
    EnableWindow(g.bRemove, !r);
    EnableWindow(g.bClear, !r);
    EnableWindow(g.bStart, !r && !g.paths.empty());
    EnableWindow(g.bSnap, !r && !g.paths.empty());
    EnableWindow(g.bCancel, r && !g.previewRun);
    EnableWindow(g.bPause, r && !g.previewRun);
    EnableWindow(g.bPrev, !r && !g.paths.empty());
    EnableWindow(g.bPlay, !r && !g.paths.empty());
    if (g.paths.empty() && g.player) stopPlayer();
    EnableWindow(g.bFolder, !g.paths.empty());
    if (!r) {
        g.pause = false;
        SetWindowTextW(g.bPause, TX("ui.btn.pause").c_str());
        ui::setButtonIcon(g.bPause, IC_PAUSE);
        SendMessageW(g.prog, ui::STPB_SETSTATE, 0, 0);
    }
    // Liste vide : on masque la liste et la fenetre dessine la zone de depot
    ShowWindow(g.list, g.paths.empty() ? SW_HIDE : SW_SHOW);
    updateForced();
    saveQueueNow();
    InvalidateRect(g.wnd, nullptr, FALSE);
}
