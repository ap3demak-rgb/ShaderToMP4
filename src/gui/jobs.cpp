// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "app.h"
#include "core/ffmpeg_download.h"
#include "core/i18n.h"
#include "core/gl.h"
#include "core/outputopts.h"
#include "core/params.h"
#include "core/proext.h"
#include "core/formats.h"
#include "core/publish.h"
#include "core/webexport.h"
#include "core/compare.h"
#include "core/looptools.h"
#include "core/quality.h"
#include "core/render.h"
#include "core/vr.h"
#include "core/util.h"
#include "platform/platform.h"
#include "ui_controls.h"
#include "../../res/resource.h"

std::wstring jobLogPath(const std::wstring& shaderPath) {
    std::wstring name = fileSettingsPath(g.exeDir, shaderPath);  // .../jobs/<nom>-<empreinte>.ini
    name = name.substr(name.find_last_of(L"/\\") + 1);
    return g.exeDir + L"jobs/logs/" + name.substr(0, name.size() - 4) + L".log";
}

// Le plan suit la liste : les travaux en attente, dans l'ordre des priorites puis de la liste (appele par la fenetre quand elle change pendant un rendu)
void rebuildPlan() {
    if (!g.running || g.previewRun || g.snapshotRun) return;
    std::vector<std::pair<int, std::wstring>> list;
    {
        std::lock_guard<std::mutex> lk(g.jobMutex);
        for (size_t i = 0; i < g.paths.size(); i++)
            if (g.state[i] == ST_PENDING && g.runIds.count(g.ids[i]) && !g.startedIds.count(g.ids[i])) list.push_back({g.ids[i], g.paths[i]});
        std::stable_sort(list.begin(), list.end(), [](const std::pair<int, std::wstring>& a, const std::pair<int, std::wstring>& b) {
            const auto pa = g.priority.find(a.second), pb = g.priority.find(b.second);
            return (pa == g.priority.end() ? 0 : pa->second) > (pb == g.priority.end() ? 0 : pb->second);
        });
        for (const auto& j : list)
            if (!g.jobFrames.count(j.first)) {  // travail repris apres une suspension : ses images ne sont pas encore connues
                const Settings js = settingsForFile(g.runBase, g.exeDir, j.second);
                g.jobFrames[j.first] = std::max<long long>(1, std::llround(js.duration * js.fps));
            }
        g.plan = list;
    }
}

// Fin estimee de toute la file : temps restant du travail en cours + images des travaux a venir a la vitesse mesuree
std::wstring queueEtaText(double speed, double remaining) {
    if (g.snapshotRun || g.previewRun || speed <= 0) return L"";
    long long frames = 0;
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lk(g.jobMutex);
        for (const auto& j : g.plan) {
            const auto it = g.jobFrames.find(j.first);
            frames += it == g.jobFrames.end() ? 0 : it->second;
            count++;
        }
    }
    if (count == 0) return L"";
    const double sec = remaining + (double)frames / speed;
    const time_t endAt = time(nullptr) + (time_t)sec;
    struct tm tmv;
    localtime_s(&tmv, &endAt);
    wchar_t clock[16];
    wcsftime(clock, 16, L"%H:%M", &tmv);
    return TXF("status.queue_eta", (int)count, fmtTime(sec).c_str(), clock);
}

static void postLog(const std::wstring& s) {
    PostMessageW(g.wnd, WM_APP_LOG, 0, (LPARAM) new std::wstring(s));
}

// Temps GPU moyens par image (image, tampons, post-traitement) mesures pendant le rendu, montres dans la barre d'etat
static std::atomic<double> gGpu[3];

// Thread de rendu : cree son propre contexte OpenGL, traite le lot, le detruit.
static void runJobs(std::vector<std::pair<int, std::wstring>> jobs, bool dynamic) {
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
    FILE* jobLog = nullptr;  // journal du travail en cours (jobs/logs/), ecrit par ce thread : chaque ligne va au bon travail
    H.log = [&jobLog](const std::wstring& s) {
        postLog(s);
        if (jobLog) {
            const std::string line = U8(s) + "\r\n";
            fwrite(line.data(), 1, line.size(), jobLog);
            fflush(jobLog);
        }
    };
    H.cancel = &g.cancel;
    H.pause = &g.pause;
    H.snapshot = g.snapshotRun && g.imageKind <= 1;  // la planche-contact n'est pas une image fixe pour le moteur ; les kinds 3 et 4 ont leur propre chemin
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
    double estLow = 0, estMid = 0, estHigh = 0;  // estimation de la file : somme des fichiers
    size_t taken = 0;
    for (;;) {
        std::pair<int, std::wstring> job;  // identifiant du travail dans la liste, chemin du shader
        size_t after = 0;                  // travaux qui restent apres celui-ci
        if (dynamic) {  // la liste peut etre reordonnee pendant le rendu : le travail suivant est pris dans le plan que la fenetre tient a jour
            std::lock_guard<std::mutex> lk(g.jobMutex);
            if (g.plan.empty()) break;
            job = g.plan.front();
            g.plan.erase(g.plan.begin());
            g.startedIds.insert(job.first);
            after = g.plan.size();
        } else {
            if (taken >= jobs.size()) break;
            job = jobs[taken];
            after = jobs.size() - taken - 1;
        }
        taken++;
        const int idx = job.first;
        const std::wstring& shader = job.second;
        if (g.cancel) {
            PostMessageW(g.wnd, WM_APP_FILE, (WPARAM)idx, ST_CANCELLED);
            continue;
        }
        PostMessageW(g.wnd, WM_APP_FILE, (WPARAM)idx, ST_RUNNING);
        if (jobLog) fclose(jobLog);
        jobLog = nullptr;
        if (!g.snapshotRun) {
            platform::makeDir(g.exeDir + L"jobs");
            platform::makeDir(g.exeDir + L"jobs/logs");
            jobLog = platform::openFile(jobLogPath(shader), "wb");
        }
        postLog(wfmt(L"[%d/%d] ", (int)taken, (int)(taken + after)) + shader);
        gGpu[0] = gGpu[1] = gGpu[2] = 0;
        H.gpuTimes = [](double image, double buffers, double post) {
            gGpu[0] = image;
            gGpu[1] = buffers;
            gGpu[2] = post;
        };
        H.progress = [idx](long long n, long long total, double speed, double remaining) {
            PostMessageW(g.wnd, WM_APP_PROGRESS, (WPARAM)idx, (LPARAM) new ProgressInfo{n, total, speed, remaining, gGpu[0].load(), gGpu[1].load(), gGpu[2].load()});
        };
        std::wstring e;
        Settings run = settingsForFile(g.runBase, g.exeDir, shader);  // reglages generaux + ceux de ce shader
        if (g.snapshotRun && g.snapshotAt >= 0) run.start = g.snapshotAt;  // image PNG au temps de l'apercu
        Result r = Result::Ok;
        if (g.snapshotRun && g.imageKind == 3) {  // vignette intelligente : les trois images les plus riches
            std::vector<std::wstring> made;
            r = renderSmartThumbnails(shader, g.exeDir, run, H, made, e) ? Result::Ok : g.cancel ? Result::Cancelled : Result::Failed;
            if (r == Result::Ok)  // les trois propositions s'ouvrent ensemble : on choisit en les voyant
                for (const std::wstring& f : made) ShellExecuteW(nullptr, L"open", f.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else if (g.snapshotRun && g.imageKind == 7) {  // controle qualite : flashs, valeurs invalides, scintillement, aliasing, precision
            CheckReport cr;
            Hooks Hc = H;
            Hc.previewFrame = [](int w, int h, std::vector<uint8_t>&& px) { PostMessageW(g.wnd, WM_APP_PREVIEW, 0, (LPARAM) new PreviewData{w, h, std::move(px)}); };
            if (runChecks(shader, g.exeDir, run, CheckOptions(), Hc, cr, e)) {
                const std::string text = checkReportText(cr);
                size_t pos = 0;
                while (pos < text.size()) {
                    size_t nl = text.find('\n', pos);
                    if (nl == std::string::npos) nl = text.size();
                    if (nl > pos) postLog(L"  " + W(text.substr(pos, nl - pos)));
                    pos = nl + 1;
                }
                if (writeCheckReport(cr, outputPath(shader, run.outdir, L""))) postLog(L"  " + TXF("log.report_written", (outputPath(shader, run.outdir, L"") + L".check.txt").c_str()));
            } else {
                r = g.cancel ? Result::Cancelled : Result::Failed;
            }
        } else if (g.snapshotRun && g.imageKind == 8) {  // estimation du temps de rendu
            Estimate est;
            if (estimateRender(shader, g.exeDir, run, H, est, e)) {
                estLow += est.secLow;
                estMid += est.secMid;
                estHigh += est.secHigh;
                const std::wstring text = describeEstimate(est);
                size_t pos = 0;
                while (pos < text.size()) {
                    size_t nl = text.find(L'\n', pos);
                    if (nl == std::wstring::npos) nl = text.size();
                    if (nl > pos) postLog(L"  " + text.substr(pos, nl - pos));
                    pos = nl + 1;
                }
            } else {
                r = g.cancel ? Result::Cancelled : Result::Failed;
            }
        } else if (g.snapshotRun && g.imageKind == 9) {  // version avant / apres edition : une image de chaque au temps de l'apercu
            CompareData* cd = new CompareData{0, 0, {}, {}};
            if (compareVersions(shader, g.cmpBeforeText, g.cmpAfterText, g.exeDir, run, run.start, cd->before, cd->after, cd->w, cd->h, e)) {
                PostMessageW(g.wnd, WM_APP_COMPARE, 0, (LPARAM)cd);
            } else {
                delete cd;
                r = Result::Failed;
            }
        } else if (g.imageKind == 10) {  // page HTML autonome a cote du shader, sans rendu
            std::wstring page;
            if (exportHtmlBeside(shader, g.exeDir, run, page, e)) postLog(L"  " + TXF("log.html_written", page.c_str()));
            else r = Result::Failed;
        } else if (g.imageKind == 11) {  // controle d'integrite de la video deja produite
            std::vector<std::wstring> lines;
            bool same = true;
            const std::wstring video = outputFile(shader, run, false);
            if (!fileExists(video)) {
                e = TXF("error.verify_read", video.c_str());
                r = Result::Failed;
            } else if (verifyLines(findFfmpeg(g.exeDir), video, lines, same, e)) {
                for (const std::wstring& l : lines) postLog(L"  " + l);
                if (!same) r = Result::Failed, e = TX("log.report.status_failed");
            } else {
                r = Result::Failed;
            }
        } else if (g.snapshotRun && g.imageKind == 6) {  // carte de chaleur du cout GPU par zone
            HeatStats hs;
            std::wstring heatFile;
            Hooks Hh = H;  // la carte s'affiche dans l'apercu
            Hh.previewFrame = [](int w, int h, std::vector<uint8_t>&& px) { PostMessageW(g.wnd, WM_APP_PREVIEW, 0, (LPARAM) new PreviewData{w, h, std::move(px)}); };
            if (renderHeatmap(shader, g.exeDir, run, 16, 9, -1, Hh, hs, heatFile, e)) {
                postLog(L"  " + describeHeat(hs));
            } else {
                r = g.cancel ? Result::Cancelled : Result::Failed;
            }
        } else if (g.snapshotRun && g.imageKind == 5) {  // les six faces d'une cubemap (shader mainVR), carrees
            Settings sq = run;
            sq.width = sq.height = std::min(run.width, run.height);
            std::vector<std::wstring> made;
            r = renderCubeFaces(shader, g.exeDir, sq, H, made, e) ? Result::Ok : g.cancel ? Result::Cancelled : Result::Failed;
        } else if (g.snapshotRun && g.imageKind == 4) {  // recherche de la periode du shader, sans rendu
            LoopFound lf;
            if (findLoop(shader, g.exeDir, run, 0, H, lf, e)) {
                postLog(L"  " + describeLoop(lf));
                if (lf.found && !lf.isStatic) PostMessageW(g.wnd, WM_APP_LOOP, 0, (LPARAM) new double(lf.period));
            } else {
                r = g.cancel ? Result::Cancelled : Result::Failed;
            }
        } else {
            std::wstring produced;
            H.output = [&produced](const std::wstring& o) { produced = o; };
            const auto t0 = std::chrono::steady_clock::now();
            r = convertFile(shader, g.exeDir, run, H, e);
            H.output = nullptr;
            appendHistory(g.exeDir, makeHistoryEntry(shader, run, r == Result::Ok ? produced : std::wstring(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                                                     r == Result::Ok ? "ok" : r == Result::Cancelled ? "cancelled" : "failed"));
        }
        if (r == Result::Failed) H.log(L"  " + TXF("log.error", e.c_str()));  // aussi dans le journal du travail
        else if (r == Result::Cancelled) H.log(L"  " + TX("log.cancelled"));
        if (jobLog) fclose(jobLog);
        jobLog = nullptr;
        PostMessageW(g.wnd, WM_APP_FILE, (WPARAM)idx,
                     r == Result::Ok ? ST_DONE : r == Result::Cancelled ? ST_CANCELLED : ST_FAILED);
    }
    if (g.snapshotRun && g.imageKind == 8 && jobs.size() > 1 && estMid > 0)
        postLog(L"  " + TXF("log.estimate.total", formatDuration(estMid).c_str(), formatDuration(estLow).c_str(), formatDuration(estHigh).c_str()));
    destroyGLContext(gl);
    PostMessageW(g.wnd, WM_APP_DONE, 0, 0);
}

// Bouton « Images » : une image PNG au temps de début, la vignette de couverture ou une planche-contact (grille au choix)
void showImageMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, TX("ui.menu.png").c_str());
    AppendMenuW(m, MF_STRING, 3, TX("ui.menu.png_now").c_str());
    AppendMenuW(m, MF_STRING, 2, TX("ui.menu.thumb").c_str());
    AppendMenuW(m, MF_STRING, 4, TX("ui.menu.thumb_smart").c_str());
    AppendMenuW(m, MF_STRING, 5, TX("ui.menu.cubefaces").c_str());
    AppendMenuW(m, MF_STRING, 6, TX("ui.menu.heatmap").c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 7, TX("ui.menu.check").c_str());
    AppendMenuW(m, MF_STRING, 8, TX("ui.menu.estimate").c_str());
    AppendMenuW(m, MF_STRING, 9, TX("ui.menu.exporthtml").c_str());
    AppendMenuW(m, MF_STRING, 12, TX("ui.menu.verify").c_str());
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
    else if (cmd == 4) startJobs(true, 3);
    else if (cmd == 5) startJobs(true, 5);
    else if (cmd == 6) startJobs(true, 6);
    else if (cmd == 7) startJobs(true, 7);
    else if (cmd == 8) startJobs(true, 8);
    else if (cmd == 9) startJobs(true, 10);
    else if (cmd == 12) startJobs(true, 11);
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
    if (snapshot && imageKind == 8) {  // estimation : tous les fichiers pas encore faits, le total est donne a la fin
        for (size_t i = 0; i < g.paths.size(); i++)
            if (g.state[i] != ST_DONE) jobs.push_back({g.ids[i], g.paths[i]});
    } else if (snapshot) {  // une image du fichier selectionne (sinon du premier)
        if (g.paths.empty()) return;
        int sel = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
        if (sel < 0) sel = 0;
        jobs.push_back({g.ids[(size_t)sel], g.paths[(size_t)sel]});
    } else {
        std::vector<char> chosen(g.paths.size(), selectedOnly || batch > 0 ? 0 : 1);  // « rendre la selection » ou un lot : seulement ceux-la
        if (batch > 0)
            for (size_t k = 0; k < g.paths.size(); k++) chosen[k] = batchOf(g.batches, g.paths[k]) == batch;
        if (selectedOnly) {
            int k = -1;
            while ((k = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)k, LVNI_SELECTED)) != -1 && k < (int)chosen.size()) chosen[(size_t)k] = 1;
        }
        std::set<int> chosenIds;
        for (size_t i = 0; i < g.paths.size(); i++) {
            if (!chosen[i]) continue;
            chosenIds.insert(g.ids[i]);
            if (g.state[i] == ST_HELD) continue;  // un travail suspendu attend qu'on le reprenne
            if (!selectedOnly && batch == 0 && g.state[i] == ST_DONE) continue;
            g.state[i] = ST_PENDING;
            setItemText((int)i, 1, statusText(ST_PENDING));
            jobs.push_back({g.ids[i], g.paths[i]});
        }
        std::stable_sort(jobs.begin(), jobs.end(), [](const std::pair<int, std::wstring>& a, const std::pair<int, std::wstring>& b) {
            const auto pa = g.priority.find(a.second), pb = g.priority.find(b.second);
            return (pa == g.priority.end() ? 0 : pa->second) > (pb == g.priority.end() ? 0 : pb->second);  // haute priorite d'abord, l'ordre de la liste sinon
        });
        {
            std::lock_guard<std::mutex> lk(g.jobMutex);
            g.runIds = chosenIds;
            g.startedIds.clear();
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
    g.runStart = std::chrono::steady_clock::now();
    g.lastSpeed = 0;
    if (!snapshot) {  // plan de la file et images de chaque travail pour l'estimation de la fin
        std::lock_guard<std::mutex> lk(g.jobMutex);
        g.plan = jobs;
        g.jobFrames.clear();
        for (const auto& j : jobs) {
            const Settings js = settingsForFile(g.runBase, g.exeDir, j.second);
            g.jobFrames[j.first] = std::max<long long>(1, std::llround(js.duration * js.fps));
        }
    }
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
        g.snapshotPath = imageKind == 3 || imageKind == 4 || imageKind == 5 || imageKind == 6 || imageKind >= 7 ? std::wstring() : outputPath(jobs[0].second, cur.outdir, imageKind == 1 || imageKind == 3 ? L"_thumb.png" : imageKind == 2 ? L"_sheet.png" : imageKind == 6 ? L"_heat.png" : L".png");
    }
    setStatus(TX(snapshot && imageKind == 4 ? "status.finding_loop" : snapshot && imageKind == 7 ? "status.checking" : snapshot && imageKind == 8 ? "status.estimating" : snapshot && imageKind == 9 ? "status.comparing" : snapshot && imageKind == 10 ? "status.exporting_html" : snapshot && imageKind == 11 ? "status.verifying" : snapshot ? "status.rendering_image" : "status.rendering"));
    g.worker = std::thread(runJobs, std::move(jobs), !snapshot);
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

// --- Camera d'une scene 3D (edition Pro) choisie a la souris sur l'apercu -------------------------------------------------
// Reglages vus par le rendu du shader selectionne : ceux de la fenetre, puis les commentaires // du shader
static bool shaderSettings(Settings& out) {
    if (g.paths.empty()) return false;
    out = g.base;
    std::string code;
    if (readFile(g.paths[selectedOrFirst()], code)) parseLines(out, code, true);
    return true;
}

bool sceneSelected() {
    const ProExt* px = proExt();
    Settings s;
    return px && px->sceneRequested && px->orbitCamera && shaderSettings(s) && px->sceneRequested(s);
}

void orbitScene(double dx, double dy, double zoom) {
    const ProExt* px = proExt();
    Settings s;
    if (!px || !px->orbitCamera || !shaderSettings(s) || !px->sceneRequested || !px->sceneRequested(s)) return;
    for (const Override& o : shaderOverrides(g.paths[selectedOrFirst()]))  // la camera ecrite dans le shader prime : la souris ne la changerait pas
        if (o.key == "camera" || o.key == "camerapos" || o.key == "cameratarget") {
            setStatus(TX("status.orbit_locked"));
            return;
        }
    const double t = g.player && g.player->running() ? g.player->time() : g.previewTime;
    const auto changes = px->orbitCamera(s, t, dx, dy, zoom);
    if (changes.empty()) return;
    for (const auto& kv : changes) applySetting(g.base, kv.first, kv.second);
    toolsChanged();  // le point de vue est copie dans les reglages (config.ini a la fermeture)
    std::string text;
    for (const char* k : {"camerapos", "cameratarget"})
        if (g.base.pro.count(k)) text += (text.empty() ? "" : "  ") + std::string(k) + "=" + g.base.pro[k];
    setStatus(TXF("status.orbit", W(text).c_str()));
    if (g.player && g.player->running()) g.player->changeSettings(changes);
    else if (g.running) g.orbitPending = true;
    else runPreview();
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
    g.worker = std::thread(runJobs, std::move(jobs), false);
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

