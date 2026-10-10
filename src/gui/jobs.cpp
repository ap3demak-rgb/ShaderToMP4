#include "app.h"
#include "core/ffmpeg_download.h"
#include "core/gl.h"
#include "core/render.h"
#include "core/util.h"

static void postLog(const std::wstring& s) {
    PostMessageW(g.wnd, WM_APP_LOG, 0, (LPARAM) new std::wstring(s));
}

// Thread de rendu : cree son propre contexte OpenGL, traite le lot, le detruit.
static void runJobs(std::vector<std::pair<int, std::wstring>> jobs) {
    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        postLog(L"ERREUR : " + err);
        destroyGLContext(gl);
        for (auto& j : jobs) PostMessageW(g.wnd, WM_APP_FILE, (WPARAM)j.first, ST_FAILED);
        PostMessageW(g.wnd, WM_APP_DONE, 0, 0);
        return;
    }
    if (!g.previewRun) {
        const char* r = (const char*)glGetString(GL_RENDERER);
        postLog(L"GPU : " + W(r ? r : "?"));
    }

    Hooks H;
    H.log = postLog;
    H.cancel = &g.cancel;
    H.pause = &g.pause;
    H.snapshot = g.snapshotRun;
    if (g.previewRun) {  // une seule image renvoyee a la fenetre, pas de ffmpeg
        H.previewFrame = [](int w, int h, std::vector<uint8_t>&& px) {
            PostMessageW(g.wnd, WM_APP_PREVIEW, 0, (LPARAM) new PreviewData{w, h, std::move(px)});
        };
        H.previewTime = g.previewTime;
        std::wstring e;
        if (convertFile(jobs[0].second, g.exeDir, g.base, H, e) == Result::Failed) postLog(L"ERREUR apercu : " + e);
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
        Result r = convertFile(jobs[i].second, g.exeDir, g.base, H, e);
        if (r == Result::Failed) postLog(L"  ERREUR : " + e);
        else if (r == Result::Cancelled) postLog(L"  Annule.");
        PostMessageW(g.wnd, WM_APP_FILE, (WPARAM)idx,
                     r == Result::Ok ? ST_DONE : r == Result::Cancelled ? ST_CANCELLED : ST_FAILED);
    }
    destroyGLContext(gl);
    PostMessageW(g.wnd, WM_APP_DONE, 0, 0);
}

void promptDownload() {
    if (g.running) return;
    int r = MessageBoxW(g.wnd,
                        L"ffmpeg.exe est introuvable (il encode les videos).\n\n"
                        L"Le telecharger automatiquement (environ 80 Mo) depuis gyan.dev ?\n"
                        L"Le fichier est verifie par son empreinte SHA-256 puis installe a cote de ShaderToMP4.exe.",
                        L"ffmpeg manquant", MB_YESNO | MB_ICONQUESTION);
    if (r != IDYES) return;
    g.cancel = false;
    g.previewRun = false;
    g.running = true;
    updateButtons();
    SendMessageW(g.prog, PBM_SETPOS, 0, 0);
    appendLog(L"Telechargement de ffmpeg...");
    g.worker = std::thread([] {
        std::wstring err;
        bool ok = downloadFfmpeg(g.exeDir, [](int pct, const std::wstring& msg) {
            PostMessageW(g.wnd, WM_APP_DL, (WPARAM)pct, (LPARAM) new std::wstring(msg));
        }, &g.cancel, err);
        PostMessageW(g.wnd, WM_APP_DLDONE, ok ? 1 : 0, (LPARAM) new std::wstring(err));
    });
}

void startJobs(bool snapshot) {
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
        for (size_t i = 0; i < g.paths.size(); i++) {
            if (g.state[i] == ST_DONE) continue;
            g.state[i] = ST_PENDING;
            setItemText((int)i, 1, statusText(ST_PENDING));
            jobs.push_back({(int)i, g.paths[i]});
        }
    }
    if (jobs.empty()) return;
    Settings cur = g.base;
    std::wstring serr;
    if (!uiToSettings(cur, serr)) {
        MessageBoxW(g.wnd, serr.c_str(), L"Réglages invalides", MB_ICONWARNING);
        return;
    }
    g.base = cur;
    saveSettings();
    g.cancel = false;
    g.running = true;
    updateButtons();
    SendMessageW(g.prog, PBM_SETPOS, 0, 0);
    g.snapshotRun = snapshot;
    g.previewRun = false;
    if (snapshot) {
        g.snapshotPath = outputPath(jobs[0].second, cur.outdir, L".png");
    }
    setStatus(snapshot ? L"Rendu de l'image..." : L"Rendu en cours...");
    g.worker = std::thread(runJobs, std::move(jobs));
}

void updatePrevLabel() {
    double start = 0, dur = 0;
    if (!parseSeconds(getText(g.edStart), start)) start = 0;
    if (!parseSeconds(getText(g.edDur), dur)) dur = 0;
    double t = start + SendMessageW(g.trkPrev, TBM_GETPOS, 0, 0) / 100.0 * dur;
    SetWindowTextW(g.lblPrev, wfmt(L"Apercu : t = %.2f s", t).c_str());
}

static int selectedOrFirst() {
    int sel = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    return sel < 0 ? 0 : sel;
}

void runPreview() {
    if (g.running || g.paths.empty()) return;
    Settings cur = g.base;
    std::wstring serr;
    if (!uiToSettings(cur, serr)) {
        MessageBoxW(g.wnd, serr.c_str(), L"Réglages invalides", MB_ICONWARNING);
        return;
    }
    g.base = cur;
    int sel = selectedOrFirst();
    g.previewTime = cur.start + SendMessageW(g.trkPrev, TBM_GETPOS, 0, 0) / 100.0 * cur.duration;
    g.previewRun = true;
    g.snapshotRun = false;
    g.cancel = false;
    g.running = true;
    updateButtons();
    setStatus(L"Rendu de l'apercu...");
    std::vector<std::pair<int, std::wstring>> jobs{{sel, g.paths[sel]}};
    g.worker = std::thread(runJobs, std::move(jobs));
}

void openFolder() {
    if (g.paths.empty()) return;
    int sel = selectedOrFirst();
    Settings cur = g.base;
    std::wstring e;
    if (uiToSettings(cur, e)) g.base = cur;
    std::wstring file = outputPath(g.paths[sel], g.base.outdir, L"." + W(g.base.container));
    if (fileExists(file)) {
        ShellExecuteW(g.wnd, L"open", L"explorer.exe", (L"/select,\"" + file + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    std::wstring dir = file.substr(0, file.find_last_of(L"\\/"));
    CreateDirectoryW(dir.c_str(), nullptr);
    ShellExecuteW(g.wnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

