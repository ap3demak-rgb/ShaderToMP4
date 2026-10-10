#include "app.h"
#include "core/util.h"

// --- Panneau de reglages video ---
const ResPreset kRes[] = {
    {L"1280x720 (720p)", 1280, 720},
    {L"1920x1080 (1080p)", 1920, 1080},
    {L"2560x1440 (1440p)", 2560, 1440},
    {L"3840x2160 (4K)", 3840, 2160},
    {L"1080x1920 (vertical 9:16)", 1080, 1920},
    {L"1080x1080 (carré 1:1)", 1080, 1080},
};
const int kResCount = (int)(sizeof(kRes) / sizeof(kRes[0]));  // l'entree suivante = "Personnalise"
const wchar_t* kPresets[] = {L"ultrafast", L"superfast", L"veryfast", L"faster", L"fast",
                                    L"medium",    L"slow",      L"slower",   L"veryslow"};

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
    const wchar_t* hint = c == 0 ? L"sans perte" : c <= 12 ? L"quasi parfait" : c <= 18 ? L"très bon"
                          : c <= 23 ? L"bon" : L"qualité réduite";
    SetWindowTextW(g.lblCrf, wfmt(L"%d  -  %ls", c, hint).c_str());
}

void settingsToUI(const Settings& s) {
    SetWindowTextW(g.edW, std::to_wstring(s.width).c_str());
    SetWindowTextW(g.edH, std::to_wstring(s.height).c_str());
    SetWindowTextW(g.cmbFps, wfmt(L"%g", s.fps).c_str());
    SetWindowTextW(g.edDur, wfmt(L"%g", s.duration).c_str());
    SetWindowTextW(g.edStart, wfmt(L"%g", s.start).c_str());
    SendMessageW(g.trkCrf, TBM_SETPOS, TRUE, std::min(35, std::max(0, s.crf)));
    int p = (int)SendMessageW(g.cmbPreset, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)W(s.preset).c_str());
    SendMessageW(g.cmbPreset, CB_SETCURSEL, (WPARAM)(p < 0 ? 6 : p), 0);
    SendMessageW(g.cmbCodec, CB_SETCURSEL, s.codec == "h265" ? 1 : 0, 0);
    SendMessageW(g.cmbContainer, CB_SETCURSEL, s.container == "mov" ? 1 : s.container == "mkv" ? 2 : 0, 0);
    SendMessageW(g.cmbSS, CB_SETCURSEL, (WPARAM)(std::min(4, std::max(1, s.ss)) - 1), 0);
    SetWindowTextW(g.edOut, s.outdir.c_str());
    SetWindowTextW(g.edBitrate, std::to_wstring(s.bitrate).c_str());
    SendMessageW(g.cmbColor, CB_SETCURSEL, s.color == "bt709full" ? 1 : s.color == "srgb" ? 2 : s.color == "bt601" ? 3 : 0, 0);
    SendMessageW(g.cmbBlur, CB_SETCURSEL, s.blur >= 16 ? 3 : s.blur >= 8 ? 2 : s.blur >= 2 ? 1 : 0, 0);
    SetWindowTextW(g.edLoop, wfmt(L"%g", s.loop).c_str());
    SetWindowTextW(g.edAudio, s.audio.c_str());
    SetWindowTextW(g.edFadeIn, wfmt(L"%g", s.fadein).c_str());
    SetWindowTextW(g.edFadeOut, wfmt(L"%g", s.fadeout).c_str());
    SendMessageW(g.cmbEncoder, CB_SETCURSEL, s.encoder == "auto" ? 1 : s.encoder == "nvenc" ? 2 : s.encoder == "amf" ? 3 : s.encoder == "qsv" ? 4 : 0, 0);
    syncResCombo();
    updateCrfLabel();
}

bool uiToSettings(Settings& s, std::wstring& err) {
    int w = _wtoi(getText(g.edW).c_str()), h = _wtoi(getText(g.edH).c_str());
    if (w < 16 || w > 16384 || h < 16 || h > 16384) { err = L"Résolution invalide (16 à 16384 pixels)."; return false; }
    std::wstring ft = getText(g.cmbFps);
    for (auto& c : ft)
        if (c == L',') c = L'.';
    wchar_t* end = nullptr;
    double fps = wcstod(ft.c_str(), &end);
    if (end == ft.c_str() || *end != 0 || fps <= 0 || fps > 240) { err = L"FPS invalide (0 à 240)."; return false; }
    double dur = 0, start = 0;
    if (!parseSeconds(getText(g.edDur), dur) || dur <= 0) { err = L"Durée invalide (ex : 90 ou 1:30)."; return false; }
    if (!parseSeconds(getText(g.edStart), start)) { err = L"Début invalide (ex : 0 ou 0:10)."; return false; }
    s.width = w;
    s.height = h;
    s.fps = fps;
    s.duration = dur;
    s.start = start;
    s.crf = (int)SendMessageW(g.trkCrf, TBM_GETPOS, 0, 0);
    int ps = (int)SendMessageW(g.cmbPreset, CB_GETCURSEL, 0, 0);
    s.preset = "";
    for (const wchar_t* c = kPresets[ps < 0 ? 6 : ps]; *c; c++) s.preset += (char)*c;
    s.codec = SendMessageW(g.cmbCodec, CB_GETCURSEL, 0, 0) == 1 ? "h265" : "h264";
    int ct = (int)SendMessageW(g.cmbContainer, CB_GETCURSEL, 0, 0);
    s.container = ct == 1 ? "mov" : ct == 2 ? "mkv" : "mp4";
    s.ss = std::max(0, (int)SendMessageW(g.cmbSS, CB_GETCURSEL, 0, 0)) + 1;
    {
        std::wstring od(GetWindowTextLengthW(g.edOut) + 1, L'\0');
        od.resize(GetWindowTextW(g.edOut, &od[0], (int)od.size()));
        while (!od.empty() && (od.back() == L' ' || od.back() == L'\\')) od.pop_back();
        s.outdir = od;
    }
    {
        double lp = 0, fi = 0, fo = 0;
        if (!parseSeconds(getText(g.edLoop), lp)) { err = L"Période de boucle invalide (secondes, 0 = libre)."; return false; }
        if (!parseSeconds(getText(g.edFadeIn), fi) || !parseSeconds(getText(g.edFadeOut), fo)) {
            err = L"Durée de fondu invalide (secondes).";
            return false;
        }
        int br = _wtoi(getText(g.edBitrate).c_str());
        if (br < 0 || br > 500000) { err = L"Débit invalide (kb/s, 0 = qualité constante)."; return false; }
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
    bi.lpszTitle = L"Dossier de sortie des vidéos";
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
    o.lpstrFilter = L"Audio (*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac)\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac\0Tous les fichiers\0*.*\0";
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
    o.lpstrFilter = L"Profils de réglages (*.ini)\0*.ini\0";
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
        MessageBoxW(g.wnd, L"Impossible de lire ce profil.", L"Profil", MB_ICONWARNING);
        return;
    }
    Settings p;  // valeurs par defaut + celles du profil
    parseLines(p, text, false);
    settingsToUI(p);
    appendLog(L"Profil chargé : " + path);
}

void saveProfile() {
    Settings cur = g.base;
    std::wstring err, path;
    if (!uiToSettings(cur, err)) {
        MessageBoxW(g.wnd, err.c_str(), L"Réglages invalides", MB_ICONWARNING);
        return;
    }
    if (!pickIni(true, path)) return;
    writeIni(path, cur);
    appendLog(L"Profil enregistré : " + path);
}


// Preselections rapides : modifient seulement les reglages de qualite, le reste est conserve.
void applyQuickPreset(int idx) {
    Settings s = g.base;
    std::wstring e;
    if (!uiToSettings(s, e)) s = g.base;  // une valeur invalide dans la fenetre : on repart des derniers reglages valides
    s.blur = 1;
    s.ss = 1;
    switch (idx) {
        case 0:  // brouillon : rapide pour tester
            s.width = 1280; s.height = 720; s.fps = 30; s.crf = 23; s.preset = "veryfast"; s.codec = "h264";
            break;
        case 1:  // YouTube 1080p60
            s.width = 1920; s.height = 1080; s.fps = 60; s.crf = 16; s.preset = "slow"; s.codec = "h264"; s.container = "mp4";
            break;
        case 2:  // 4K 60 images/s
            s.width = 3840; s.height = 2160; s.fps = 60; s.crf = 20; s.preset = "medium"; s.codec = "h265"; s.container = "mp4";
            break;
        default:  // vertical 9:16
            s.width = 1080; s.height = 1920; s.fps = 30; s.crf = 16; s.preset = "slow"; s.codec = "h264"; s.container = "mp4";
            break;
    }
    s.bitrate = 0;
    settingsToUI(s);
}
