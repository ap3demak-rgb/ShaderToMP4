#include "app.h"
#include "core/util.h"

App g;

const wchar_t* statusText(int st) {
    switch (st) {
        case ST_RUNNING: return L"En cours...";
        case ST_DONE: return L"OK";
        case ST_FAILED: return L"Erreur";
        case ST_CANCELLED: return L"Annule";
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

void appendLog(const std::wstring& text) {
    std::wstring t;
    t.reserve(text.size() + 8);
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == L'\r') continue;
        if (text[i] == L'\n') t += L'\r';
        t += text[i];
    }
    t += L"\r\n";
    int len = GetWindowTextLengthW(g.log);
    SendMessageW(g.log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g.log, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
    SendMessageW(g.log, EM_SCROLLCARET, 0, 0);
}

void updateButtons() {
    const bool r = g.running.load();
    for (HWND c : {g.cmbRes, g.edW, g.edH, g.cmbFps, g.edDur, g.edStart, g.cmbCodec, g.cmbPreset, g.cmbContainer, g.cmbSS, g.edOut, g.bBrowse, g.bLoadP, g.bSaveP, g.trkCrf,
                   g.edBitrate, g.cmbColor, g.cmbBlur, g.edLoop, g.edAudio, g.bAudio, g.edFadeIn, g.edFadeOut, g.cmbEncoder})
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
    }
}

