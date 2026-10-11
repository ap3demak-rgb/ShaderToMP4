// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "ui_imgui/model.h"
#include "core/ffmpeg_download.h"
#include "core/formats.h"
#include "core/presets.h"
#include "core/queue.h"
#include "core/gl.h"
#include "core/i18n.h"
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
    cancel_ = true;
    join();
}

void Model::join() {
    if (worker_.joinable()) worker_.join();
}

// --- Réglages ---------------------------------------------------------------------------------------------------
void Model::fromSettings(const Settings& s) {
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
}

bool Model::toSettings(Settings& s, std::wstring& err) const {
    if (f.w < 16 || f.w > 16384 || f.h < 16 || f.h > 16384) { err = TX("error.res_invalid"); return false; }
    std::wstring ft = W(f.fps);
    for (auto& c : ft)
        if (c == L',') c = L'.';
    wchar_t* end = nullptr;
    const double fps = wcstod(ft.c_str(), &end);
    if (end == ft.c_str() || *end != 0 || fps <= 0 || fps > 240) { err = TX("error.fps_invalid"); return false; }
    double dur = 0, start = 0, lp = 0, fi = 0, fo = 0;
    if (!parseSeconds(W(f.dur), dur) || dur <= 0) { err = TX("error.duration_invalid"); return false; }
    if (!parseSeconds(W(f.start), start)) { err = TX("error.start_invalid"); return false; }
    if (!parseSeconds(W(f.loop), lp)) { err = TX("error.loop_invalid"); return false; }
    if (!parseSeconds(W(f.fadein), fi) || !parseSeconds(W(f.fadeout), fo)) { err = TX("error.fade_invalid"); return false; }
    if (f.bitrate < 0 || f.bitrate > 500000) { err = TX("error.bitrate_invalid"); return false; }
    s.width = f.w;
    s.height = f.h;
    s.fps = fps;
    s.duration = dur;
    s.start = start;
    s.crf = f.crf;
    s.preset = kPresetNames[std::min(8, std::max(0, f.preset))];
    s.codec = codecAt(f.codec).id;
    s.container = containerAt(f.container).id;
    s.depth = f.depth == 1 ? 10 : 8;
    s.ss = f.ss + 1;
    std::wstring od = W(f.outdir);
    while (!od.empty() && (od.back() == L' ' || od.back() == L'/' || od.back() == L'\\')) od.pop_back();
    s.outdir = od;
    s.bitrate = f.bitrate;
    s.loop = lp;
    s.fadein = fi;
    s.fadeout = fo;
    s.color = kColors[std::min(3, std::max(0, f.color))];
    s.blur = kBlurs[std::min(3, std::max(0, f.blur))];
    s.audio = W(f.audio);
    const auto& encs = encoderList();
    s.encoder = encs[std::min((int)encs.size() - 1, std::max(0, f.encoder))];
    return true;
}

void Model::applyQuickPreset(int idx) {
    Settings s = base;
    std::wstring e;
    if (!toSettings(s, e)) s = base;
    applyBuiltinPreset(idx, s);
    fromSettings(s);
}

void Model::save() const { writeIni(exeDir + L"config.ini", base); }

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
    for (const auto& file : files) {
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

void Model::removeSelected() {
    if (running() || selected < 0 || selected >= (int)paths.size()) return;
    paths.erase(paths.begin() + selected);
    state.erase(state.begin() + selected);
    stateText.erase(stateText.begin() + selected);
    selected = std::min(selected, (int)paths.size() - 1);
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
    H.snapshot = snapshotRun_;
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
        const Result r = convertFile(jobs[i].second, exeDir, runSettings_, H, e);
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

void Model::startJobs(bool snapshot) {
    if (running()) return;
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
            if (state[i] == ST_DONE) continue;
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
    join();
    runSettings_ = cur;
    cancel_ = false;
    pause_ = false;
    running_ = true;
    snapshotRun_ = snapshot;
    previewRun = false;
    progress = 0;
    progressFailed = false;
    if (snapshot) snapshotPath_ = outputPath(jobs[0].second, cur.outdir, L".png");
    setStatus(TX(snapshot ? "status.rendering_image" : "status.rendering"));
    worker_ = std::thread(&Model::runJobsThread, this, std::move(jobs));
}

void Model::runPreview() {
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
                prevPixels = std::move(e.px);
                prevW = e.w;
                prevH = e.h;
                prevFresh = true;
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
                if (snapshotRun_ && !fails && !cancelled && platform::fileExists(snapshotPath_)) platform::openWithDefault(snapshotPath_);
                snapshotRun_ = false;
                appendLog(L"");
                break;
            }
        }
    }
}

}  // namespace ui
