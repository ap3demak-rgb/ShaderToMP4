// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "app.h"
#include "core/i18n.h"
#include "core/util.h"
#include "platform/platform.h"

// --- Panneau de reglages video ---
const ResPreset kRes[] = {
    {L"720p", nullptr, 1280, 720},
    {L"1080p", nullptr, 1920, 1080},
    {L"1440p", nullptr, 2560, 1440},
    {L"4K", nullptr, 3840, 2160},
    {nullptr, "ui.res.vertical", 1080, 1920},
    {nullptr, "ui.res.square", 1080, 1080},
};
const int kResCount = (int)(sizeof(kRes) / sizeof(kRes[0]));  // l'entree suivante = "Personnalise"
const wchar_t* kPresets[] = {L"ultrafast", L"superfast", L"veryfast", L"faster", L"fast",
                                    L"medium",    L"slow",      L"slower",   L"veryslow"};

std::wstring resName(const ResPreset& r) {
    std::wstring s = wfmt(L"%dx%d", r.w, r.h);
    if (r.tag) s += L" (" + std::wstring(r.tag) + L")";
    else if (r.key) s += L" (" + TX(r.key) + L")";
    return s;
}

std::wstring getText(HWND h) {
    wchar_t b[128] = {0};
    GetWindowTextW(h, b, 128);
    return b;
}

bool parseSeconds(std::wstring t, double& out) {
    for (auto& c : t)
        if (c == L',') c = L'.';
    double acc = 0;
    int parts = 0;
    size_t pos = 0;
    for (;;) {
        size_t e = t.find(L':', pos);
        std::wstring p = t.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos);
        if (p.empty() || ++parts > 3) return false;
        wchar_t* end = nullptr;
        double v = wcstod(p.c_str(), &end);
        if (end == p.c_str() || *end != 0 || v < 0) return false;
        acc = acc * 60 + v;
        if (e == std::wstring::npos) break;
        pos = e + 1;
    }
    out = acc;
    return true;
}

void syncResCombo() {
    int w = _wtoi(getText(g.edW).c_str()), h = _wtoi(getText(g.edH).c_str()), idx = kResCount;
    for (int i = 0; i < kResCount; i++)
        if (kRes[i].w == w && kRes[i].h == h) { idx = i; break; }
    SendMessageW(g.cmbRes, CB_SETCURSEL, (WPARAM)idx, 0);
}

void updateCrfLabel() {
    int c = (int)SendMessageW(g.trkCrf, TBM_GETPOS, 0, 0);
    const char* hint = c == 0 ? "ui.crf.lossless" : c <= 12 ? "ui.crf.near_perfect" : c <= 18 ? "ui.crf.very_good"
                       : c <= 23 ? "ui.crf.good" : "ui.crf.reduced";
    SetWindowTextW(g.lblCrf, wfmt(L"%d  -  %ls", c, TX(hint).c_str()).c_str());
}

void settingsToUI(const Settings& s) {
    g.base.post = s.post;  // post-traitement : reglé dans la fenêtre « Paramètres », pas dans ce panneau
    g.base.lut = s.lut;
    SetWindowTextW(g.edW, std::to_wstring(s.width).c_str());
    SetWindowTextW(g.edH, std::to_wstring(s.height).c_str());
    SetWindowTextW(g.cmbFps, i18n::num(s.fps).c_str());
    SetWindowTextW(g.edDur, i18n::num(s.duration).c_str());
    SetWindowTextW(g.edStart, i18n::num(s.start).c_str());
    SendMessageW(g.trkCrf, TBM_SETPOS, TRUE, std::min(35, std::max(0, s.crf)));
    int p = (int)SendMessageW(g.cmbPreset, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)W(s.preset).c_str());
    SendMessageW(g.cmbPreset, CB_SETCURSEL, (WPARAM)(p < 0 ? 6 : p), 0);
    SendMessageW(g.cmbCodec, CB_SETCURSEL, codecIndex(s.codec), 0);
    SendMessageW(g.cmbContainer, CB_SETCURSEL, containerIndex(s.container), 0);
    SendMessageW(g.cmbDepth, CB_SETCURSEL, s.depth >= 10 ? 1 : 0, 0);
    SendMessageW(g.cmbSS, CB_SETCURSEL, (WPARAM)(std::min(4, std::max(1, s.ss)) - 1), 0);
    SetWindowTextW(g.edOut, s.outdir.c_str());
    SetWindowTextW(g.edBitrate, std::to_wstring(s.bitrate).c_str());
    SendMessageW(g.cmbColor, CB_SETCURSEL, s.color == "bt709full" ? 1 : s.color == "srgb" ? 2 : s.color == "bt601" ? 3 : 0, 0);
    SendMessageW(g.cmbBlur, CB_SETCURSEL, s.blur >= 16 ? 3 : s.blur >= 8 ? 2 : s.blur >= 2 ? 1 : 0, 0);
    SetWindowTextW(g.edLoop, i18n::num(s.loop).c_str());
    SetWindowTextW(g.edAudio, s.audio.c_str());
    SetWindowTextW(g.edFadeIn, i18n::num(s.fadein).c_str());
    SetWindowTextW(g.edFadeOut, i18n::num(s.fadeout).c_str());
    SendMessageW(g.cmbEncoder, CB_SETCURSEL, s.encoder == "auto" ? 1 : s.encoder == "nvenc" ? 2 : s.encoder == "amf" ? 3 : s.encoder == "qsv" ? 4 : 0, 0);
    syncResCombo();
    updateCrfLabel();
}

bool uiToSettings(Settings& s, std::wstring& err) {
    int w = _wtoi(getText(g.edW).c_str()), h = _wtoi(getText(g.edH).c_str());
    if (w < 16 || w > 16384 || h < 16 || h > 16384) { err = TX("error.res_invalid"); return false; }
    std::wstring ft = getText(g.cmbFps);
    for (auto& c : ft)
        if (c == L',') c = L'.';
    wchar_t* end = nullptr;
    double fps = wcstod(ft.c_str(), &end);
    if (end == ft.c_str() || *end != 0 || fps <= 0 || fps > 240) { err = TX("error.fps_invalid"); return false; }
    double dur = 0, start = 0;
    if (!parseSeconds(getText(g.edDur), dur) || dur <= 0) { err = TX("error.duration_invalid"); return false; }
    if (!parseSeconds(getText(g.edStart), start)) { err = TX("error.start_invalid"); return false; }
    s.width = w;
    s.height = h;
    s.fps = fps;
    s.duration = dur;
    s.start = start;
    s.crf = (int)SendMessageW(g.trkCrf, TBM_GETPOS, 0, 0);
    int ps = (int)SendMessageW(g.cmbPreset, CB_GETCURSEL, 0, 0);
    s.preset = "";
    for (const wchar_t* c = kPresets[ps < 0 ? 6 : ps]; *c; c++) s.preset += (char)*c;
    s.codec = codecAt((int)SendMessageW(g.cmbCodec, CB_GETCURSEL, 0, 0)).id;
    s.container = containerAt((int)SendMessageW(g.cmbContainer, CB_GETCURSEL, 0, 0)).id;
    s.depth = SendMessageW(g.cmbDepth, CB_GETCURSEL, 0, 0) == 1 ? 10 : 8;
    s.ss = std::max(0, (int)SendMessageW(g.cmbSS, CB_GETCURSEL, 0, 0)) + 1;
    {
        std::wstring od(GetWindowTextLengthW(g.edOut) + 1, L'\0');
        od.resize(GetWindowTextW(g.edOut, &od[0], (int)od.size()));
        while (!od.empty() && (od.back() == L' ' || od.back() == L'\\')) od.pop_back();
        s.outdir = od;
    }
    {
        double lp = 0, fi = 0, fo = 0;
        if (!parseSeconds(getText(g.edLoop), lp)) { err = TX("error.loop_invalid"); return false; }
        if (!parseSeconds(getText(g.edFadeIn), fi) || !parseSeconds(getText(g.edFadeOut), fo)) {
            err = TX("error.fade_invalid");
            return false;
        }
        int br = _wtoi(getText(g.edBitrate).c_str());
        if (br < 0 || br > 500000) { err = TX("error.bitrate_invalid"); return false; }
        s.bitrate = br;
        s.loop = lp;
        s.fadein = fi;
        s.fadeout = fo;
        static const char* kColors[] = {"bt709", "bt709full", "srgb", "bt601"};
        int ci = (int)SendMessageW(g.cmbColor, CB_GETCURSEL, 0, 0);
        s.color = kColors[ci < 0 || ci > 3 ? 0 : ci];
        static const int kBlurs[] = {1, 4, 8, 16};
        int bi = (int)SendMessageW(g.cmbBlur, CB_GETCURSEL, 0, 0);
        s.blur = kBlurs[bi < 0 || bi > 3 ? 0 : bi];
        std::wstring au(GetWindowTextLengthW(g.edAudio) + 1, L'\0');
        au.resize(GetWindowTextW(g.edAudio, &au[0], (int)au.size()));
        s.audio = au;
        static const char* kEncs[] = {"cpu", "auto", "nvenc", "amf", "qsv"};
        int ei = (int)SendMessageW(g.cmbEncoder, CB_GETCURSEL, 0, 0);
        s.encoder = kEncs[ei < 0 || ei > 4 ? 0 : ei];
    }
    return true;
}

void saveSettings() { writeIni(g.exeDir + L"config.ini", g.base); }

void browseOutDir() {
    BROWSEINFOW bi{};
    bi.hwndOwner = g.wnd;
    const std::wstring title = TX("dialog.out_folder");
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYDIRS;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t p[MAX_PATH * 2];
    if (SHGetPathFromIDListW(pidl, p)) SetWindowTextW(g.edOut, p);
    ILFree(pidl);
}

void browseAudio() {
    std::vector<wchar_t> buf(MAX_PATH * 2, 0);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = g.wnd;
    const std::wstring audioPat = L"*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac";
    const std::wstring filter = makeFilter({{TX("dialog.audio_files") + L" (" + audioPat + L")", audioPat}, {TX("dialog.all_files"), L"*.*"}});
    o.lpstrFilter = filter.c_str();
    o.lpstrFile = buf.data();
    o.nMaxFile = (DWORD)buf.size();
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&o)) SetWindowTextW(g.edAudio, buf.data());
}

static bool pickIni(bool save, std::wstring& path) {
    std::vector<wchar_t> buf(MAX_PATH * 2, 0);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = g.wnd;
    const std::wstring filter = makeFilter({{TX("dialog.profiles") + L" (*.ini)", L"*.ini"}});
    o.lpstrFilter = filter.c_str();
    o.lpstrFile = buf.data();
    o.nMaxFile = (DWORD)buf.size();
    o.lpstrDefExt = L"ini";
    o.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) return false;
    path = buf.data();
    return true;
}

void loadProfile() {
    std::wstring path;
    std::string text;
    if (!pickIni(false, path)) return;
    if (!readFile(path, text)) {
        MessageBoxW(g.wnd, TX("error.profile_read").c_str(), TX("dialog.profile_title").c_str(), MB_ICONWARNING);
        return;
    }
    Settings p;  // valeurs par defaut + celles du profil
    parseLines(p, text, false);
    settingsToUI(p);
    appendLog(TXF("log.profile_loaded", path.c_str()));
}

void saveProfile() {
    Settings cur = g.base;
    std::wstring err, path;
    if (!uiToSettings(cur, err)) {
        MessageBoxW(g.wnd, err.c_str(), TX("dialog.invalid_settings").c_str(), MB_ICONWARNING);
        return;
    }
    if (!pickIni(true, path)) return;
    writeIni(path, cur);
    appendLog(TXF("log.profile_saved", path.c_str()));
}


// Preselections rapides : modifient seulement taille, cadence, codec, conteneur et qualite ; le reste est conserve.
void applyQuickPreset(int idx) {
    Settings s = g.base;
    std::wstring e;
    if (!uiToSettings(s, e)) s = g.base;  // une valeur invalide dans la fenetre : on repart des derniers reglages valides
    applyBuiltinPreset(idx, s);
    settingsToUI(s);
}

// Menu « Plus » : preselections integrees supplementaires, puis fichiers presets/*.ini du dossier de l'application
void showPresetMenu() {
    HMENU m = CreatePopupMenu();
    for (int i = kChipPresetCount; i < kBuiltinPresetCount; i++) AppendMenuW(m, MF_STRING, 100 + i, TX(builtinPresetKey(i)).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    const std::vector<UserPreset> users = userPresets(g.exeDir);
    if (users.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 1, TX("ui.preset.none").c_str());
    for (size_t i = 0; i < users.size() && i < 100; i++) AppendMenuW(m, MF_STRING, (UINT_PTR)(200 + i), users[i].name.c_str());
    RECT r;
    GetWindowRect(g.chips[4], &r);
    const int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN, r.right, r.bottom, 0, g.wnd, nullptr);
    DestroyMenu(m);
    if (cmd >= 100 && cmd < 200) {
        applyQuickPreset(cmd - 100);
    } else if (cmd >= 200 && cmd - 200 < (int)users.size()) {
        bool ok = false;
        const Settings p = loadUserPreset(users[cmd - 200], &ok);
        if (!ok) {
            MessageBoxW(g.wnd, TX("error.profile_read").c_str(), TX("dialog.profile_title").c_str(), MB_ICONWARNING);
            return;
        }
        settingsToUI(p);
        appendLog(TXF("log.profile_loaded", users[cmd - 200].path.c_str()));
    }
}

// Taille estimee du fichier de sortie, comparee a l'espace libre du volume de destination
void updateEstimate() {
    if (!g.lblEst || !g.cmbDepth) return;
    Settings s = g.base;
    std::wstring e;
    if (!uiToSettings(s, e)) {
        SetWindowTextW(g.lblEst, L"");
        return;
    }
    std::wstring dir = s.outdir;
    if (dir.empty() && !g.paths.empty()) {
        const size_t cut = g.paths[0].find_last_of(L"\\/");
        dir = cut == std::wstring::npos ? L"" : g.paths[0].substr(0, cut);
    }
    const std::wstring txt = estimateText(s, dir);
    SetWindowTextW(g.lblEst, txt.c_str());
}
