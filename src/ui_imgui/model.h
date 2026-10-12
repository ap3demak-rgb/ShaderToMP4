// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Modèle de l'interface Linux / macOS : réglages en cours d'édition, liste des shaders, état du rendu, journal.
// Aucune dépendance à ImGui : le dessin est dans gui_main.cpp. Le rendu tourne dans un thread de travail
// qui ne parle au modèle que par une file d'événements vidée par pump() dans le thread de l'interface.
#pragma once
#include "common.h"
#include "core/batches.h"
#include "core/channeldrop.h"
#include "core/filesettings.h"
#include "core/player.h"
#include "core/settings.h"

namespace ui {

enum State { ST_PENDING, ST_RUNNING, ST_DONE, ST_FAILED, ST_CANCELLED };

struct ResPreset {
    const char* tag;  // texte fixe (« 720p »), ou nullptr
    const char* key;  // clé de langue, ou nullptr
    int w, h;
};
extern const ResPreset kRes[];
extern const int kResCount;
extern const char* const kPresetNames[9];
extern const char* const kColors[4];
extern const int kBlurs[4];
extern const std::vector<std::string>& encoderList();  // « cpu », « auto » puis les encodeurs matériels du système

// Valeurs des champs de saisie (tampons de texte pour les champs libres, indices pour les listes)
struct Fields {
    int w = 1920, h = 1080;
    char fps[16] = "60", dur[16] = "90", start[16] = "0";
    int crf = 12, preset = 6, codec = 0, container = 0, ss = 0, depth = 0;  // ss : 0..3 = x1..x4 ; depth : 0 = 8 bits, 1 = 10 bits
    char outdir[512] = "";
    int bitrate = 0;
    int color = 0, blur = 0, encoder = 0;
    char loop[16] = "0", audio[512] = "", fadein[16] = "0", fadeout[16] = "0";
    float previewPos = 0.0f;  // 0..1 entre « début » et « début + durée »
    bool postOn[8] = {false, false, false, false, false, false, false, false};  // effets de post-traitement (ordre de postEffectInfos())
    float postAmt[8] = {0, 0, 0, 0, 0, 0, 0, 0};                                 // leur quantité
    char lut[512] = "";                                                           // LUT couleur .cube
    // Sortie : transparence, HDR, taille, interpolation, volume sonore, titre et credit (core/outputopts)
    bool alpha = false;
    int hdr = 0, scaleFilter = 0;  // hdr : 0 aucun, 1 HDR10 (PQ), 2 HLG
    char hdrPeak[16] = "1000", hdrFall[16] = "400", hdrWhite[16] = "203", scale[32] = "", interp[16] = "", loudnorm[16] = "";
    char title[256] = "", credit[256] = "", titleDur[16] = "3";
    // Avance : nom du fichier, fichier existant, metadonnees, fin de lot, parties, file de lecture GPU, obturateur
    char name[256] = "", chunk[16] = "30", pbo[8] = "3", shutter[16] = "0,5", variants[512] = "";
    int exists = 0, endAction = 0;  // exists : 0 ecraser, 1 numeroter, 2 ignorer ; endAction : 0 rien, 1 dossier, 2 veille
    bool metadata = true;
    // Temps et boucle : boucle sans couture (0 aucune, 1 fondu, 2 aller-retour), fondu, courbe de temps, vitesse, echantillons de l'image fixe
    int loopMode = 0;
    bool audioFollow = false;
    int stillFormat = 0;  // 0 PNG, 1 TIFF
    bool precheck = false;  // controle qualite avant chaque rendu video
    char loopFade[16] = "1", timemap[256] = "", speed[16] = "", samples[16] = "", tile[16] = "", dpi[16] = "";
};

struct LogLine {
    std::wstring text;
    int level;  // 0 normal, 1 erreur, 2 avertissement, 3 succès, 4 titre
};

class Model {
public:
    explicit Model(const std::wstring& exeDir);
    ~Model();

    // --- Réglages ---
    std::wstring exeDir;
    Settings base;  // derniers réglages validés (écrits dans config.ini)
    Fields f;
    void fromSettings(const Settings& s);
    bool toSettings(Settings& s, std::wstring& err) const;  // les reglages imposes par le shader (champs gris) ne sont pas lus : s garde la valeur de la fenetre
    // Reglages imposes par le shader selectionne : les champs montrent ses valeurs (grises) ; les valeurs de la fenetre restent dans `base`
    std::vector<Override> forced;
    bool isForced(const char* key) const { return isOverridden(forced, key); }
    void refreshForced();  // a appeler a chaque image : suit la selection et les changements du fichier
    // Reglages propres au shader selectionne (« ce shader seul ») : pendant qu'ils sont actifs, `base` et les champs les montrent et
    // generalSettings() rend les reglages generaux ; ils sont enregistres dans jobs/ (jamais dans le shader, sauf writeScopeIntoShader).
    bool fileScopeOn() const { return fileOn_; }
    Settings generalSettings() const;
    void toggleFileScope();
    void writeScopeIntoShader();
    void flushFileScope();
    void applyQuickPreset(int idx);  // preselections integrees (core/presets.h)
    int formatsRevision = 0;         // change quand ffmpeg est (re)installe : les listes de formats sont a refaire
    void save() const;
    bool loadProfile(const std::wstring& path);
    bool saveProfile(const std::wstring& path);

    // --- Liste ---
    std::vector<std::wstring> paths;
    std::vector<int> state;
    std::vector<std::wstring> stateText;
    int selected = 0;
    void addFiles(const std::vector<std::wstring>& files);
    // Fichiers deposes : les shaders vont dans la liste ; les images, sons et videos attendent le choix du canal (pendingMedia, premier en tete)
    void dropFiles(const std::vector<std::wstring>& files);
    std::vector<std::wstring> pendingMedia;
    void applyMedia(const std::wstring& file, int channel);  // channel 0 a 3, ou -1 : bande-son (audio=)
    void removeSelected();
    void moveSelected(int mode);  // priorite : -1 monte, +1 descend, 0 passe en tete
    void resetSelectedState();
    BatchMap batches;  // lots (1 a 4) des shaders de la liste ; jobs/batches.txt
    void setBatch(int n);  // lot du shader selectionne (0 = aucun)    // le shader selectionne redevient « en attente »
    void clearList();
    void saveQueue() const;  // shaders pas encore rendus : queue.txt, retrouves a la reouverture

    // --- Actions ---
    void startJobs(bool snapshot, int imageKind = 0, int cols = 0, int rows = 0, bool atPreview = false, bool selectedOnly = false, int batch = 0);  // snapshot : une image (0 PNG, 1 vignette, 2 planche cols x rows, 3 vignette intelligente, 4 recherche de boucle)
    void runPreview();
    void togglePause();
    // Apercu en temps reel (lecture) : demarre / arrete, suit le curseur et le shader selectionne
    bool playing() const { return player_ && player_->running(); }
    FpsMeter playMeter;  // cadence reelle de l'apercu en lecture
    void togglePlay();
    void stopPlay();
    void seekPlay();  // le curseur d'apercu a bouge : saute a ce temps
    void cancelRun();
    void openOutputFolder();
    void startFfmpegDownload();
    bool ffmpegMissing() const;

    // --- État visible ---
    bool running() const { return running_.load(); }
    bool paused() const { return pause_.load(); }
    bool previewRun = false;
    float progress = 0.0f;  // 0..1
    bool progressFailed = false;
    std::wstring status;
    std::wstring dialogTitle, dialogText;  // fenêtre de message à afficher (vide = aucune)
    bool askDownload = false;              // proposer le téléchargement de ffmpeg
    std::vector<LogLine> log;
    bool logDirty = false;

    // Aperçu : pixels BGRA à envoyer en texture par l'interface
    std::vector<uint8_t> prevPixels;
    int prevW = 0, prevH = 0;
    bool prevFresh = false;
    // Comparaison avant / apres edition : deux images et un rideau a glisser dans l'apercu (avant a gauche, apres a droite)
    struct Curtain {
        bool on = false;
        int w = 0, h = 0;
        std::vector<uint8_t> before, after;
        float x = 0.5f;
    } curtain;
    void composeCurtain();  // ecrit l'image d'apercu a la position du rideau
    void compareVersions(const std::string& before, const std::string& after);  // une image de chaque version au temps de l'apercu

    std::function<void(const std::wstring&)> logHook;  // appelee pour chaque ligne du journal (l'editeur y lit les erreurs du compilateur)
    void appendLog(const std::wstring& text);
    void setStatus(const std::wstring& t) { status = t; }
    void pump();  // à appeler à chaque image, dans le thread de l'interface
    std::wstring statusOf(int st) const;
    void refreshTexts();  // après un changement de langue

private:
    struct Event {
        enum Kind { Log, Progress, File, Done, Preview, Download, DownloadDone, Loop, Compare } kind;  // Loop : speed = periode trouvee
        int idx = 0, w = 0, h = 0;
        long long n = 0, total = 0;
        double speed = 0, remaining = 0;
        double gpuImage = 0, gpuBuffers = 0, gpuPost = 0;  // temps GPU moyens par image (ms)
        std::wstring text;
        std::vector<uint8_t> px, px2;  // px2 : image « apres » de la comparaison
    };
    void post(Event&& e);
    void runJobsThread(std::vector<std::pair<int, std::wstring>> jobs);
    void join();

    std::atomic<bool> running_{false}, cancel_{false}, pause_{false};
    std::string cmpBefore_, cmpAfter_;  // textes des deux versions, lus par le thread de rendu
    std::atomic<double> gpu_[3];  // temps GPU moyens par image mesures pendant le rendu
    bool snapshotRun_ = false;
    int imageKind_ = 0, sheetCols_ = 0, sheetRows_ = 0;
public:
    bool attention = false;  // un lot vient de finir : la fenetre demande l'attention (barre des taches / Dock)
private:
    std::wstring snapshotPath_;
    double previewTime_ = 0;
    Settings runSettings_;
    std::thread worker_;
    std::unique_ptr<PreviewPlayer> player_;
    std::atomic<int> playPending_{0};  // images du lecteur pas encore affichees
    std::wstring playPath_;
    double playStart_ = 0, playDur_ = 1;
    void syncFileScope(const std::wstring& cur);
    void leaveFileScope(bool deleteSettings);
    void enterFileScope(const std::wstring& path, const std::vector<Override>& list);
    bool fileOn_ = false;
    std::wstring filePath_, fileShown_;
    Settings general_;
    double runStart_ = -1;  // image PNG au temps de l'apercu : temps du shader, -1 = au temps de debut
    std::wstring forcedPath_;
    std::chrono::steady_clock::time_point forcedChecked_{};
    std::mutex evMutex_;
    std::vector<Event> events_;
};

bool parseSeconds(std::wstring t, double& out);  // « 90 », « 1:30 », « 1,5 »

}  // namespace ui
