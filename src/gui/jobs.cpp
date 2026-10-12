// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "app.h"
#include "core/ffmpeg_download.h"
#include "core/i18n.h"
#include "core/gl.h"
#include "core/outputopts.h"
#include "core/params.h"
#include "core/render.h"
#include "core/util.h"
#include "ui_controls.h"
#include "../../res/resource.h"

static void postLog(const std::wstring& s) {
    PostMessageW(g.wnd, WM_APP_LOG, 0, (LPARAM) new std::wstring(s));
}

// Thread de rendu : cree son propre contexte OpenGL, traite le lot, le detruit.
static void runJobs(std::vector<std::pair<int, std::wstring>> jobs) {
    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        postLog(TXF("log.error", err.c_str()));
        destroyGLContext(gl);
        for (auto& j : jobs) PostMessageW(g.wnd, WM_APP_FILE, (WPARAM)j.first, ST_FAILED);
        PostMessageW(g.wnd, WM_APP_DONE, 0, 0);
        return;
    }
    if (!g.previewRun) {
        const char* r = (const char*)glGetString(GL_RENDERER);
        postLog(TXF("log.gpu", W(r ? r : "?").c_str()));
    }

    Hooks H;
    H.log = postLog;
    H.cancel = &g.cancel;
    H.pause = &g.pause;
    H.snapshot = g.snapshotRun && g.imageKind != 2;  // la planche-contact n'est pas une image fixe pour le moteur
    if (g.snapshotRun && g.imageKind == 1) H.thumbTime = -2;  // vignette : milieu de la plage
    if (g.snapshotRun && g.imageKind == 2) {
        H.sheetCols = g.sheetCols;
        H.sheetRows = g.sheetRows;
    }
    if (g.previewRun) {  // une seule image renvoyee a la fenetre, pas de ffmpeg
        H.previewFrame = [](int w, int h, std::vector<uint8_t>&& px) {
            PostMessageW(g.wnd, WM_APP_PREVIEW, 0, (LPARAM) new PreviewData{w, h, std::move(px)});
        };
        H.previewTime = g.previewTime;
        std::wstring e;
        if (convertFile(jobs[0].second, g.exeDir, g.base, H, e) == Result::Failed) postLog(TXF("log.preview_error", e.c_str()));
        destroyGLContext(gl);
        PostMessageW(g.wnd, WM_APP_DONE, 0, 0);
        return;
    }
    for (size_t i = 0; i < jobs.size(); i++) {
        const int idx = jobs[i].first;
        if (g.cancel) {
            PostMessageW(g.wnd, WM_APP_FILE, (WPARAM)idx, ST_CANCELLED);
            continue;
        }
        PostMessageW(g.wnd, WM_APP_FILE, (WPARAM)idx, ST_RUNNING);
        postLog(wfmt(L"[%d/%d] ", (int)i + 1, (int)jobs.size()) + jobs[i].second);
        H.progress = [idx](long long n, long long total, double speed, double remaining) {
            PostMessageW(g.wnd, WM_APP_PROGRESS, (WPARAM)idx, (LPARAM) new ProgressInfo{n, total, speed, remaining});
        };
        std::wstring e;
        Settings run = settingsForFile(g.runBase, g.exeDir, jobs[i].second);  // reglages generaux + ceux de ce shader
        if (g.snapshotRun && g.snapshotAt >= 0) run.start = g.snapshotAt;  // image PNG au temps de l'apercu
        Result r = convertFile(jobs[i].second, g.exeDir, run, H, e);
        if (r == Result::Failed) postLog(L"  " + TXF("log.error", e.c_str()));
        else if (r == Result::Cancelled) postLog(L"  " + TX("log.cancelled"));
        PostMessageW(g.wnd, WM_APP_FILE, (WPARAM)idx,
                     r == Result::Ok ? ST_DONE : r == Result::Cancelled ? ST_CANCELLED : ST_FAILED);
    }
    destroyGLContext(gl);
    PostMessageW(g.wnd, WM_APP_DONE, 0, 0);
}

// Bouton « Images » : une image PNG au temps de début, la vignette de couverture ou une planche-contact (grille au choix)
void showImageMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, TX("ui.menu.png").c_str());
    AppendMenuW(m, MF_STRING, 3, TX("ui.menu.png_now").c_str());
    AppendMenuW(m, MF_STRING, 2, TX("ui.menu.thumb").c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < kSheetGridCount; i++)
        AppendMenuW(m, MF_STRING, (UINT_PTR)(10 + i), TXF("ui.menu.sheet", kSheetGrids[i][0], kSheetGrids[i][1]).c_str());
    RECT r;
    GetWindowRect(g.bSnap, &r);
    const int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN, r.left, r.bottom, 0, g.wnd, nullptr);
    DestroyMenu(m);
    if (cmd == 1) startJobs(true, 0);
    else if (cmd == 2) startJobs(true, 1);
    else if (cmd == 3) startJobs(true, 0, 0, 0, true);
    else if (cmd >= 10 && cmd < 10 + kSheetGridCount) startJobs(true, 2, kSheetGrids[cmd - 10][0], kSheetGrids[cmd - 10][1]);
}

void promptDownload() {
    if (g.running) return;
    int r = MessageBoxW(g.wnd,
                        TX("dialog.ffmpeg_missing").c_str(), TX("dialog.ffmpeg_missing_title").c_str(),
                        MB_YESNO | MB_ICONQUESTION);
    if (r != IDYES) return;
    g.cancel = false;
    g.previewRun = false;
    g.running = true;
    updateButtons();
    SendMessageW(g.prog, PBM_SETPOS, 0, 0);
    appendLog(TX("log.downloading_ffmpeg"));
    g.worker = std::thread([] {
        std::wstring err;
        bool ok = downloadFfmpeg(g.exeDir, [](int pct, const std::wstring& msg) {
            PostMessageW(g.wnd, WM_APP_DL, (WPARAM)pct, (LPARAM) new std::wstring(msg));
        }, &g.cancel, err);
        PostMessageW(g.wnd, WM_APP_DLDONE, ok ? 1 : 0, (LPARAM) new std::wstring(err));
    });
}

void startJobs(bool snapshot, int imageKind, int cols, int rows, bool atPreview, bool selectedOnly, int batch) {
    stopPlayer();
    if (!g.running && !ffmpegPresent(g.exeDir)) {
        promptDownload();
        return;
    }
    if (g.running) return;
    std::vector<std::pair<int, std::wstring>> jobs;
    if (snapshot) {  // une image du fichier selectionne (sinon du premier)
        if (g.paths.empty()) return;
        int sel = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
        if (sel < 0) sel = 0;
        jobs.push_back({sel, g.paths[sel]});
    } else {
        std::vector<char> chosen(g.paths.size(), selectedOnly || batch > 0 ? 0 : 1);  // « rendre la selection » ou un lot : seulement ceux-la
        if (batch > 0)
            for (size_t k = 0; k < g.paths.size(); k++) chosen[k] = batchOf(g.batches, g.paths[k]) == batch;
        if (selectedOnly) {
            int k = -1;
            while ((k = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)k, LVNI_SELECTED)) != -1 && k < (int)chosen.size()) chosen[(size_t)k] = 1;
        }
        for (size_t i = 0; i < g.paths.size(); i++) {
            if (!chosen[i]) continue;
            if (!selectedOnly && batch == 0 && g.state[i] == ST_DONE) continue;
            g.state[i] = ST_PENDING;
            setItemText((int)i, 1, statusText(ST_PENDING));
            jobs.push_back({(int)i, g.paths[i]});
        }
    }
    if (jobs.empty()) return;
    Settings cur = g.base;
    std::wstring serr;
    if (!uiToSettings(cur, serr)) {
        MessageBoxW(g.wnd, serr.c_str(), TX("dialog.invalid_settings").c_str(), MB_ICONWARNING);
        return;
    }
    g.base = cur;
    saveSettings();
    flushFileScope();  // les reglages propres au shader courant sont lus sur le disque par le thread de rendu
    g.runBase = generalSettings();
    g.cancel = false;
    g.running = true;
    updateButtons();
    SendMessageW(g.prog, PBM_SETPOS, 0, 0);
    g.snapshotRun = snapshot;
    g.snapshotAt = snapshot && atPreview ? cur.start + SendMessageW(g.trkPrev, TBM_GETPOS, 0, 0) / (double)kPrevSteps * cur.duration : -1;
    g.imageKind = snapshot ? imageKind : 0;
    g.sheetCols = cols;
    g.sheetRows = rows;
    g.previewRun = false;
    if (snapshot) {
        g.snapshotPath = outputPath(jobs[0].second, cur.outdir, imageKind == 1 ? L"_thumb.png" : imageKind == 2 ? L"_sheet.png" : L".png");
    }
    setStatus(TX(snapshot ? "status.rendering_image" : "status.rendering"));
    g.worker = std::thread(runJobs, std::move(jobs));
}

static void refreshPrevMarks(double start, double dur);

void updatePrevLabel() {
    double start = 0, dur = 0;
    if (!parseSeconds(getText(g.edStart), start)) start = 0;
    if (!parseSeconds(getText(g.edDur), dur)) dur = 0;
    refreshPrevMarks(start, dur);
    double t = start + SendMessageW(g.trkPrev, TBM_GETPOS, 0, 0) / (double)kPrevSteps * dur;
    if (g.player && g.player->running())  // en lecture : numero de l'image et cadence reelle
        SetWindowTextW(g.lblPrev, TXF("ui.preview_play", i18n::fixed(t, 2).c_str(), (long long)std::llround((t - start) * g.base.fps), i18n::fixed(g.playMeter.fps(), 0).c_str()).c_str());
    else
        SetWindowTextW(g.lblPrev, TXF("ui.preview_time", i18n::fixed(t, 2).c_str()).c_str());
}

static int selectedOrFirst() {
    int sel = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    return sel < 0 ? 0 : sel;
}

// Images cles des parametres du shader selectionne, reperees sur le curseur de l'apercu (relues seulement si le code ou la plage change)
static void refreshPrevMarks(double start, double dur) {
    static std::string lastKey;
    if (!g.trkPrev) return;
    const std::wstring path = g.paths.empty() ? L"" : g.paths[(size_t)selectedOrFirst() < g.paths.size() ? (size_t)selectedOrFirst() : 0];
    const std::string key = U8(path) + "|" + std::to_string(start) + "|" + std::to_string(dur) + "|" + (path.empty() ? "" : fnvHex(collectShaderCode(path)));
    if (key == lastKey) return;
    lastKey = key;
    std::vector<float> marks;
    if (!path.empty() && dur > 0) {
        std::vector<ParamInfo> infos;
        std::vector<double> keys;
        std::wstring err;
        if (loadShaderParams(g.exeDir, path, infos, err, nullptr, &keys))
            for (double t : keys) {
                const double f = (t - start) / dur;
                if (f >= 0 && f <= 1) marks.push_back((float)f);
            }
    }
    SendMessageW(g.trkPrev, ui::STSL_SETMARKS, marks.size(), (LPARAM)marks.data());
}

// --- Lecture en temps reel -----------------------------------------------------------------------------------------
void updatePlayButton() {
    const bool on = g.player && g.player->running();
    SetWindowTextW(g.bPlay, TX(on ? "ui.btn.stop_play" : "ui.btn.play").c_str());
    ui::setButtonIcon(g.bPlay, on ? IC_STOP : IC_PLAY);
}

void stopPlayer() {
    g.player.reset();  // arrete et joint le thread
    updatePlayButton();
}

void startPlayer() {
    if (g.running || g.paths.empty()) return;
    stopPlayer();
    Settings cur = g.base;
    std::wstring serr;
    if (!uiToSettings(cur, serr)) {
        MessageBoxW(g.wnd, serr.c_str(), TX("dialog.invalid_settings").c_str(), MB_ICONWARNING);
        return;
    }
    g.base = cur;
    const int sel = selectedOrFirst();
    const double t0 = cur.start + SendMessageW(g.trkPrev, TBM_GETPOS, 0, 0) / (double)kPrevSteps * cur.duration;
    g.previewPending = 0;
    g.playMeter.reset();
    g.playerPath = g.paths[sel];
    g.player.reset(new PreviewPlayer);
    g.player->start(g.paths[sel], g.exeDir, cur, t0,
                    [](int w, int h, std::vector<uint8_t>&& px, double t) {
                        g.previewPending++;
                        PostMessageW(g.wnd, WM_APP_PREVIEW, 0, (LPARAM) new PreviewData{w, h, std::move(px), t});
                    },
                    [] { return g.previewPending.load() < 2; },  // l'interface n'a pas fini d'afficher : on saute l'image
                    postLog);
    updatePlayButton();
    setStatus(TX("status.playing"));
}

void followSelection() {
    if (g.player && g.player->running() && !g.paths.empty() && g.paths[selectedOrFirst()] != g.playerPath) startPlayer();
}

void runPreview() {
    if (g.running || g.paths.empty()) return;
    stopPlayer();
    Settings cur = g.base;
    std::wstring serr;
    if (!uiToSettings(cur, serr)) {
        MessageBoxW(g.wnd, serr.c_str(), TX("dialog.invalid_settings").c_str(), MB_ICONWARNING);
        return;
    }
    g.base = cur;
    int sel = selectedOrFirst();
    g.previewTime = cur.start + SendMessageW(g.trkPrev, TBM_GETPOS, 0, 0) / (double)kPrevSteps * cur.duration;
    g.previewRun = true;
    g.snapshotRun = false;
    g.cancel = false;
    g.running = true;
    updateButtons();
    setStatus(TX("status.rendering_preview"));
    std::vector<std::pair<int, std::wstring>> jobs{{sel, g.paths[sel]}};
    g.worker = std::thread(runJobs, std::move(jobs));
}

void openFolder() {
    if (g.paths.empty()) return;
    int sel = selectedOrFirst();
    Settings cur = g.base;
    std::wstring e;
    if (uiToSettings(cur, e)) g.base = cur;
    std::wstring file = outputFile(g.paths[sel], g.base, false);
    if (fileExists(file)) {
        ShellExecuteW(g.wnd, L"open", L"explorer.exe", (L"/select,\"" + file + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    std::wstring dir = file.substr(0, file.find_last_of(L"\\/"));
    CreateDirectoryW(dir.c_str(), nullptr);
    ShellExecuteW(g.wnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

