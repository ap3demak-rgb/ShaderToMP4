// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Modèle de l'interface Linux / macOS : réglages en cours d'édition, liste des shaders, état du rendu, journal.
// Aucune dépendance à ImGui : le dessin est dans gui_main.cpp. Le rendu tourne dans un thread de travail
// qui ne parle au modèle que par une file d'événements vidée par pump() dans le thread de l'interface.
#pragma once
#include "common.h"
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
extern const char* const kCodecs[2];
extern const char* const kContainers[3];
extern const char* const kColors[4];
extern const int kBlurs[4];
extern const std::vector<std::string>& encoderList();  // « cpu », « auto » puis les encodeurs matériels du système

// Valeurs des champs de saisie (tampons de texte pour les champs libres, indices pour les listes)
struct Fields {
    int w = 1920, h = 1080;
    char fps[16] = "60", dur[16] = "90", start[16] = "0";
    int crf = 12, preset = 6, codec = 0, container = 0, ss = 0;  // ss : 0..3 = x1..x4
    char outdir[512] = "";
    int bitrate = 0;
    int color = 0, blur = 0, encoder = 0;
    char loop[16] = "0", audio[512] = "", fadein[16] = "0", fadeout[16] = "0";
    float previewPos = 0.0f;  // 0..1 entre « début » et « début + durée »
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
    bool toSettings(Settings& s, std::wstring& err) const;
    void applyQuickPreset(int idx);  // 0 brouillon, 1 YouTube, 2 4K 60, 3 vertical
    void save() const;
    bool loadProfile(const std::wstring& path);
    bool saveProfile(const std::wstring& path);

    // --- Liste ---
    std::vector<std::wstring> paths;
    std::vector<int> state;
    std::vector<std::wstring> stateText;
    int selected = 0;
    void addFiles(const std::vector<std::wstring>& files);
    void removeSelected();
    void clearList();

    // --- Actions ---
    void startJobs(bool snapshot);
    void runPreview();
    void togglePause();
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

    void appendLog(const std::wstring& text);
    void setStatus(const std::wstring& t) { status = t; }
    void pump();  // à appeler à chaque image, dans le thread de l'interface
    std::wstring statusOf(int st) const;
    void refreshTexts();  // après un changement de langue

private:
    struct Event {
        enum Kind { Log, Progress, File, Done, Preview, Download, DownloadDone } kind;
        int idx = 0, w = 0, h = 0;
        long long n = 0, total = 0;
        double speed = 0, remaining = 0;
        std::wstring text;
        std::vector<uint8_t> px;
    };
    void post(Event&& e);
    void runJobsThread(std::vector<std::pair<int, std::wstring>> jobs);
    void join();

    std::atomic<bool> running_{false}, cancel_{false}, pause_{false};
    bool snapshotRun_ = false;
    std::wstring snapshotPath_;
    double previewTime_ = 0;
    Settings runSettings_;
    std::thread worker_;
    std::mutex evMutex_;
    std::vector<Event> events_;
};

bool parseSeconds(std::wstring t, double& out);  // « 90 », « 1:30 », « 1,5 »

}  // namespace ui
