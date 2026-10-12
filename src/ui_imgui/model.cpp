// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "ui_imgui/model.h"
#include "core/ffmpeg_download.h"
#include "core/formats.h"
#include "core/endaction.h"
#include "core/presets.h"
#include "core/queue.h"
#include "core/shadertoy_import.h"
#include "core/gl.h"
#include "core/looptools.h"
#include "core/outputopts.h"
#include "core/i18n.h"
#include "core/postfx.h"
#include "core/render.h"
#include "core/util.h"
#include "platform/platform.h"

namespace ui {

const ResPreset kRes[] = {
    {"720p", nullptr, 1280, 720},
    {"1080p", nullptr, 1920, 1080},
    {"1440p", nullptr, 2560, 1440},
    {"4K", nullptr, 3840, 2160},
    {nullptr, "ui.res.vertical", 1080, 1920},
    {nullptr, "ui.res.square", 1080, 1080},
};
const int kResCount = (int)(sizeof(kRes) / sizeof(kRes[0]));
const char* const kPresetNames[9] = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"};
const char* const kColors[4] = {"bt709", "bt709full", "srgb", "bt601"};
const int kBlurs[4] = {1, 4, 8, 16};

const std::vector<std::string>& encoderList() {
    static const std::vector<std::string> list = {"cpu", "auto", "nvenc",
#ifdef __APPLE__
                                                  "videotoolbox"
#else
                                                  "vaapi", "qsv"
#endif
    };
    return list;
}

static void copyTo(char* dst, size_t cap, const std::string& s) {
    snprintf(dst, cap, "%s", s.c_str());
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

Model::Model(const std::wstring& dir) : exeDir(dir) {
    const std::wstring ini = exeDir + L"config.ini";
    std::string text;
    if (!platform::fileExists(ini)) writeIni(ini, base);
    if (readFile(ini, text)) parseLines(base, text, false);
    fromSettings(base);
    status = TX("ui.status.idle");
}

Model::~Model() {
    player_.reset();
    cancel_ = true;
    join();
}

void Model::join() {
    if (worker_.joinable()) worker_.join();
}

// --- Réglages ---------------------------------------------------------------------------------------------------
void Model::fromSettings(const Settings& real) {
    Settings s = real;
    applyOverrides(s, forced);  // champs gris : valeurs du shader
    f.w = s.width;
    f.h = s.height;
    copyTo(f.fps, sizeof(f.fps), U8(i18n::num(s.fps)));
    copyTo(f.dur, sizeof(f.dur), U8(i18n::num(s.duration)));
    copyTo(f.start, sizeof(f.start), U8(i18n::num(s.start)));
    f.crf = std::min(35, std::max(0, s.crf));
    f.preset = 6;
    for (int i = 0; i < 9; i++)
        if (s.preset == kPresetNames[i]) f.preset = i;
    f.codec = codecIndex(s.codec);
    f.container = containerIndex(s.container);
    f.depth = s.depth >= 10 ? 1 : 0;
    f.ss = std::min(4, std::max(1, s.ss)) - 1;
    copyTo(f.outdir, sizeof(f.outdir), U8(s.outdir));
    f.bitrate = s.bitrate;
    f.color = s.color == "bt709full" ? 1 : s.color == "srgb" ? 2 : s.color == "bt601" ? 3 : 0;
    f.blur = s.blur >= 16 ? 3 : s.blur >= 8 ? 2 : s.blur >= 2 ? 1 : 0;
    copyTo(f.loop, sizeof(f.loop), U8(i18n::num(s.loop)));
    copyTo(f.audio, sizeof(f.audio), U8(s.audio));
    copyTo(f.fadein, sizeof(f.fadein), U8(i18n::num(s.fadein)));
    copyTo(f.fadeout, sizeof(f.fadeout), U8(i18n::num(s.fadeout)));
    f.encoder = 0;
    const auto& encs = encoderList();
    for (size_t i = 0; i < encs.size(); i++)
        if (s.encoder == encs[i]) f.encoder = (int)i;
    // post-traitement : quantités par défaut, puis celles de s.post
    const auto& fx = postEffectInfos();
    for (size_t i = 0; i < fx.size() && i < 8; i++) {
        f.postOn[i] = false;
        f.postAmt[i] = (float)fx[i].def;
    }
    std::vector<PostEffect> on;
    std::wstring bad;
    if (parsePostEffects(real.post, on, bad))
        for (const PostEffect& e : on) {
            f.postOn[e.index] = true;
            f.postAmt[e.index] = (float)e.amount;
        }
    copyTo(f.lut, sizeof(f.lut), U8(real.lut));
    OutputFields o;
    outputFieldsFrom(real, o);
    f.alpha = o.alpha;
    f.hdr = o.hdr;
    f.scaleFilter = o.scaleFilter;
    copyTo(f.hdrPeak, sizeof(f.hdrPeak), U8(o.hdrPeak));
    copyTo(f.hdrFall, sizeof(f.hdrFall), U8(o.hdrFall));
    copyTo(f.hdrWhite, sizeof(f.hdrWhite), U8(o.hdrWhite));
    copyTo(f.scale, sizeof(f.scale), U8(o.scale));
    copyTo(f.interp, sizeof(f.interp), U8(o.interp));
    copyTo(f.loudnorm, sizeof(f.loudnorm), U8(o.loudnorm));
    copyTo(f.title, sizeof(f.title), U8(o.title));
    copyTo(f.credit, sizeof(f.credit), U8(o.credit));
    copyTo(f.titleDur, sizeof(f.titleDur), U8(o.titleDur));
    copyTo(f.name, sizeof(f.name), U8(o.name));
    copyTo(f.chunk, sizeof(f.chunk), U8(o.chunk));
    copyTo(f.pbo, sizeof(f.pbo), U8(o.pbo));
    copyTo(f.shutter, sizeof(f.shutter), U8(o.shutter));
    copyTo(f.variants, sizeof(f.variants), U8(o.variants));
    f.loopMode = o.loopMode;
    copyTo(f.loopFade, sizeof(f.loopFade), U8(o.loopFade));
    copyTo(f.timemap, sizeof(f.timemap), U8(o.timemap));
    copyTo(f.speed, sizeof(f.speed), U8(o.speed));
    copyTo(f.samples, sizeof(f.samples), U8(o.samples));
    f.exists = o.exists;
    f.endAction = o.endAction;
    f.metadata = o.metadata;
}

bool Model::toSettings(Settings& s, std::wstring& err) const {
    const bool fW = isForced("width"), fH = isForced("height");
    if ((!fW && (f.w < 16 || f.w > 16384)) || (!fH && (f.h < 16 || f.h > 16384))) { err = TX("error.res_invalid"); return false; }
    double fps = s.fps, dur = s.duration, start = s.start, lp = s.loop, fi = s.fadein, fo = s.fadeout;
    if (!isForced("fps")) {
        std::wstring ft = W(f.fps);
        for (auto& c : ft)
            if (c == L',') c = L'.';
        wchar_t* end = nullptr;
        fps = wcstod(ft.c_str(), &end);
        if (end == ft.c_str() || *end != 0 || fps <= 0 || fps > 240) { err = TX("error.fps_invalid"); return false; }
    }
    if (!isForced("duration") && (!parseSeconds(W(f.dur), dur) || dur <= 0)) { err = TX("error.duration_invalid"); return false; }
    if (!isForced("start") && !parseSeconds(W(f.start), start)) { err = TX("error.start_invalid"); return false; }
    if (!isForced("loop") && !parseSeconds(W(f.loop), lp)) { err = TX("error.loop_invalid"); return false; }
    if ((!isForced("fadein") && !parseSeconds(W(f.fadein), fi)) || (!isForced("fadeout") && !parseSeconds(W(f.fadeout), fo))) { err = TX("error.fade_invalid"); return false; }
    if (!isForced("bitrate") && (f.bitrate < 0 || f.bitrate > 500000)) { err = TX("error.bitrate_invalid"); return false; }
    if (!fW) s.width = f.w;
    if (!fH) s.height = f.h;
    s.fps = fps;
    s.duration = dur;
    s.start = start;
    s.loop = lp;
    s.fadein = fi;
    s.fadeout = fo;
    if (!isForced("crf")) s.crf = f.crf;
    if (!isForced("preset")) s.preset = kPresetNames[std::min(8, std::max(0, f.preset))];
    if (!isForced("codec")) s.codec = codecAt(f.codec).id;
    if (!isForced("container")) s.container = containerAt(f.container).id;
    if (!isForced("depth")) s.depth = f.depth == 1 ? 10 : 8;
    if (!isForced("ss")) s.ss = f.ss + 1;
    if (!isForced("outdir")) {
        std::wstring od = W(f.outdir);
        while (!od.empty() && (od.back() == L' ' || od.back() == L'/' || od.back() == L'\\')) od.pop_back();
        s.outdir = od;
    }
    if (!isForced("bitrate")) s.bitrate = f.bitrate;
    if (!isForced("color")) s.color = kColors[std::min(3, std::max(0, f.color))];
    if (!isForced("blur")) s.blur = kBlurs[std::min(3, std::max(0, f.blur))];
    if (!isForced("audio")) s.audio = W(f.audio);
    if (!isForced("encoder")) {
        const auto& encs = encoderList();
        s.encoder = encs[std::min((int)encs.size() - 1, std::max(0, f.encoder))];
    }
    s.post.clear();
    const auto& fx = postEffectInfos();
    for (size_t i = 0; i < fx.size() && i < 8; i++)
        if (f.postOn[i]) {
            char b[64];
            snprintf(b, sizeof(b), "%s%s:%g", s.post.empty() ? "" : ",", fx[i].name, (double)f.postAmt[i]);
            s.post += b;
        }
    s.lut = W(f.lut);
    OutputFields o;  // transparence, HDR, taille, interpolation, volume, texte : meme validation que la fenetre Windows
    o.alpha = f.alpha;
    o.hdr = f.hdr;
    o.scaleFilter = f.scaleFilter;
    o.hdrPeak = W(f.hdrPeak);
    o.hdrFall = W(f.hdrFall);
    o.hdrWhite = W(f.hdrWhite);
    o.scale = W(f.scale);
    o.interp = W(f.interp);
    o.loudnorm = W(f.loudnorm);
    o.title = W(f.title);
    o.credit = W(f.credit);
    o.titleDur = W(f.titleDur);
    o.name = W(f.name);
    o.chunk = W(f.chunk);
    o.pbo = W(f.pbo);
    o.shutter = W(f.shutter);
    o.variants = W(f.variants);
    o.loopMode = f.loopMode;
    o.loopFade = W(f.loopFade);
    o.timemap = W(f.timemap);
    o.speed = W(f.speed);
    o.samples = W(f.samples);
    o.exists = f.exists;
    o.endAction = f.endAction;
    o.metadata = f.metadata;
    return outputFieldsApply(o, s, err);
}

void Model::applyQuickPreset(int idx) {
    Settings s = base;
    std::wstring e;
    if (!toSettings(s, e)) s = base;
    applyBuiltinPreset(idx, s);
    base = s;  // les reglages que le shader impose gardent la valeur du preset pour les autres shaders
    fromSettings(s);
}

// Reglages imposes par le shader selectionne (// cle=valeur) : relus quand la selection change, puis une fois par seconde
void Model::refreshForced() {
    const std::wstring cur = paths.empty() ? L"" : paths[std::min(std::max(0, selected), (int)paths.size() - 1)];
    const auto now = std::chrono::steady_clock::now();
    if (cur == forcedPath_ && now - forcedChecked_ < std::chrono::seconds(1)) return;
    const bool pathChanged = cur != forcedPath_;
    forcedPath_ = cur;
    forcedChecked_ = now;
    if (pathChanged) syncFileScope(cur);  // autre shader : ses reglages propres (jobs/) prennent la place
    const std::vector<Override> next = cur.empty() ? std::vector<Override>() : shaderOverrides(cur);
    if (next == forced && !pathChanged) return;
    if (!pathChanged) {  // d'abord garder ce qui a ete saisi dans les champs libres (deja fait par syncFileScope quand la selection change)
        Settings s = base;
        std::wstring e;
        if (toSettings(s, e)) base = s;
    }
    forced = next;
    fromSettings(base);
}

// config.ini ne recoit jamais les reglages propres a un shader
void Model::save() const { writeIni(exeDir + L"config.ini", generalSettings()); }

Settings Model::generalSettings() const {
    Settings s = base;
    if (fileOn_) copyMainSettings(s, general_);  // les autres reglages (post-traitement, sortie...) restent communs
    return s;
}

void Model::flushFileScope() {
    if (!fileOn_) return;
    Settings s = base;
    std::wstring e;
    if (toSettings(s, e)) base = s;
    saveFileSettings(exeDir, filePath_, diffMainSettings(general_, base));
}

// Sort de l'etat « reglages propres » : base retrouve les reglages generaux (les champs communs modifies entre-temps sont gardes)
void Model::leaveFileScope(bool deleteSettings) {
    if (!fileOn_) return;
    if (deleteSettings) removeFileSettings(exeDir, filePath_);
    else saveFileSettings(exeDir, filePath_, diffMainSettings(general_, base));
    Settings back = base;
    copyMainSettings(back, general_);
    base = back;
    fileOn_ = false;
    filePath_.clear();
}

void Model::enterFileScope(const std::wstring& path, const std::vector<Override>& list) {
    general_ = base;
    applyFileOverrides(base, list);
    fileOn_ = true;
    filePath_ = path;
}

// La selection a change : ce qui a ete saisi pour l'ancien shader est garde, puis l'etat du nouveau (jobs/) est pris
void Model::syncFileScope(const std::wstring& cur) {
    if (cur == fileShown_) return;
    Settings s = base;
    std::wstring e;
    if (toSettings(s, e)) base = s;
    leaveFileScope(false);
    fileShown_ = cur;
    if (!cur.empty() && fileSettingsExist(exeDir, cur)) enterFileScope(cur, loadFileSettings(exeDir, cur));
}

void Model::toggleFileScope() {
    if (running() || paths.empty()) return;
    const std::wstring cur = paths[std::min(std::max(0, selected), (int)paths.size() - 1)];
    Settings s = base;
    std::wstring e;
    if (toSettings(s, e)) base = s;
    if (fileOn_) {
        leaveFileScope(true);  // rendre les reglages generaux et effacer ceux du shader
    } else {
        enterFileScope(cur, {});
        saveFileSettings(exeDir, cur, {});  // le fichier marque l'etat « reglages propres », meme sans difference
    }
    fileShown_ = cur;
    fromSettings(base);
}

void Model::writeScopeIntoShader() {
    if (!fileOn_ || running()) return;
    Settings s = base;
    std::wstring err;
    if (!toSettings(s, err)) {
        dialogTitle = TX("dialog.invalid_settings");
        dialogText = err;
        return;
    }
    base = s;
    const std::vector<Override> list = diffMainSettings(general_, base);
    dialogTitle = TX("dialog.profile_title");
    if (list.empty()) {
        dialogText = TX("dialog.nothing_to_write");
        return;
    }
    const std::wstring path = filePath_;
    if (!writeSettingsIntoShader(path, list, err)) {
        dialogText = err;
        return;
    }
    appendLog(TXF("log.settings_written", (int)list.size(), path.c_str()));
    dialogText = TXF("log.settings_written", (int)list.size(), path.c_str());
    leaveFileScope(true);  // ces reglages sont maintenant des commentaires du shader : le fichier jobs/ ne sert plus
    fileShown_ = path;
    forced.clear();        // relus par refreshForced : le shader impose maintenant ces valeurs
    forcedChecked_ = std::chrono::steady_clock::time_point{};
    fromSettings(base);
}

bool Model::loadProfile(const std::wstring& path) {
    std::string text;
    if (!readFile(path, text)) {
        dialogTitle = TX("dialog.profile_title");
        dialogText = TX("error.profile_read");
        return false;
    }
    Settings p;
    parseLines(p, text, false);
    fromSettings(p);
    appendLog(TXF("log.profile_loaded", path.c_str()));
    return true;
}

bool Model::saveProfile(const std::wstring& path) {
    Settings cur = base;
    std::wstring err;
    if (!toSettings(cur, err)) {
        dialogTitle = TX("dialog.invalid_settings");
        dialogText = err;
        return false;
    }
    writeIni(path, cur);
    appendLog(TXF("log.profile_saved", path.c_str()));
    return true;
}

// --- Liste ------------------------------------------------------------------------------------------------------
std::wstring Model::statusOf(int st) const {
    switch (st) {
        case ST_RUNNING: return TX("status.running");
        case ST_DONE: return TX("status.done");
        case ST_FAILED: return TX("status.failed");
        case ST_CANCELLED: return TX("status.cancelled");
        default: return TX("status.pending");
    }
}

void Model::addFiles(const std::vector<std::wstring>& files) {
    if (running()) return;
    for (const auto& file0 : files) {
        std::wstring ie;  // un .json exporte de Shadertoy est importe, puis le shader obtenu est ajoute
        const std::wstring file = resolveShaderInput(exeDir, file0, [this](const std::wstring& m) { appendLog(m); }, ie);
        if (file.empty()) {
            appendLog(TXF("log.error", ie.c_str()));
            continue;
        }
        bool dup = false;
        for (const auto& p : paths)
            if (p == file) { dup = true; break; }
        if (dup || !platform::fileExists(file)) continue;
        paths.push_back(file);
        state.push_back(ST_PENDING);
        stateText.push_back(statusOf(ST_PENDING));
    }
    saveQueue();
}

void Model::dropFiles(const std::vector<std::wstring>& files) {
    std::vector<std::wstring> shaders;
    for (const auto& f : files) {
        const DropKind k = classifyDropped(f);
        if (k == DropKind::Image || k == DropKind::Sound || k == DropKind::Video) pendingMedia.push_back(f);
        else shaders.push_back(f);
    }
    if (!shaders.empty()) addFiles(shaders);
    if (!pendingMedia.empty() && (paths.empty() || running())) {  // sans shader (ou pendant un rendu) il n'y a rien a modifier
        appendLog(TX("log.drop_media_no_shader"));
        pendingMedia.clear();
    }
}

// Ecrit `// iChannelN=...` (ou `// audio=...`) dans le shader selectionne : demande explicite de l'utilisateur (depose + choix du canal)
void Model::applyMedia(const std::wstring& file, int channel) {
    if (paths.empty() || running()) return;
    const std::wstring shader = paths[std::min(std::max(0, selected), (int)paths.size() - 1)];
    const Override ov = channel < 0 ? soundtrackOverride(shader, file) : channelOverride(shader, file, classifyDropped(file), channel);
    std::wstring err;
    if (!writeSettingsIntoShader(shader, {ov}, err)) {
        appendLog(TXF("log.error", err.c_str()));
        return;
    }
    appendLog(TXF("log.channel_written", W(ov.key).c_str(), W(ov.value).c_str(), shader.c_str()));
    forcedChecked_ = std::chrono::steady_clock::time_point{};  // le shader impose maintenant ce reglage : relu a la prochaine image
    if (playing()) {
        stopPlay();
        togglePlay();
    }
}

void Model::removeSelected() {
    if (running() || selected < 0 || selected >= (int)paths.size()) return;
    paths.erase(paths.begin() + selected);
    state.erase(state.begin() + selected);
    stateText.erase(stateText.begin() + selected);
    selected = std::min(selected, (int)paths.size() - 1);
    saveQueue();
}

// Le shader selectionne monte, descend ou passe en tete de file
void Model::moveSelected(int mode) {
    const int n = (int)paths.size();
    if (running() || selected < 0 || selected >= n) return;
    const int to = mode == 0 ? 0 : std::min(n - 1, std::max(0, selected + mode));
    if (to == selected) return;
    const std::wstring p = paths[selected];
    const int st = state[selected];
    const std::wstring tx = stateText[selected];
    paths.erase(paths.begin() + selected);
    state.erase(state.begin() + selected);
    stateText.erase(stateText.begin() + selected);
    paths.insert(paths.begin() + to, p);
    state.insert(state.begin() + to, st);
    stateText.insert(stateText.begin() + to, tx);
    selected = to;
    saveQueue();
}

void Model::setBatch(int n) {
    if (selected < 0 || selected >= (int)paths.size()) return;
    if (n > 0) batches[paths[selected]] = n;
    else batches.erase(paths[selected]);
    saveBatches(exeDir, batches);
}

void Model::resetSelectedState() {
    if (running() || selected < 0 || selected >= (int)paths.size()) return;
    state[selected] = ST_PENDING;
    stateText[selected] = statusOf(ST_PENDING);
    saveQueue();
}

void Model::clearList() {
    if (running()) return;
    paths.clear();
    state.clear();
    stateText.clear();
    selected = 0;
    saveQueue();
}

void Model::saveQueue() const {
    std::vector<std::wstring> pending;
    for (size_t i = 0; i < paths.size() && i < state.size(); i++)
        if (state[i] != ST_DONE) pending.push_back(paths[i]);
    ::saveQueue(exeDir, pending);
}

void Model::refreshTexts() {
    for (size_t i = 0; i < state.size(); i++)
        if (state[i] != ST_RUNNING) stateText[i] = statusOf(state[i]);
    if (!running()) status = TX("ui.status.idle");
}

// --- Journal ----------------------------------------------------------------------------------------------------
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

void Model::appendLog(const std::wstring& text) {
    int level = 0;
    if (hasAnyWord(text, "log.words.error")) level = 1;
    else if (hasAnyWord(text, "log.words.warn")) level = 2;
    else if (hasAnyWord(text, "log.words.ok")) level = 3;
    else if (text.rfind(L"[", 0) == 0 || text.rfind(APP_NAME_WSTR, 0) == 0) level = 4;
    log.push_back({text, level});
    if (log.size() > 5000) log.erase(log.begin(), log.begin() + 1000);
    logDirty = true;
    if (logHook) logHook(text);
}

// --- Rendu ------------------------------------------------------------------------------------------------------
void Model::post(Event&& e) {
    std::lock_guard<std::mutex> l(evMutex_);
    events_.push_back(std::move(e));
}

void Model::runJobsThread(std::vector<std::pair<int, std::wstring>> jobs) {
    auto postLog = [this](const std::wstring& s) {
        Event e;
        e.kind = Event::Log;
        e.text = s;
        post(std::move(e));
    };
    auto postFile = [this](int idx, int st) {
        Event e;
        e.kind = Event::File;
        e.idx = idx;
        e.n = st;
        post(std::move(e));
    };
    auto postDone = [this] {
        Event e;
        e.kind = Event::Done;
        post(std::move(e));
    };
    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        postLog(TXF("log.error", err.c_str()));
        destroyGLContext(gl);
        for (auto& j : jobs) postFile(j.first, ST_FAILED);
        postDone();
        return;
    }
    if (!previewRun) {
        const char* r = (const char*)glGetString(GL_RENDERER);
        postLog(TXF("log.gpu", W(r ? r : "?").c_str()));
    }
    Hooks H;
    H.log = postLog;
    H.cancel = &cancel_;
    H.pause = &pause_;
    H.snapshot = snapshotRun_ && imageKind_ <= 1;  // la planche-contact n'est pas une image fixe pour le moteur ; les kinds 3 et 4 ont leur propre chemin
    if (snapshotRun_ && imageKind_ == 1) H.thumbTime = -2;  // vignette : milieu de la plage
    if (snapshotRun_ && imageKind_ == 2) {
        H.sheetCols = sheetCols_;
        H.sheetRows = sheetRows_;
    }
    if (previewRun) {
        H.previewFrame = [this](int w, int h, std::vector<uint8_t>&& px) {
            Event e;
            e.kind = Event::Preview;
            e.w = w;
            e.h = h;
            e.px = std::move(px);
            post(std::move(e));
        };
        H.previewTime = previewTime_;
        std::wstring e;
        if (convertFile(jobs[0].second, exeDir, runSettings_, H, e) == Result::Failed) postLog(TXF("log.preview_error", e.c_str()));
        destroyGLContext(gl);
        postDone();
        return;
    }
    for (size_t i = 0; i < jobs.size(); i++) {
        const int idx = jobs[i].first;
        if (cancel_) {
            postFile(idx, ST_CANCELLED);
            continue;
        }
        postFile(idx, ST_RUNNING);
        postLog(wfmt(L"[%d/%d] ", (int)i + 1, (int)jobs.size()) + jobs[i].second);
        H.progress = [this, idx](long long n, long long total, double speed, double remaining) {
            Event e;
            e.kind = Event::Progress;
            e.idx = idx;
            e.n = n;
            e.total = total;
            e.speed = speed;
            e.remaining = remaining;
            post(std::move(e));
        };
        std::wstring e;
        Settings run = settingsForFile(runSettings_, exeDir, jobs[i].second);  // reglages generaux + ceux de ce shader
        if (snapshotRun_ && runStart_ >= 0) run.start = runStart_;
        Result r = Result::Ok;
        if (snapshotRun_ && imageKind_ == 3) {  // vignette intelligente : les trois images les plus riches
            std::vector<std::wstring> made;
            r = renderSmartThumbnails(jobs[i].second, exeDir, run, H, made, e) ? Result::Ok : cancel_ ? Result::Cancelled : Result::Failed;
        } else if (snapshotRun_ && imageKind_ == 4) {  // recherche de la periode du shader, sans rendu
            LoopFound lf;
            if (findLoop(jobs[i].second, exeDir, run, 0, H, lf, e)) {
                postLog(L"  " + describeLoop(lf));
                if (lf.found && !lf.isStatic) {
                    Event ev;
                    ev.kind = Event::Loop;
                    ev.speed = lf.period;
                    post(std::move(ev));
                }
            } else {
                r = cancel_ ? Result::Cancelled : Result::Failed;
            }
        } else {
            r = convertFile(jobs[i].second, exeDir, run, H, e);
        }
        if (r == Result::Failed) postLog(L"  " + TXF("log.error", e.c_str()));
        else if (r == Result::Cancelled) postLog(L"  " + TX("log.cancelled"));
        postFile(idx, r == Result::Ok ? ST_DONE : r == Result::Cancelled ? ST_CANCELLED : ST_FAILED);
    }
    destroyGLContext(gl);
    postDone();
}

bool Model::ffmpegMissing() const { return !ffmpegPresent(exeDir); }

void Model::startFfmpegDownload() {
    if (running()) return;
    join();
    cancel_ = false;
    previewRun = false;
    running_ = true;
    progress = 0;
    progressFailed = false;
    appendLog(TX("log.downloading_ffmpeg"));
    worker_ = std::thread([this] {
        std::wstring err;
        const bool ok = downloadFfmpeg(exeDir, [this](int pct, const std::wstring& msg) {
            Event e;
            e.kind = Event::Download;
            e.idx = pct;
            e.text = msg;
            post(std::move(e));
        }, &cancel_, err);
        Event e;
        e.kind = Event::DownloadDone;
        e.idx = ok ? 1 : 0;
        e.text = err;
        post(std::move(e));
    });
}

void Model::startJobs(bool snapshot, int imageKind, int cols, int rows, bool atPreview, bool selectedOnly, int batch) {
    if (running()) return;
    stopPlay();
    if (ffmpegMissing()) {
        if (ffmpegDownloadSupported()) {
            askDownload = true;
        } else {
            dialogTitle = TX("dialog.ffmpeg_missing_title");
            dialogText = TX("error.download_unsupported");
        }
        return;
    }
    std::vector<std::pair<int, std::wstring>> jobs;
    if (snapshot) {
        if (paths.empty()) return;
        const int sel = std::min(std::max(0, selected), (int)paths.size() - 1);
        jobs.push_back({sel, paths[sel]});
    } else {
        for (size_t i = 0; i < paths.size(); i++) {
            if (selectedOnly && (int)i != selected) continue;  // « rendre la selection »
            if (batch > 0 && batchOf(batches, paths[i]) != batch) continue;  // « rendre le lot N »
            if (!selectedOnly && batch == 0 && state[i] == ST_DONE) continue;
            state[i] = ST_PENDING;
            stateText[i] = statusOf(ST_PENDING);
            jobs.push_back({(int)i, paths[i]});
        }
    }
    if (jobs.empty()) return;
    Settings cur = base;
    std::wstring serr;
    if (!toSettings(cur, serr)) {
        dialogTitle = TX("dialog.invalid_settings");
        dialogText = serr;
        return;
    }
    base = cur;
    save();
    flushFileScope();  // les reglages propres au shader courant sont lus sur le disque par le thread de rendu
    join();
    runSettings_ = generalSettings();
    runStart_ = snapshot && atPreview ? cur.start + f.previewPos * cur.duration : -1;  // image PNG au temps de l'apercu
    cancel_ = false;
    pause_ = false;
    running_ = true;
    snapshotRun_ = snapshot;
    imageKind_ = snapshot ? imageKind : 0;
    sheetCols_ = cols;
    sheetRows_ = rows;
    previewRun = false;
    progress = 0;
    progressFailed = false;
    if (snapshot) snapshotPath_ = imageKind == 4 ? std::wstring() : outputPath(jobs[0].second, cur.outdir, imageKind == 1 || imageKind == 3 ? L"_thumb.png" : imageKind == 2 ? L"_sheet.png" : L".png");
    setStatus(TX(snapshot && imageKind == 4 ? "status.finding_loop" : snapshot ? "status.rendering_image" : "status.rendering"));
    worker_ = std::thread(&Model::runJobsThread, this, std::move(jobs));
}

void Model::runPreview() {
    if (running() || paths.empty()) return;
    stopPlay();
    Settings cur = base;
    std::wstring serr;
    if (!toSettings(cur, serr)) {
        dialogTitle = TX("dialog.invalid_settings");
        dialogText = serr;
        return;
    }
    base = cur;
    const int sel = std::min(std::max(0, selected), (int)paths.size() - 1);
    join();
    runSettings_ = cur;
    previewTime_ = cur.start + f.previewPos * cur.duration;
    previewRun = true;
    snapshotRun_ = false;
    cancel_ = false;
    running_ = true;
    setStatus(TX("status.rendering_preview"));
    worker_ = std::thread(&Model::runJobsThread, this, std::vector<std::pair<int, std::wstring>>{{sel, paths[sel]}});
}

void Model::stopPlay() { player_.reset(); }

void Model::togglePlay() {
    if (playing()) {
        stopPlay();
        return;
    }
    if (running() || paths.empty()) return;
    Settings cur = base;
    std::wstring serr;
    if (!toSettings(cur, serr)) {
        dialogTitle = TX("dialog.invalid_settings");
        dialogText = serr;
        return;
    }
    base = cur;
    const int sel = std::min(std::max(0, selected), (int)paths.size() - 1);
    playPath_ = paths[sel];
    playStart_ = cur.start;
    playDur_ = cur.duration > 0 ? cur.duration : 1;
    playPending_ = 0;
    playMeter.reset();
    player_.reset(new PreviewPlayer);
    player_->start(playPath_, exeDir, cur, cur.start + f.previewPos * cur.duration,
                   [this](int w, int h, std::vector<uint8_t>&& px, double t) {
                       playPending_++;
                       Event e;
                       e.kind = Event::Preview;
                       e.idx = 1;  // image du lecteur
                       e.w = w;
                       e.h = h;
                       e.speed = t;  // temps du shader
                       e.px = std::move(px);
                       post(std::move(e));
                   },
                   [this] { return playPending_.load() < 2; },  // l'interface n'a pas fini d'afficher : on saute l'image
                   [this](const std::wstring& s) {
                       Event e;
                       e.kind = Event::Log;
                       e.text = s;
                       post(std::move(e));
                   });
    setStatus(TX("status.playing"));
}

void Model::seekPlay() {
    if (player_ && player_->running()) player_->seek(playStart_ + f.previewPos * playDur_);
}

void Model::togglePause() {
    if (!running() || previewRun) return;
    pause_ = !pause_;
    setStatus(pause_ ? TX("status.paused") : TX("status.rendering"));
}

void Model::cancelRun() {
    if (!running()) return;
    cancel_ = true;
    pause_ = false;
    setStatus(TX("status.cancelling"));
}

void Model::openOutputFolder() {
    if (paths.empty()) return;
    const int sel = std::min(std::max(0, selected), (int)paths.size() - 1);
    Settings cur = base;
    std::wstring e;
    if (toSettings(cur, e)) base = cur;
    const std::wstring file = outputFile(paths[sel], base, false);
    if (platform::fileExists(file)) {
        platform::revealInFolder(file);
        return;
    }
    const std::wstring dir = file.substr(0, file.find_last_of(L"\\/"));
    platform::makeDir(dir);
    platform::openWithDefault(dir);
}

void Model::pump() {
    refreshForced();
    if (playing() && !paths.empty() && selected >= 0 && selected < (int)paths.size() && paths[selected] != playPath_) {
        stopPlay();  // autre shader selectionne : la lecture repart sur celui-la
        togglePlay();
    }
    std::vector<Event> evs;
    {
        std::lock_guard<std::mutex> l(evMutex_);
        evs.swap(events_);
    }
    for (auto& e : evs) {
        switch (e.kind) {
            case Event::Log:
                appendLog(e.text);
                break;
            case Event::Progress: {
                const int pct = (int)(e.n * 100 / std::max(1LL, e.total));
                progress = (float)e.n / (float)std::max(1LL, e.total);
                if (e.idx >= 0 && e.idx < (int)paths.size()) stateText[e.idx] = TXF("status.running_pct", pct);
                setStatus(TXF("status.progress", e.n, e.total, pct, i18n::fixed(e.speed, 1).c_str(), fmtTime(e.remaining).c_str()));
                break;
            }
            case Event::File:
                if (e.idx >= 0 && e.idx < (int)paths.size()) {
                    state[e.idx] = (int)e.n;
                    stateText[e.idx] = statusOf((int)e.n);
                    if (e.n == ST_RUNNING) progress = 0;
                    if (e.n == ST_DONE) progress = 1;
                    progressFailed = e.n == ST_FAILED;
                    saveQueue();
                }
                break;
            case Event::Preview:
                if (e.idx == 1) {  // image du lecteur : la position suit le temps du shader
                    if (playPending_ > 0) playPending_--;
                    playMeter.tick();
                    if (player_ && player_->running()) f.previewPos = (float)std::min(1.0, std::max(0.0, (e.speed - playStart_) / playDur_));
                }
                prevPixels = std::move(e.px);
                prevW = e.w;
                prevH = e.h;
                prevFresh = true;
                break;
            case Event::Loop:  // periode trouvee : elle devient la periode du reglage « Boucle » (la duree en sera un multiple entier)
                if (!isForced("loop")) copyTo(f.loop, sizeof(f.loop), U8(i18n::num(std::round(e.speed * 1000.0) / 1000.0)));
                break;
            case Event::Download:
                progress = (float)e.idx / 100.0f;
                setStatus(e.text);
                break;
            case Event::DownloadDone:
                join();
                running_ = false;
                if (e.idx) {
                    forgetFfmpegCapabilities();
                    formatsRevision++;
                    appendLog(TX("log.ffmpeg_installed"));
                    setStatus(TX("status.ffmpeg_installed"));
                } else {
                    appendLog(TXF("log.error", e.text.c_str()));
                    setStatus(TX("status.ffmpeg_failed"));
                    dialogTitle = TX("dialog.ffmpeg_missing_title");
                    dialogText = TXF("dialog.ffmpeg_failed", e.text.c_str());
                    progressFailed = true;
                }
                break;
            case Event::Done: {
                join();
                running_ = false;
                pause_ = false;
                if (previewRun) {
                    previewRun = false;
                    setStatus(TX("status.preview_ready"));
                    break;
                }
                int fails = 0, cancelled = 0;
                for (int st : state) {
                    if (st == ST_FAILED) fails++;
                    if (st == ST_CANCELLED) cancelled++;
                }
                if (cancelled) setStatus(TX("status.cancelled_done"));
                else if (fails) setStatus(TXF("status.finished_errors", fails));
                else setStatus(TX(snapshotRun_ ? "status.image_created" : "status.all_done"));
                progressFailed = fails > 0;
                attention = true;
                if (!fails && !cancelled && !snapshotRun_ && !paths.empty())  // action de fin facultative (config.ini : endaction)
                    performEndAction(base.endaction, outputFile(paths.back(), base, false), [this](const std::wstring& m) { appendLog(m); }, false);
                if (snapshotRun_ && !fails && !cancelled && !snapshotPath_.empty() && platform::fileExists(snapshotPath_)) platform::openWithDefault(snapshotPath_);
                snapshotRun_ = false;
                appendLog(L"");
                break;
            }
        }
    }
}

}  // namespace ui
