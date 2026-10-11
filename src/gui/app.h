// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Interface Windows : etat global, identifiants, messages et fonctions partagees entre les fichiers de src/gui.
#pragma once
#include "common.h"
#include "core/settings.h"
#include "core/formats.h"
#include "core/presets.h"

enum {
    ID_LIST = 101, ID_ADD, ID_REMOVE, ID_CLEAR, ID_START, ID_CANCEL, ID_PROG, ID_STATUS, ID_LOG,
    ID_RES = 120, ID_W, ID_H, ID_FPS, ID_DUR, ID_START_T, ID_CODEC, ID_PRESET, ID_CRF, ID_SNAP, ID_CONTAINER, ID_SS, ID_OUT, ID_BROWSE, ID_LOADP, ID_SAVEP,
    ID_BITRATE, ID_COLOR, ID_BLUR, ID_LOOP, ID_AUDIO, ID_AUDIOB, ID_FADEIN, ID_FADEOUT, ID_PAUSE, ID_FOLDER, ID_PREVIEW,
    ID_PREVTRK, ID_PREVBTN, ID_ENCODER, ID_PRE0, ID_PRE1, ID_PRE2, ID_PRE3, ID_PRE4, ID_DEPTH, ID_ABOUT, ID_LANG
};
enum {
    WM_APP_LOG = WM_APP + 1,  // lParam = std::wstring* (a liberer)
    WM_APP_PROGRESS,          // wParam = index, lParam = ProgressInfo* (a liberer)
    WM_APP_FILE,              // wParam = index, lParam = FileStatus
    WM_APP_DONE,              // fin du lot (le thread peut etre joint)
    WM_APP_PREVIEW,           // lParam = PreviewData* (a liberer)
    WM_APP_DL,                // telechargement de ffmpeg : wParam = pourcentage, lParam = std::wstring* (message)
    WM_APP_DLDONE             // fin du telechargement : wParam = 1 si ok, lParam = std::wstring* (erreur)
};
enum FileStatus { ST_PENDING, ST_RUNNING, ST_DONE, ST_FAILED, ST_CANCELLED };

struct PreviewData {
    int w, h;
    std::vector<uint8_t> bgra;  // haut en bas
};

struct ProgressInfo {
    long long n, total;
    double speed, remaining;
};

struct App {
    HWND wnd = nullptr, list = nullptr, bAdd = nullptr, bRemove = nullptr, bClear = nullptr, bStart = nullptr,
         bCancel = nullptr, bSnap = nullptr, prog = nullptr, status = nullptr, log = nullptr;
    HWND grp = nullptr, cmbRes = nullptr, edW = nullptr, edH = nullptr, cmbFps = nullptr, edDur = nullptr,
         edStart = nullptr, cmbCodec = nullptr, cmbPreset = nullptr, cmbContainer = nullptr, cmbSS = nullptr, edOut = nullptr, bBrowse = nullptr, bLoadP = nullptr, bSaveP = nullptr, trkCrf = nullptr, lblCrf = nullptr;
    HWND edBitrate = nullptr, cmbColor = nullptr, cmbBlur = nullptr, edLoop = nullptr, edAudio = nullptr,
         bAudio = nullptr, edFadeIn = nullptr, edFadeOut = nullptr, bPause = nullptr, bFolder = nullptr,
         preview = nullptr, trkPrev = nullptr, lblPrev = nullptr, bPrev = nullptr, cmbEncoder = nullptr, cmbDepth = nullptr, lblEst = nullptr, lblForced = nullptr;
    HBITMAP prevBmp = nullptr;  // derniere image d apercu
    int prevW = 0, prevH = 0;
    std::vector<HWND> labels;  // libelles du panneau de reglages (dans l ordre de layout())
    HFONT font = nullptr, fontBold = nullptr;
    UINT dpi = 96;
    HBRUSH brCard = nullptr, brField = nullptr;
    HWND chips[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};  // preselections rapides
    HWND tip = nullptr;
    HWND bAbout = nullptr, bLang = nullptr;  // en-tête : À propos, langue
    int chipRight = 0;                       // bord droit de la pastille de version (à gauche des boutons de l'en-tête)
    struct Card {
        RECT r;
        std::wstring title;
        int icon;
    };
    std::vector<Card> cards;  // recalcule par layout() ; peint par WM_PAINT
    RECT headerRect{}, dropRect{}, bandRect{};
    bool dropHot = false;   // fichiers en cours de survol (glisser-deposer)
    float dropAmt = 0;      // intensite de l illumination 0..1, animee
    std::vector<std::wstring> paths;
    std::vector<int> state;  // FileStatus, parallele a paths
    std::thread worker;
    std::atomic<bool> running{false}, cancel{false}, pause{false};
    bool previewRun = false;   // le lot en cours est un apercu (UI uniquement)
    double previewTime = 0;
    bool snapshotRun = false;  // le lot en cours est une image PNG (UI uniquement)
    std::wstring snapshotPath;
    std::wstring exeDir;
    Settings base;
};
extern App g;

// --- app_state.cpp : etat de la fenetre ---
std::wstring statusText(int st);  // état d'un shader dans la liste, dans la langue courante
void setItemText(int i, int sub, const std::wstring& t);
void setStatus(const std::wstring& t);
void appendLog(const std::wstring& text);
void updateButtons();

// --- file_list.cpp : liste des shaders ---
void addFiles(const std::vector<std::wstring>& files);
void removeSelected();
void clearAll();
void browseFiles();

// --- settings_panel.cpp : panneau "Reglages video" ---
struct ResPreset {
    const wchar_t* tag;  // texte technique (720p, 4K...) ou nullptr
    const char* key;     // clé i18n du libellé (vertical, carré...) ou nullptr
    int w, h;
};
std::wstring resName(const ResPreset& r);  // "1280x720 (720p)" dans la langue courante
extern const ResPreset kRes[6];
extern const int kResCount;  // l'entree suivante de la liste = "Personnalise"
extern const wchar_t* kPresets[9];
std::wstring getText(HWND h);
bool parseSeconds(std::wstring t, double& out);  // "90", "1:30", "0:01:30" -> secondes
void syncResCombo();
void updateCrfLabel();
void updateForced();        // reglages imposes par le shader selectionne (// cle=valeur)
void updateEstimate();      // taille estimee du fichier, sous le preset
void showPresetMenu();      // menu « Plus » : preselections integrees et fichiers presets/*.ini
void settingsToUI(const Settings& s);
bool uiToSettings(Settings& s, std::wstring& err);  // false + message si une valeur est invalide
void saveSettings();                                // ecrit config.ini
void browseOutDir();
void browseAudio();
void loadProfile();
void saveProfile();
void applyQuickPreset(int idx);  // 0 brouillon, 1 YouTube 1080p60, 2 4K 60, 3 vertical 9:16

// --- jobs.cpp : rendus, apercu, telechargement ---
void promptDownload();               // propose de telecharger ffmpeg.exe
void startJobs(bool snapshot = false);
void updatePrevLabel();
void runPreview();
void openFolder();

// --- window.cpp ---
LRESULT CALLBACK wndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp);
void restoreWindowPlacement(HWND wnd, int showCmd);  // window.ini : position et taille memorisees
void saveWindowPlacement(HWND wnd);
std::string readSavedLanguage();  // langue mémorisée dans window.ini ("" si aucune)
void showAbout(HWND parent);      // fenêtre « À propos » (about.cpp)
void setDropHot(bool hot);  // illumine / eteint la zone de depot (fondu)
