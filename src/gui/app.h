// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Interface Windows : etat global, identifiants, messages et fonctions partagees entre les fichiers de src/gui.
#pragma once
#include "common.h"
#include "core/settings.h"
#include "core/formats.h"
#include "core/batches.h"
#include "core/channeldrop.h"
#include "core/endaction.h"
#include "core/filesettings.h"
#include "core/locate.h"
#include "core/player.h"
#include "core/queue.h"
#include "core/shadertoy_import.h"
#include "core/presets.h"

enum {
    ID_LIST = 101, ID_ADD, ID_REMOVE, ID_CLEAR, ID_START, ID_CANCEL, ID_PROG, ID_STATUS, ID_LOG,
    ID_RES = 120, ID_W, ID_H, ID_FPS, ID_DUR, ID_START_T, ID_CODEC, ID_PRESET, ID_CRF, ID_SNAP, ID_CONTAINER, ID_SS, ID_OUT, ID_BROWSE, ID_LOADP, ID_SAVEP,
    ID_BITRATE, ID_COLOR, ID_BLUR, ID_LOOP, ID_AUDIO, ID_AUDIOB, ID_FADEIN, ID_FADEOUT, ID_PAUSE, ID_FOLDER, ID_PREVIEW,
    ID_PREVTRK, ID_PREVBTN, ID_ENCODER, ID_PRE0, ID_PRE1, ID_PRE2, ID_PRE3, ID_PRE4, ID_DEPTH, ID_PLAY, ID_ABOUT, ID_LANG, ID_PARAMS, ID_OUTPUT, ID_FILESCOPE, ID_EDITOR, ID_FINDLOOP, ID_BUY
};
enum {
    WM_APP_LOG = WM_APP + 1,  // lParam = std::wstring* (a liberer)
    WM_APP_PROGRESS,          // wParam = index, lParam = ProgressInfo* (a liberer)
    WM_APP_FILE,              // wParam = index, lParam = FileStatus
    WM_APP_DONE,              // fin du lot (le thread peut etre joint)
    WM_APP_PREVIEW,           // lParam = PreviewData* (a liberer)
    WM_APP_DL,                // telechargement de ffmpeg : wParam = pourcentage, lParam = std::wstring* (message)
    WM_APP_DLDONE,            // fin du telechargement : wParam = 1 si ok, lParam = std::wstring* (erreur)
    WM_APP_MEDIA,             // images, sons ou videos deposes sur la fenetre : lParam = std::vector<std::wstring>* (a liberer)
    WM_APP_LOOP,              // periode trouvee par la recherche de boucle : lParam = double* (a liberer)
    WM_APP_COMPARE            // les deux images « avant / apres edition » : lParam = CompareData* (a liberer)
};
constexpr int kPrevSteps = 10000;  // graduations du curseur d'apercu (de Debut a Debut + Duree)
enum FileStatus { ST_PENDING, ST_RUNNING, ST_DONE, ST_FAILED, ST_CANCELLED };

struct PreviewData {
    int w, h;
    std::vector<uint8_t> bgra;  // haut en bas
    double t = -1;              // temps du shader si l'image vient du lecteur, sinon -1
};

struct CompareData {
    int w, h;
    std::vector<uint8_t> before, after;  // BGRA, haut en bas
};

struct ProgressInfo {
    long long n, total;
    double speed, remaining;
    double gpuImage = 0, gpuBuffers = 0, gpuPost = 0;  // temps GPU moyens par image (ms), 0 = pas encore mesures
};

struct App {
    HWND wnd = nullptr, list = nullptr, bAdd = nullptr, bRemove = nullptr, bClear = nullptr, bStart = nullptr,
         bCancel = nullptr, bSnap = nullptr, prog = nullptr, status = nullptr, log = nullptr;
    HWND grp = nullptr, cmbRes = nullptr, edW = nullptr, edH = nullptr, cmbFps = nullptr, edDur = nullptr,
         edStart = nullptr, cmbCodec = nullptr, cmbPreset = nullptr, cmbContainer = nullptr, cmbSS = nullptr, edOut = nullptr, bBrowse = nullptr, bLoadP = nullptr, bSaveP = nullptr, trkCrf = nullptr, lblCrf = nullptr;
    HWND edBitrate = nullptr, cmbColor = nullptr, cmbBlur = nullptr, edLoop = nullptr, edAudio = nullptr,
         bAudio = nullptr, edFadeIn = nullptr, edFadeOut = nullptr, bPause = nullptr, bFolder = nullptr,
         preview = nullptr, trkPrev = nullptr, lblPrev = nullptr, bPrev = nullptr, bPlay = nullptr, cmbEncoder = nullptr, cmbDepth = nullptr, lblEst = nullptr, lblForced = nullptr;
    HBITMAP prevBmp = nullptr;  // derniere image d apercu
    int prevW = 0, prevH = 0;
    std::vector<HWND> labels;  // libelles du panneau de reglages (dans l ordre de layout())
    HFONT font = nullptr, fontBold = nullptr;
    UINT dpi = 96;
    HBRUSH brCard = nullptr, brField = nullptr;
    HWND chips[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};  // preselections rapides
    HWND tip = nullptr;
    HWND bFileScope = nullptr;  // « ce shader seul » : les champs de la carte des reglages valent pour le shader selectionne uniquement
    HWND bAbout = nullptr, bLang = nullptr, bParams = nullptr, bOutput = nullptr, bEditor = nullptr, bBuy = nullptr;  // en-tête : À propos, langue, paramètres et post-traitement, sortie
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
    std::unique_ptr<PreviewPlayer> player;  // apercu en temps reel (lecture) ; null si arrete
    std::wstring playerPath;                // shader lu par le lecteur
    std::atomic<int> previewPending{0};     // images envoyees par le lecteur et pas encore affichees
    std::atomic<bool> running{false}, cancel{false}, pause{false};
    bool previewRun = false;   // le lot en cours est un apercu (UI uniquement)
    double previewTime = 0;
    bool orbitPending = false;  // la camera a bouge pendant un apercu : en refaire un des que celui-ci est fini
    FpsMeter playMeter;         // cadence reelle de l'apercu en lecture
    double snapshotAt = -1;     // image PNG au temps de l'apercu : temps du shader (secondes), -1 = au temps de debut
    bool snapshotRun = false;  // le lot en cours est une image (PNG, vignette ou planche-contact) (UI uniquement)
    int imageKind = 0;         // 0 image PNG au temps de début, 1 vignette de couverture, 2 planche-contact, 3 vignette intelligente, 4 recherche de boucle, 5 faces de cubemap, 6 carte du cout, 7 controle qualite, 8 estimation du temps, 9 comparaison avant / apres edition
    int sheetCols = 0, sheetRows = 0;
    std::wstring snapshotPath;
    // Comparaison de deux versions du shader (editeur) : textes envoyes au thread de rendu, puis rideau a glisser dans l'apercu
    std::string cmpBeforeText, cmpAfterText;
    struct Curtain {
        bool on = false;
        int w = 0, h = 0;
        std::vector<uint8_t> before, after;
        double x = 0.5;  // position du rideau (0 a 1) : avant a gauche, apres a droite
        bool dragging = false;
    } curtain;
    std::wstring exeDir;
    Settings base;
    std::vector<Override> forced;  // reglages imposes par le shader selectionne : affiches grises a la place de ceux de la fenetre
    // Reglages propres au shader selectionne : pendant qu'ils sont actifs, g.base les contient (les champs les montrent) et `general` garde les
    // reglages generaux ; ils sont enregistres dans jobs/ quand on change de shader, au rendu et a la fermeture.
    struct FileScope {
        bool on = false;
        std::wstring path;   // shader dont les reglages propres sont actifs
        std::wstring shown;  // shader dont l'etat est affiche (suit la selection)
        Settings general;
    } fileScope;
    BatchMap batches;  // numero de lot (1 a 4) des shaders de la liste ; jobs/batches.txt
    Settings runBase;  // reglages generaux du lot en cours (le thread de rendu n'en lit pas d'autres)
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
void moveSelected(int mode);  // priorite : -1 monte, +1 descend, 0 passe en tete (les shaders selectionnes)
void setBatchOfSelected(int n);  // lot des shaders selectionnes (0 = aucun)
void showListMenu();          // menu contextuel de la liste : priorite, rendre la selection, retirer
void clearAll();
void browseFiles();
void dropMedia(const std::vector<std::wstring>& files);  // image / son / video deposes : choix du canal, ecrit `// iChannelN=...` dans le shader selectionne

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
void startPlayer();         // lecture en temps reel du shader selectionne
void stopPlayer();
void followSelection();     // la lecture suit le shader selectionne
void updatePlayButton();
void saveQueueNow();         // file d'attente (shaders pas encore rendus) dans queue.txt
void updateForced();        // reglages imposes par le shader selectionne (// cle=valeur) : champs affiches grises avec la valeur du shader
bool isForced(const char* key);     // ce reglage est-il impose par le shader selectionne ?
bool controlForced(HWND c);         // ce champ de la fenetre est-il grise parce que le shader impose le reglage ?
void enableSettingsControls();      // active / grise les champs : ni pendant un rendu, ni quand le shader impose le reglage
void updateEstimate();      // taille estimee du fichier, sous le preset
std::wstring selectedShaderPath();   // shader selectionne dans la liste (le premier sinon), "" si la liste est vide
Settings generalSettings();          // reglages generaux : g.base sans les reglages propres au shader
void flushFileScope();               // enregistre les reglages propres au shader courant (jobs/)
void syncFileScope();                // la selection a change : quitte l'etat du shader precedent, prend celui du nouveau
void toggleFileScope();              // bouton « ce shader seul » : active, ou rend les reglages generaux et efface ceux du shader
void writeScopeIntoShader();         // ecrit les reglages propres au shader en commentaires dans le shader (demande de l'utilisateur)
void updateFileScopeButton();
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
void startJobs(bool snapshot = false, int imageKind = 0, int cols = 0, int rows = 0, bool atPreview = false, bool selectedOnly = false, int batch = 0);  // snapshot : une image (0 PNG, 1 vignette, 2 planche cols x rows, 3 vignette intelligente, 4 recherche de boucle, 5 faces de cubemap)
void showImageMenu();                // menu du bouton « Images » : PNG, vignette, planches-contact
void updatePrevLabel();
void runPreview();
// Scene 3D (edition Pro) : le shader selectionne en demande-t-il une, et deplacement de la camera a la souris sur l'apercu (degres d'orbite, facteur de distance).
bool sceneSelected();
void orbitScene(double dx, double dy, double zoom);
void openFolder();

// --- window.cpp ---
LRESULT CALLBACK wndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp);
void restoreWindowPlacement(HWND wnd, int showCmd);  // window.ini : position et taille memorisees
void saveWindowPlacement(HWND wnd);
std::string readSavedLanguage();  // langue mémorisée dans window.ini ("" si aucune)
void showBuy(HWND parent);        // achat de l'edition Pro : trois montants, ouvre la page PayPal (buy.cpp)
void showAbout(HWND parent);      // fenêtre « À propos » (about.cpp)
void showParams(HWND parent);     // fenêtre « Paramètres et post-traitement » (params_dialog.cpp)
void showEditor(HWND parent);     // éditeur du shader sélectionné et de ses passes (editor.cpp)
bool editorIsOpen();
void editorNoteLog(const std::wstring& line);  // erreur « fichier:ligne: message » du journal, marquée dans l'éditeur
void showOutput(HWND parent);     // fenêtre « Sortie » : alpha, HDR, taille, interpolation, volume, texte (output_dialog.cpp)
void setDropHot(bool hot);  // illumine / eteint la zone de depot (fondu)
