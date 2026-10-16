// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "cli.h"
#include "app_info.h"
#include "encoder.h"
#include "endaction.h"
#include "ffmpeg_download.h"
#include "formats.h"
#include "gl.h"
#include "i18n.h"
#include "compare.h"
#include "looptools.h"
#include "quality.h"
#include "process.h"
#include "publish.h"
#include "parts.h"
#include "rhythm.h"
#include "webexport.h"
#include "vr.h"
#include "render.h"
#include "shadertoy_import.h"
#include "util.h"
#include "platform/platform.h"
#include <cwctype>
#include <set>

namespace {

std::string jsonString(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += (char)c;
                }
        }
    }
    return o + "\"";
}

std::string num(double v) {
    char b[48];
    snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

}  // namespace

std::string renderHelpText() {
    std::string t = "\n" + i18n::tr8("cli.render_title") + "\n\n";
    for (const char* k : {"cli.render.opt_set", "cli.render.opt_profile", "cli.render.opt_progress", "cli.render.opt_png", "cli.render.opt_thumb", "cli.render.opt_verify", "cli.render.opt_beats", "cli.render.opt_farm", "cli.render.opt_reproduce", "cli.render.opt_exporthtml", "cli.render.opt_cachepurge", "cli.render.opt_check", "cli.render.opt_compare", "cli.render.opt_golden", "cli.render.opt_dryrun", "cli.render.opt_best", "cli.render.opt_cubefaces", "cli.render.opt_heatmap", "cli.render.opt_samples", "cli.render.opt_findloop", "cli.render.opt_sheet", "cli.render.opt_param", "cli.render.opt_watch", "cli.render.opt_watch_move", "cli.render.opt_log", "cli.render.opt_report",
                          "cli.render.exit_codes", "cli.opt.import", "cli.opt.diagnostic", "cli.opt.benchmark"})
        t += "  " + i18n::tr8(k) + "\n";
    return t;
}

int runRenderCli(const std::wstring& exeDir, const std::vector<std::wstring>& args,
                 const std::function<void(const std::string&, bool)>& write) {
    Settings base;
    std::string text;
    if (readFile(exeDir + L"config.ini", text)) parseLines(base, text, false);

    std::vector<std::wstring> files;
    std::string progress = "none";  // none | text | json
    bool snapshot = false;
    bool checkMode = false;      // --check[=flash,nan,...] : controle qualite sans rendu
    std::string checkList;
    bool compareMode = false;    // --compare a.glsl b.glsl : ecart entre deux shaders
    std::string compareVideo;    // --compare-video=sbs|curtain|diff
    double tolerance = -1;       // --tolerance=N : ecart maximal admis (0 a 255), -1 = valeur par defaut de chaque mode
    int goldenMode = 0;          // --golden=record (1) | verify (2) : images de reference
    bool farmStatusMode = false; // --farm-status dossier : etat d'une ferme de rendu
    bool beatsMode = false;      // --beats[=list|clear] son : tempo, temps forts et sections d'un fichier son
    std::string beatsOpt;
    bool verifyMode = false;     // --verify video : relit la video et la compare a son rapport
    bool reproduceMode = false;  // --reproduce video : extrait la source embarquee et refait le rendu
    bool exportHtmlMode = false; // --export-html shader : page HTML autonome
    bool cachePurgeMode = false; // --cache-purge : vide le cache des maitres
    bool dryRun = false;         // --dry-run : estimation du temps de rendu, rien n'est ecrit
    bool checkWarned = false, diffFound = false;
    double thumbTime = -1;       // --thumb[=secondes] : vignette de couverture
    int sheetCols = 0, sheetRows = 0;  // --sheet=CxR : planche-contact
    bool bestThumb = false;      // --png=best / --thumb=best : vignette intelligente (les trois images les plus riches)
    bool heatmap = false;        // --heatmap[=CxR] : carte de chaleur du cout GPU par zone de l'ecran
    int heatCols = 16, heatRows = 9;
    bool cubeFaces = false;      // --cubefaces[=taille] : les six faces d'une cubemap d'un shader mainVR
    bool findLoopMode = false;   // --find-loop[=secondes] : cherche la periode du shader au lieu de le rendre
    double findLoopMax = 0;
    std::wstring watchDir;  // --watch=dossier : rend chaque shader qui y apparait (explicite, jamais par defaut)
    int watchCount = 0;     // --watch-count=N : s'arrete apres N rendus (0 = sans fin)
    bool watchMove = false;  // --watch-move : le shader rendu est deplace vers done/ (ou failed/ en cas d'echec)
    std::wstring logPath;    // --log=fichier : copie horodatee de tout ce qui est affiche (option explicite)
    std::vector<std::pair<std::string, std::string>> overrides;
    auto fail = [&](const std::wstring& msg) {
        write(U8(TXF("log.error", msg.c_str())) + "\n", true);
        return kExitUsage;
    };
    for (const auto& a : args) {
        if (a.compare(0, 2, L"--") != 0) {
            files.push_back(a);
            continue;
        }
        const std::string arg = U8(a.substr(2));
        const size_t eq = arg.find('=');
        const std::string key = lower(eq == std::string::npos ? arg : arg.substr(0, eq));
        const std::string val = eq == std::string::npos ? "" : arg.substr(eq + 1);
        if (key == "render" || key == "quiet") continue;
        if (key == "png") {
            snapshot = true;
            bestThumb = lower(val) == "best";
        } else if (key == "farm-status") {
            farmStatusMode = true;
            if (!val.empty()) files.push_back(W(val));
        } else if (key == "beats") {
            beatsMode = true;
            beatsOpt = lower(val);
            if (!beatsOpt.empty() && beatsOpt != "list" && beatsOpt != "clear") return fail(TXF("cli.unknown_option", W(arg).c_str()));
        } else if (key == "verify") {
            verifyMode = true;
        } else if (key == "reproduce") {
            reproduceMode = true;
        } else if (key == "export-html") {
            exportHtmlMode = true;
        } else if (key == "cache-purge") {
            cachePurgeMode = true;
        } else if (key == "check") {
            checkMode = true;
            checkList = val;
            CheckOptions probeOpt;
            if (!parseCheckList(val, probeOpt)) return fail(TXF("cli.unknown_option", W(arg).c_str()));
        } else if (key == "compare") {
            compareMode = true;
        } else if (key == "compare-video") {
            compareVideo = lower(val);
            if (compareVideo != "sbs" && compareVideo != "curtain" && compareVideo != "diff") return fail(TXF("cli.unknown_option", W(arg).c_str()));
        } else if (key == "tolerance") {
            tolerance = atof(val.c_str());
        } else if (key == "golden") {
            const std::string g = lower(val);
            if (g == "record" || g == "enregistrer") goldenMode = 1;
            else if (g == "verify" || g == "verifier" || g == "vérifier") goldenMode = 2;
            else return fail(TXF("cli.unknown_option", W(arg).c_str()));
        } else if (key == "dry-run") {
            dryRun = true;
        } else if (key == "heatmap") {
            heatmap = true;
            if (!val.empty()) {
                int c = 0, r = 0;
                char x = 0;
                if (sscanf(val.c_str(), "%d%c%d", &c, &x, &r) != 3 || (x != 'x' && x != 'X') || c < 1 || r < 1 || c > 64 || r > 64) return fail(TXF("cli.bad_sheet", W(val).c_str()));
                heatCols = c;
                heatRows = r;
            }
        } else if (key == "cubefaces") {
            cubeFaces = true;
            snapshot = true;
            if (!val.empty()) {
                const int size = atoi(val.c_str());
                if (size < 16 || size > 16384) return fail(TXF("cli.unknown_option", W(arg).c_str()));
                overrides.push_back({"width", val});
                overrides.push_back({"height", val});
            }
        } else if (key == "find-loop") {
            findLoopMode = true;
            findLoopMax = val.empty() ? 0.0 : atof(val.c_str());
        } else if (key == "thumb") {  // vignette de couverture : une image a l'instant donne (milieu de la plage sans valeur), 1280 pixels au plus
            snapshot = true;
            if (lower(val) == "best") bestThumb = true;
            else thumbTime = val.empty() ? -2.0 : std::max(0.0, atof(val.c_str()));
        } else if (key == "sheet") {  // planche-contact : colonnes x lignes vignettes tirees sur la duree
            int c = 0, r = 0;
            char x = 0;
            if (sscanf(val.c_str(), "%d%c%d", &c, &x, &r) != 3 || (x != 'x' && x != 'X') || c < 1 || r < 1 || c * r > 100)
                return fail(TXF("cli.bad_sheet", W(val).c_str()));
            sheetCols = c;
            sheetRows = r;
        } else if (key == "progress") {
            progress = lower(val);
            if (progress != "json" && progress != "text" && progress != "none") return fail(TXF("cli.bad_progress", W(val).c_str()));
        } else if (key == "watch") {
            watchDir = W(val);
            if (!platform::dirExists(watchDir)) return fail(TXF("cli.watch_missing", watchDir.c_str()));
        } else if (key == "watch-count") {
            watchCount = std::max(0, atoi(val.c_str()));
        } else if (key == "watch-move") {
            watchMove = true;
        } else if (key == "log") {
            logPath = W(val);
            if (logPath.empty()) return fail(TXF("cli.unknown_option", W(arg).c_str()));
        } else if (key == "profile") {
            std::string p;
            if (!readFile(W(val), p)) return fail(TXF("error.profile_read", W(val).c_str()));
            parseLines(base, p, false);
        } else if (eq != std::string::npos && isSettingKey(key)) {
            overrides.push_back({key, val});
        } else {
            return fail(TXF("cli.unknown_option", W(arg).c_str()));
        }
    }
    for (const auto& kv : overrides) applySetting(base, kv.first, kv.second);
    FILE* logFile = nullptr;
    if (!logPath.empty()) {
        logFile = platform::openFile(logPath, "ab");
        if (!logFile) return fail(TXF("cli.log_failed", logPath.c_str()));
    }
    // sortie unique du programme : standard / erreur, et le fichier journal quand --log est donne
    const auto out = [&](const std::string& text, bool isErr) {
        write(text, isErr);
        if (!logFile) return;
        const platform::DateTime t = platform::localTime();
        char stamp[40];
        snprintf(stamp, sizeof(stamp), "[%04d-%02d-%02d %02d:%02d:%02d] ", t.year, t.month, t.day, t.hour, t.minute, t.second);
        size_t pos = 0;
        while (pos < text.size()) {
            size_t e = text.find('\n', pos);
            if (e == std::string::npos) e = text.size();
            if (e > pos) {
                const std::string line = std::string(stamp) + text.substr(pos, e - pos) + "\n";
                fwrite(line.data(), 1, line.size(), logFile);
            }
            pos = e + 1;
        }
        fflush(logFile);
    };
    struct LogCloser {
        FILE*& f;
        ~LogCloser() { if (f) fclose(f); }
    } logCloser{logFile};
    if (cachePurgeMode) {  // vide le cache des maitres (dossier cache/ du programme)
        const uint64_t freed = cachePurge(exeDir);
        out(U8(TXF("log.cache_purged", (double)freed / 1e6)) + "\n", false);
        return kExitOk;
    }
    if (farmStatusMode) {  // etat d'un dossier de ferme : pas de GPU, pas de ffmpeg
        if (files.empty()) return fail(TX("cli.no_files"));
        int missing = 0;
        for (const auto& f : files) {
            std::vector<std::wstring> lines;
            if (!farmStatus(f, base.farmTimeout, lines)) {
                out(U8(TXF("log.error", TXF("farm.status_none", f.c_str()).c_str())) + "\n", true);
                missing++;
                continue;
            }
            for (const std::wstring& l : lines) out(U8(l) + "\n", false);
        }
        return missing ? kExitRenderFailed : kExitOk;
    }
    if (files.empty() && watchDir.empty()) return fail(TX("cli.no_files"));
    for (const auto& f : files)
        if (!fileExists(f)) return fail(TXF("cli.file_missing", f.c_str()));
    if (!ffmpegPresent(exeDir)) {
        out(U8(TXF("log.error", TX("cli.no_ffmpeg").c_str())) + "\n", true);
        return kExitNoFfmpeg;
    }

    if (beatsMode) {  // rythme d'un fichier son : pas de GPU
        int failures = 0;
        for (const auto& f : files) {
            Rhythm r;
            std::wstring re;
            out(U8(f) + "\n", false);
            if (!r.load(findFfmpeg(exeDir), f, base.bpm, base.beatsPerBar, re)) {
                out(U8(TXF("log.error", re.c_str())) + "\n", true);
                failures++;
                continue;
            }
            if (beatsOpt == "clear") r.clearEdits();
            for (const std::wstring& l : rhythmSummary(r)) out(U8(L"  " + l) + "\n", false);
            if (beatsOpt == "list") {
                const RhythmGrid& g = r.grid();
                std::string line;
                auto dump = [&](const char* name, const std::vector<double>& v) {
                    line = std::string("  ") + name + ":";
                    for (double t : v) line += " " + num(t);
                    out(line + "\n", false);
                };
                dump("beats", g.beats);
                dump("bars", g.bars);
                dump("sections", g.sections);
                dump("onsets", g.onsets);
            }
        }
        return failures ? kExitRenderFailed : kExitOk;
    }
    if (verifyMode) {  // integrite : duree, cadence, nombre d'images, somme SHA-256 et rapport
        int mismatches = 0;
        for (const auto& f : files) {
            VerifyResult vr;
            std::wstring ve;
            out(U8(f) + "\n", false);
            if (!verifyVideo(findFfmpeg(exeDir), f, vr, ve)) {
                out(U8(TXF("log.error", ve.c_str())) + "\n", true);
                mismatches++;
                continue;
            }
            for (const std::wstring& c : vr.checks) out(U8(L"  " + c) + "\n", false);
            for (const std::wstring& p : vr.problems) out(U8(L"  ! " + p) + "\n", false);
            if (!vr.problems.empty()) mismatches++;
            else out(U8(L"  " + TX("log.verify.ok")) + "\n", false);
        }
        return mismatches ? kExitDiff : kExitOk;
    }
    if (exportHtmlMode) {  // page HTML autonome : pas de GPU ici
        int failures = 0;
        for (const auto& f : files) {
            std::wstring target, he;
            if (exportHtmlBeside(f, exeDir, base, target, he)) out(U8(TXF("log.html_written", target.c_str())) + "\n", false);
            else {
                out(U8(TXF("log.error", he.c_str())) + "\n", true);
                failures++;
            }
        }
        return failures ? kExitRenderFailed : kExitOk;
    }
    platform::applyGpuPreference(base.gpu);
    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        out(U8(TXF("log.error", err.c_str())) + "\n", true);
        return kExitNoGL;
    }
    const bool json = progress == "json";
    if (compareMode) {  // --compare a.glsl b.glsl : pas de rendu
        if (files.size() != 2) {
            destroyGLContext(gl);
            return fail(TX("cli.compare_two"));
        }
        CompareOptions co;
        if (tolerance >= 0) co.tolerance = tolerance;
        co.video = compareVideo;
        Hooks Hc;
        Hc.log = [&](const std::wstring& s) { out(U8(s) + "\n", false); };
        CompareResult cr;
        std::wstring ce;
        const bool done = compareShaders(files[0], files[1], exeDir, base, co, Hc, cr, ce);
        destroyGLContext(gl);
        if (!done) {
            out(U8(TXF("log.error", ce.c_str())) + "\n", true);
            return kExitRenderFailed;
        }
        if (json) {
            out("{\"event\":\"compare\",\"frames\":" + std::to_string(cr.frames) + ",\"mean\":" + num(cr.meanAbs) + ",\"max\":" + num(cr.maxAbs) + ",\"psnr\":" + num(cr.psnr) +
                    ",\"identical\":" + (cr.identical ? "true" : "false") + ",\"different\":" + (cr.different ? "true" : "false") + "}\n", false);
        } else {
            out(U8(describeCompare(cr)) + "\n", false);
            if (!cr.diffFile.empty()) out(U8(TXF("log.compare.diff_file", cr.diffFile.c_str())) + "\n", false);
            if (!cr.videoFile.empty()) out(U8(TXF("log.compare.video_file", cr.videoFile.c_str())) + "\n", false);
        }
        return cr.different ? kExitDiff : kExitOk;
    }
    int ok = 0, failed = 0, index = 0, lastDecile = -1;
    double queueLow = 0, queueMid = 0, queueHigh = 0;  // --dry-run : la file entiere
    std::wstring lastOutput;
    auto renderOne = [&](const std::wstring& f, size_t count, const Settings& cfg) -> bool {
        index++;
        std::wstring currentOutput;
        Hooks H;
        H.snapshot = snapshot;
        H.thumbTime = thumbTime;
        H.sheetCols = snapshot ? 0 : sheetCols;  // une image fixe prime sur la planche
        H.sheetRows = snapshot ? 0 : sheetRows;
        H.log = [&](const std::wstring& s) {
            if (json) out("{\"event\":\"log\",\"message\":" + jsonString(U8(s)) + "}\n", false);
            else out(U8(s) + "\n", false);
        };
        H.progress = [&](long long n, long long total, double speed, double remaining) {
            const double pct = total > 0 ? 100.0 * n / total : 0.0;
            if (json) {
                out("{\"event\":\"progress\",\"file\":" + jsonString(U8(f)) + ",\"frame\":" + std::to_string(n) + ",\"frames\":" +
                          std::to_string(total) + ",\"percent\":" + num(pct) + ",\"fps\":" + num(speed) + ",\"eta\":" + num(remaining) + "}\n",
                      false);
            } else if (progress == "text" && (int)(pct / 10) != lastDecile) {
                lastDecile = (int)(pct / 10);
                out(U8(wfmt(L"  %3.0f %%  (%lld/%lld)", pct, n, total)) + "\n", false);
            }
        };
        H.output = [&](const std::wstring& path) { currentOutput = path; lastOutput = path; };
        lastDecile = -1;
        if (json) out("{\"event\":\"start\",\"file\":" + jsonString(U8(f)) + ",\"index\":" + std::to_string(index) + ",\"count\":" +
                            std::to_string(count) + "}\n", false);
        else out(U8(f) + "\n", false);
        std::wstring e;
        if (reproduceMode) {  // la source embarquee dans la video est extraite et rendue de nouveau
            SourceBundle bundle;
            if (!readBundleFromVideo(findFfmpeg(exeDir), f, bundle, e)) {
                failed++;
                out(U8(TXF("log.error", e.c_str())) + "\n", true);
                return false;
            }
            const size_t cutp = f.find_last_of(L"\\/");
            const std::wstring vdir = cutp == std::wstring::npos ? L"" : f.substr(0, cutp + 1);
            std::wstring vstem = f.substr(cutp == std::wstring::npos ? 0 : cutp + 1);
            if (size_t dotp = vstem.find_last_of(L'.'); dotp != std::wstring::npos) vstem.erase(dotp);
            const std::wstring srcDir = vdir + vstem + L"_source";
            platform::makeDir(srcDir);
            for (const auto& file : bundle.files) {
                const std::string leaf = file.first.substr(file.first.find_last_of("\\/") == std::string::npos ? 0 : file.first.find_last_of("\\/") + 1);  // jamais de chemin
                writeFileAtomic(srcDir + platform::pathSeparator() + W(leaf), file.second);
            }
            Settings rs;
            parseLines(rs, bundle.settingsIni, false);
            rs.outdir = vdir + vstem + L"_reproduced";
            rs.exists = "overwrite";
            rs.name.clear();
            rs.report = "json";
            out(U8(L"  " + TXF("log.reproduce_extracted", (long long)bundle.files.size(), srcDir.c_str(), W(bundle.appVersion).c_str())) + "\n", false);
            const std::wstring shaderFile = srcDir + platform::pathSeparator() + W(bundle.shaderName.substr(bundle.shaderName.find_last_of("\\/") == std::string::npos ? 0 : bundle.shaderName.find_last_of("\\/") + 1));
            if (convertFile(shaderFile, exeDir, rs, H, e) == Result::Ok) {
                ok++;
                return true;
            }
            failed++;
            out(U8(TXF("log.error", e.c_str())) + "\n", true);
            return false;
        }
        if (checkMode) {  // controle qualite sans rendu
            CheckOptions co;
            parseCheckList(checkList, co);
            CheckReport cr;
            if (!runChecks(f, exeDir, cfg, co, H, cr, e)) {
                failed++;
                if (json) out("{\"event\":\"error\",\"file\":" + jsonString(U8(f)) + ",\"message\":" + jsonString(U8(e)) + "}\n", false);
                else out(U8(TXF("log.error", e.c_str())) + "\n", true);
                return false;
            }
            writeCheckReport(cr, outputPath(f, cfg.outdir, L""));
            if (cr.warnings() > 0) checkWarned = true;
            if (json) {
                std::string kinds;
                for (const CheckIssue& i : cr.issues) kinds += (kinds.empty() ? "" : ",") + jsonString(i.kind);
                out("{\"event\":\"check\",\"file\":" + jsonString(U8(f)) + ",\"warnings\":" + std::to_string(cr.warnings()) + ",\"issues\":[" + kinds + "]}\n", false);
            } else {
                out(checkReportText(cr), false);
            }
            ok++;
            return true;
        }
        if (goldenMode) {  // images de reference de ses propres shaders
            GoldenResult gr;
            bool done;
            if (goldenMode == 1) {
                done = goldenRecord(f, exeDir, cfg, 5, H, gr, e);
                if (done) out(U8(L"  " + TXF("log.golden.recorded", gr.frames, gr.dir.c_str())) + "\n", false);
            } else {
                done = goldenVerify(f, exeDir, cfg, tolerance >= 0 ? tolerance : 1.0, H, gr, e);
                if (done) {
                    if (gr.ok) out(U8(L"  " + TXF("log.golden.ok", gr.frames, W(num(gr.worstMean)).c_str())) + "\n", false);
                    else {
                        diffFound = true;
                        out(U8(L"  " + TXF("log.golden.differs", gr.worstIndex + 1, gr.worstTime, W(num(gr.worstMean)).c_str(), tolerance >= 0 ? tolerance : 1.0)) + "\n", false);
                    }
                }
            }
            if (!done) {
                failed++;
                if (json) out("{\"event\":\"error\",\"file\":" + jsonString(U8(f)) + ",\"message\":" + jsonString(U8(e)) + "}\n", false);
                else out(U8(TXF("log.error", e.c_str())) + "\n", true);
                return false;
            }
            ok++;
            return true;
        }
        if (dryRun) {  // estimation du temps de rendu
            Estimate est;
            if (!estimateRender(f, exeDir, cfg, H, est, e)) {
                failed++;
                if (json) out("{\"event\":\"error\",\"file\":" + jsonString(U8(f)) + ",\"message\":" + jsonString(U8(e)) + "}\n", false);
                else out(U8(TXF("log.error", e.c_str())) + "\n", true);
                return false;
            }
            queueLow += est.secLow;
            queueMid += est.secMid;
            queueHigh += est.secHigh;
            if (json) out("{\"event\":\"estimate\",\"file\":" + jsonString(U8(f)) + ",\"seconds\":" + num(est.secMid) + ",\"low\":" + num(est.secLow) + ",\"high\":" + num(est.secHigh) + ",\"fps\":" + num(est.fpsMean) + ",\"slow\":" + (est.slow ? "true" : "false") + "}\n", false);
            else out(U8(L"  " + describeEstimate(est)) + "\n", false);
            ok++;
            return true;
        }
        if (findLoopMode) {  // pas de rendu : la periode du shader
            LoopFound lf;
            if (!findLoop(f, exeDir, cfg, findLoopMax, H, lf, e)) {
                failed++;
                if (json) out("{\"event\":\"error\",\"file\":" + jsonString(U8(f)) + ",\"message\":" + jsonString(U8(e)) + "}\n", false);
                else out(U8(TXF("log.error", e.c_str())) + "\n", true);
                return false;
            }
            if (json) {
                out("{\"event\":\"loop\",\"file\":" + jsonString(U8(f)) + ",\"found\":" + (lf.found ? "true" : "false") + ",\"static\":" + (lf.isStatic ? "true" : "false") +
                        ",\"period\":" + num(lf.period) + ",\"distance\":" + num(lf.distance) + ",\"quality\":" + std::to_string(lf.quality) + "}\n", false);
            } else {
                out(U8(L"  " + describeLoop(lf)) + "\n", false);
            }
            if (lf.found) {
                ok++;
                return true;
            }
            failed++;
            return false;
        }
        if (heatmap) {  // cout du shader par zone de l'ecran
            HeatStats hs;
            std::wstring heatFile;
            if (renderHeatmap(f, exeDir, cfg, heatCols, heatRows, -1, H, hs, heatFile, e)) {
                H.log(L"  " + describeHeat(hs));
                H.log(L"  " + TXF("log.ok_output", heatFile.c_str()));
                ok++;
                if (json) out("{\"event\":\"done\",\"file\":" + jsonString(U8(f)) + ",\"output\":" + jsonString(U8(heatFile)) + "}\n", false);
                return true;
            }
            failed++;
            if (json) out("{\"event\":\"error\",\"file\":" + jsonString(U8(f)) + ",\"message\":" + jsonString(U8(e)) + "}\n", false);
            else out(U8(TXF("log.error", e.c_str())) + "\n", true);
            return false;
        }
        if (cubeFaces) {  // six faces PNG carrees
            std::vector<std::wstring> made;
            Settings sq = cfg;
            sq.height = sq.width;
            if (renderCubeFaces(f, exeDir, sq, H, made, e)) {
                ok++;
                if (json) out("{\"event\":\"done\",\"file\":" + jsonString(U8(f)) + ",\"output\":" + jsonString(U8(currentOutput)) + "}\n", false);
                return true;
            }
            failed++;
            if (json) out("{\"event\":\"error\",\"file\":" + jsonString(U8(f)) + ",\"message\":" + jsonString(U8(e)) + "}\n", false);
            else out(U8(TXF("log.error", e.c_str())) + "\n", true);
            return false;
        }
        if (bestThumb) {  // vignette intelligente : trois propositions
            std::vector<std::wstring> made;
            if (renderSmartThumbnails(f, exeDir, cfg, H, made, e)) {
                ok++;
                if (json) out("{\"event\":\"done\",\"file\":" + jsonString(U8(f)) + ",\"output\":" + jsonString(U8(currentOutput)) + "}\n", false);
                return true;
            }
            failed++;
            if (json) out("{\"event\":\"error\",\"file\":" + jsonString(U8(f)) + ",\"message\":" + jsonString(U8(e)) + "}\n", false);
            else out(U8(TXF("log.error", e.c_str())) + "\n", true);
            return false;
        }
        if (convertFile(f, exeDir, cfg, H, e) == Result::Ok) {
            ok++;
            if (json) out("{\"event\":\"done\",\"file\":" + jsonString(U8(f)) + ",\"output\":" + jsonString(U8(currentOutput)) + "}\n", false);
            return true;
        }
        failed++;
        if (json) out("{\"event\":\"error\",\"file\":" + jsonString(U8(f)) + ",\"message\":" + jsonString(U8(e)) + "}\n", false);
        else out(U8(TXF("log.error", e.c_str())) + "\n", true);
        return false;
    };
    for (const auto& f0 : files) {
        std::wstring ie;
        const std::wstring f = resolveShaderInput(exeDir, f0, [&](const std::wstring& m) {
            if (json) out("{\"event\":\"log\",\"message\":" + jsonString(U8(m)) + "}\n", false);
            else out(U8(m) + "\n", false);
        }, ie);
        if (f.empty()) {  // import impossible : compte comme un rendu en echec
            failed++;
            if (json) out("{\"event\":\"error\",\"file\":" + jsonString(U8(f0)) + ",\"message\":" + jsonString(U8(ie)) + "}\n", false);
            else out(U8(TXF("log.error", ie.c_str())) + "\n", true);
            continue;
        }
        renderOne(f, files.size(), base);
    }
    if (!watchDir.empty()) {  // surveillance : un shader est rendu des que sa taille ne change plus entre deux releves
        if (json) out("{\"event\":\"watching\",\"dir\":" + jsonString(U8(watchDir)) + "}\n", false);
        else out(U8(TXF("cli.watching", watchDir.c_str())) + "\n", false);
        const std::wstring sep(1, platform::pathSeparator());
        std::map<std::wstring, long long> lastSize;  // taille au dernier releve, par chemin complet
        std::set<std::wstring> handled;
        int rendered = 0;
        // <nom>.glsl ou <nom>.txt ; pas les passes <nom>.bufferA.glsl
        const auto isShaderName = [](const std::wstring& name) {
            const size_t dot = name.find_last_of(L'.');
            if (dot == std::wstring::npos || dot == 0 || name.find(L'.') != dot) return false;
            std::wstring ext = name.substr(dot);
            for (auto& ch : ext) ch = (wchar_t)towlower(ch);
            return ext == L".glsl" || ext == L".txt";
        };
        // Reglages d'un dossier : config.ini et --profile, profile.ini du dossier surveille, profile.ini du sous-dossier, puis les options de la ligne de commande
        const auto settingsFor = [&](const std::wstring& dir) {
            Settings cfg = base;
            bool layered = false;
            for (const std::wstring& d : {watchDir, dir}) {
                std::string text;
                if (readFile(d + sep + L"profile.ini", text)) {
                    parseLines(cfg, text, false);
                    layered = true;
                }
                if (dir == watchDir) break;
            }
            if (layered)
                for (const auto& kv : overrides) applySetting(cfg, kv.first, kv.second);
            return cfg;
        };
        // --watch-move : le shader et ses passes (<nom>.bufferA.glsl...) vont dans done/ ou failed/ du meme dossier
        const auto moveProcessed = [&](const std::wstring& dir, const std::wstring& name, bool succeeded) {
            const std::wstring dest = dir + sep + (succeeded ? L"done" : L"failed");
            platform::makeDir(dest);
            const std::wstring stem = name.substr(0, name.find(L'.'));
            std::vector<platform::DirEntry> all;
            platform::listDir(dir, all);
            for (const auto& en : all) {
                if (en.isDir || en.name.compare(0, stem.size() + 1, stem + L".") != 0) continue;
                std::wstring ext = en.name.substr(en.name.find_last_of(L'.'));
                for (auto& ch : ext) ch = (wchar_t)towlower(ch);
                if (ext != L".glsl" && ext != L".txt") continue;
                if (platform::moveFile(dir + sep + en.name, dest + sep + en.name, true)) out(U8(TXF("cli.moved", en.name.c_str(), dest.c_str())) + "\n", false);
            }
        };
        while (watchCount == 0 || rendered < watchCount) {
            struct Candidate { std::wstring dir, name; };
            std::vector<Candidate> found;
            std::vector<platform::DirEntry> entries;
            platform::listDir(watchDir, entries);
            for (const auto& en : entries) {
                if (en.isDir) {  // sous-dossier : ses shaders sont surveilles aussi (done/ et failed/ sont ignores)
                    if (en.name == L"done" || en.name == L"failed") continue;
                    std::vector<platform::DirEntry> inner;
                    platform::listDir(watchDir + sep + en.name, inner);
                    for (const auto& in : inner)
                        if (!in.isDir && isShaderName(in.name)) found.push_back({watchDir + sep + en.name, in.name});
                } else if (isShaderName(en.name)) {
                    found.push_back({watchDir, en.name});
                }
            }
            for (const auto& cand : found) {
                const std::wstring path = cand.dir + sep + cand.name;
                if (handled.count(path)) continue;
                std::string content;
                if (!readFile(path, content)) continue;
                auto it = lastSize.find(path);
                if (it == lastSize.end() || it->second != (long long)content.size()) {
                    lastSize[path] = (long long)content.size();  // vu pour la premiere fois ou encore en ecriture
                    continue;
                }
                handled.insert(path);
                const bool rendered_ok = renderOne(path, 1, settingsFor(cand.dir));
                if (watchMove) moveProcessed(cand.dir, cand.name, rendered_ok);
                if (++rendered >= watchCount && watchCount > 0) break;
            }
            if (watchCount == 0 || rendered < watchCount) platform::sleepMs(1000);
        }
    }
    if (dryRun && files.size() > 1) {
        if (json) out("{\"event\":\"estimate_total\",\"seconds\":" + num(queueMid) + ",\"low\":" + num(queueLow) + ",\"high\":" + num(queueHigh) + "}\n", false);
        else out(U8(TXF("log.estimate.total", formatDuration(queueMid).c_str(), formatDuration(queueLow).c_str(), formatDuration(queueHigh).c_str())) + "\n", false);
    }
    if (!failed && watchDir.empty() && !files.empty() && !checkMode && !goldenMode && !dryRun)  // action de fin facultative (endaction=folder|sleep)
        performEndAction(base.endaction, lastOutput, [&](const std::wstring& m) { out(U8(m) + "\n", false); }, true);
    destroyGLContext(gl);
    if (json) out("{\"event\":\"end\",\"ok\":" + std::to_string(ok) + ",\"failed\":" + std::to_string(failed) + "}\n", false);
    return failed ? kExitRenderFailed : checkWarned ? kExitCheckWarn : diffFound ? kExitDiff : kExitOk;
}

// ---------------------------------------------------------------------------
//  --diagnostic
// ---------------------------------------------------------------------------
int runDiagnosticCli(const std::wstring& exeDir, const std::function<void(const std::string&, bool)>& write) {
    std::string o;
    auto line = [&](const std::string& label, const std::string& value) { o += label + ": " + value + "\n"; };
#if defined(_WIN32)
    const char* os = "Windows";
#elif defined(__APPLE__)
    const char* os = "macOS";
#else
    const char* os = "Linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char* arch = "arm64";
#else
    const char* arch = "x86_64";
#endif
    o += std::string(APP_NAME_STR) + " " + APP_VER_STR + "\n";
    line(i18n::tr8("diag.system"), std::string(os) + " " + arch);
    line(i18n::tr8("diag.threads"), std::to_string(std::thread::hardware_concurrency()));
    line(i18n::tr8("diag.language"), i18n::language());

    GLContext gl;
    std::wstring err;
    if (createGLContext(gl, err)) {
        auto gs = [](unsigned name) {
            const unsigned char* s = glGetString(name);
            return s ? std::string((const char*)s) : std::string("?");
        };
        GLint maxTex = 0;
        glGetIntegerv(0x0D33 /*GL_MAX_TEXTURE_SIZE*/, &maxTex);
        line(i18n::tr8("diag.gl_vendor"), gs(0x1F00));
        line(i18n::tr8("diag.gl_renderer"), gs(0x1F01));
        line(i18n::tr8("diag.gl_version"), gs(0x1F02));
        line(i18n::tr8("diag.glsl_version"), gs(0x8B8C));
        line(i18n::tr8("diag.max_texture"), std::to_string(maxTex));
    } else {
        line(i18n::tr8("diag.opengl"), U8(err));
    }

    const std::wstring ffmpeg = findFfmpeg(exeDir);
    std::string first;
    const int rc = runProcess(L"\"" + ffmpeg + L"\" -hide_banner -version", [&](const std::wstring& l) {
        if (first.empty()) first = U8(l);
    });
    if (rc != 0 || first.empty()) {
        line("ffmpeg", i18n::tr8("diag.not_found"));
    } else {
        line("ffmpeg", first);
        std::string codecs, containers;
        for (int i = 0; i < codecCount(); i++)
            codecs += U8(codecAt(i).label) + (codecAvailable(ffmpeg, codecAt(i).id) ? "+ " : "- ");
        for (int i = 0; i < containerCount(); i++)
            containers += U8(containerAt(i).label) + (containerAvailable(ffmpeg, containerAt(i).id) ? "+ " : "- ");
        line(i18n::tr8("diag.codecs"), codecs);
        line(i18n::tr8("diag.containers"), containers);
        Hooks H;
        H.log = [](const std::wstring&) {};
        std::string hw;
        for (const char* codec : {"h264", "h265", "av1"}) {
            Settings s;
            s.codec = codec;
            s.encoder = "auto";
            hw += std::string(codec) + "=" + resolveEncoder(s, ffmpeg, H) + " ";
        }
        line(i18n::tr8("diag.hw_encoders"), hw);
    }
    if (gl.impl) destroyGLContext(gl);
    write(o, false);
    return kExitOk;
}

// ---------------------------------------------------------------------------
//  --benchmark
// ---------------------------------------------------------------------------
int runBenchmarkCli(const std::wstring& exeDir, const std::vector<std::wstring>& args,
                    const std::function<void(const std::string&, bool)>& write) {
    double seconds = 2.0, maxDrop = 15.0;
    bool json = false, quick = false, parallelBench = false;
    int parallelWorkers = 0;
    std::wstring comparePath;  // --compare=fichier.json : resultat d'un --benchmark --json precedent
    for (const auto& a : args) {
        const std::string s = U8(a);
        if (s == "--json") json = true;
        else if (s.compare(0, 10, "--parallel") == 0) {  // gain du rendu en parallele (contextes OpenGL) sur ce poste
            parallelBench = true;
            parallelWorkers = s.size() > 11 ? std::max(2, std::min(16, atoi(s.c_str() + 11))) : 0;
        }
        else if (s == "--quick") quick = true;  // sans le 4K (machines lentes, integration continue)
        else if (s.compare(0, 10, "--seconds=") == 0) seconds = std::max(0.5, std::min(30.0, atof(s.c_str() + 10)));
        else if (s.compare(0, 10, "--compare=") == 0) comparePath = W(s.substr(10));
        else if (s.compare(0, 11, "--max-drop=") == 0) maxDrop = std::max(0.0, std::min(100.0, atof(s.c_str() + 11)));
    }
    // Resultats precedents : {"width":W,"height":H,"encoder":"E","fps":F}
    struct Bench { int w = 0, h = 0; std::string enc; double fps = 0; bool used = false; };
    std::vector<Bench> previous;
    if (!comparePath.empty()) {
        std::string prevText;
        if (!readFile(comparePath, prevText)) {
            write(U8(TXF("log.error", TXF("cli.bad_compare", comparePath.c_str()).c_str())) + "\n", true);
            return kExitUsage;
        }
        size_t pos = 0;
        while ((pos = prevText.find("{\"width\":", pos)) != std::string::npos) {
            Bench b;
            char enc[64] = {0};
            if (sscanf(prevText.c_str() + pos, "{\"width\":%d,\"height\":%d,\"encoder\":\"%63[^\"]\",\"fps\":%lf", &b.w, &b.h, enc, &b.fps) == 4) {
                b.enc = enc;
                previous.push_back(b);
            }
            pos += 9;
        }
        if (previous.empty()) {
            write(U8(TXF("log.error", TXF("cli.bad_compare", comparePath.c_str()).c_str())) + "\n", true);
            return kExitUsage;
        }
    }
    if (!ffmpegPresent(exeDir)) {
        write(U8(TXF("log.error", TX("cli.no_ffmpeg").c_str())) + "\n", true);
        return kExitNoFfmpeg;
    }
    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        write(U8(TXF("log.error", err.c_str())) + "\n", true);
        return kExitNoGL;
    }
    // Shader de mesure : plasma (un peu de calcul par pixel), ecrit dans un dossier temporaire a cote du programme
    const std::wstring dir = exeDir + L"benchmark_tmp";
    platform::makeDir(dir);
    const std::wstring shader = dir + platform::pathSeparator() + L"bench.glsl";
    const std::string src =
        "void mainImage(out vec4 o, in vec2 p){ vec2 uv = p / iResolution.xy; float t = iTime;"
        " float v = sin(uv.x * 10.0 + t) + sin(uv.y * 10.0 - t) + sin((uv.x + uv.y) * 8.0 + t * 0.5)"
        " + sin(length(uv - 0.5) * 20.0 - t * 2.0);"
        " o = vec4(0.5 + 0.5 * sin(vec3(v, v + 2.0, v + 4.0)), 1.0); }\n";
    if (FILE* f = platform::openFile(shader, "wb")) {
        fwrite(src.data(), 1, src.size(), f);
        fclose(f);
    }
    struct Case { int w, h; const char* encoder; };
    const Case cases[] = {{1280, 720, "cpu"}, {1920, 1080, "cpu"}, {1920, 1080, "auto"}, {3840, 2160, "cpu"}};
    const std::string hwPrefix = U8(TX("log.hw_encoder")).substr(0, U8(TX("log.hw_encoder")).find('%'));
    std::string text, js = "{\"version\":\"" + std::string(APP_VER_STR) + "\",\"results\":[";
    std::string cmpText, cmpJs;
    int regressions = 0;
    bool first = true;
    for (const Case& c : cases) {
        if (quick && c.w > 1920) continue;
        Settings s;
        s.width = c.w;
        s.height = c.h;
        s.fps = 60;
        s.duration = seconds;
        s.crf = 23;
        s.preset = "veryfast";
        s.encoder = c.encoder;
        s.outdir = dir;
        s.exists = "overwrite";
        s.chunk = 0;
        double speed = 0;
        std::string used = c.encoder;
        Hooks H;
        H.log = [&](const std::wstring& l) {
            const std::string u = U8(l);
            const size_t p = u.find(hwPrefix);
            if (!hwPrefix.empty() && p != std::string::npos) used = u.substr(p + hwPrefix.size());
        };
        H.progress = [&](long long, long long, double sp, double) { speed = sp; };
        std::wstring e;
        const bool ok = convertFile(shader, exeDir, s, H, e) == Result::Ok;
        char b[160];
        snprintf(b, sizeof(b), "%dx%d  %-14s  %8.1f %s\n", c.w, c.h, used.c_str(), ok ? speed : 0.0, ok ? "fps" : U8(e).c_str());
        text += b;
        snprintf(b, sizeof(b), "%s{\"width\":%d,\"height\":%d,\"encoder\":\"%s\",\"fps\":%.1f}", first ? "" : ",", c.w, c.h, used.c_str(), ok ? speed : 0.0);
        js += b;
        for (Bench& pv : previous) {  // comparaison avec le meme cas du resultat precedent
            if (pv.used || pv.w != c.w || pv.h != c.h || pv.enc != used || pv.fps <= 0) continue;
            pv.used = true;
            const double now = ok ? speed : 0.0, change = 100.0 * (now - pv.fps) / pv.fps;
            const bool regression = change < -maxDrop;
            if (regression) regressions++;
            snprintf(b, sizeof(b), "%dx%d  %-14s  %8.1f -> %8.1f fps  %+.0f %%%s\n", c.w, c.h, used.c_str(), pv.fps, now, change, regression ? "  <-- !" : "");
            cmpText += b;
            snprintf(b, sizeof(b), "%s{\"width\":%d,\"height\":%d,\"encoder\":\"%s\",\"previous\":%.1f,\"fps\":%.1f,\"change_percent\":%.1f,\"regression\":%s}",
                     cmpJs.empty() ? "" : ",", c.w, c.h, used.c_str(), pv.fps, now, change, regression ? "true" : "false");
            cmpJs += b;
            break;
        }
        first = false;
    }
    js += "]";
    std::string parallelText, parallelJs;
    if (parallelBench) {  // la meme video en huit parties, rendue avec 1 puis N contextes : le temps total et le gain
        const int hw = (int)std::max(2u, std::thread::hardware_concurrency());
        const int workers = parallelWorkers > 0 ? parallelWorkers : std::min(4, hw);
        Settings s;
        s.width = 1280;
        s.height = 720;
        s.fps = 60;
        s.duration = std::max(4.0, seconds * 2);
        s.chunk = s.duration / 8;
        s.crf = 23;
        s.preset = "veryfast";
        s.encoder = "cpu";
        s.outdir = dir;
        s.exists = "overwrite";
        double base1 = 0;
        for (int w : {1, workers}) {
            s.parallel = w;
            Hooks H;
            H.log = [](const std::wstring&) {};
            std::wstring e;
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = convertFile(shader, exeDir, s, H, e) == Result::Ok;
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (w == 1) base1 = ok ? sec : 0;
            const double gain = ok && sec > 0 && base1 > 0 ? base1 / sec : 0;
            char b[200];
            snprintf(b, sizeof(b), "parallel=%-2d  %7.2f s  x%.2f%s\n", w, ok ? sec : 0.0, gain, platform::multiContext() || w == 1 ? "" : "  (serial: one context on this system)");
            parallelText += b;
            snprintf(b, sizeof(b), "%s{\"workers\":%d,\"seconds\":%.2f,\"speedup\":%.2f}", parallelJs.empty() ? "" : ",", w, ok ? sec : 0.0, gain);
            parallelJs += b;
        }
        js += ",\"parallel\":[" + parallelJs + "]";
    }
    if (!previous.empty())
        js += ",\"compare\":{\"max_drop_percent\":" + std::to_string((int)maxDrop) + ",\"regressions\":" + std::to_string(regressions) + ",\"results\":[" + cmpJs + "]}";
    js += "}\n";
    removeTree(dir);
    destroyGLContext(gl);
    std::string report = U8(TX("diag.benchmark_title")) + "\n" + text;
    if (parallelBench) report += "\n" + U8(TX("diag.bench_parallel")) + "\n" + parallelText;
    if (!previous.empty()) {
        report += "\n" + U8(TXF("diag.bench_compare", (int)maxDrop)) + "\n" + (cmpText.empty() ? U8(TX("diag.bench_no_match")) + "\n" : cmpText);
    }
    write(json ? js : report, false);
    if (regressions && !json) write(U8(TXF("diag.bench_regression", regressions)) + "\n", true);
    return regressions ? kExitBenchRegression : kExitOk;
}
