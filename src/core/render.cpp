// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render.h"
#include "channels.h"
#include "compat.h"
#include "encoder.h"
#include "locate.h"
#include "passes.h"
#include "formats.h"
#include "i18n.h"
#include "dateinput.h"
#include "gl.h"
#include "montage.h"
#include "overlay.h"
#include "params.h"
#include "postfx.h"
#include "previewconv.h"
#include "mouse.h"
#include "vr.h"
#include "process.h"
#include "report.h"
#include "soundpass.h"
#include "timemap.h"
#include "looptools.h"
#include "quality.h"
#include "publish.h"
#include "keyboard.h"
#include "util.h"
#include "app_info.h"
#include "platform/platform.h"

// ---------------------------------------------------------------------------
//  Envoi des images a ffmpeg dans un thread dedie (rendu et encodage en parallele)
// ---------------------------------------------------------------------------
struct FrameQueue {
    platform::Process* proc = nullptr;
    size_t frameBytes = 0;
    std::vector<std::vector<uint8_t>> bufs;
    std::queue<int> freeQ, fullQ;
    std::mutex m;
    std::condition_variable cv;
    bool finished = false, failed = false;
};

static void writerThread(FrameQueue* q) {
    for (;;) {
        int idx;
        {
            std::unique_lock<std::mutex> l(q->m);
            q->cv.wait(l, [&] { return !q->fullQ.empty() || q->finished; });
            if (q->fullQ.empty()) return;
            idx = q->fullQ.front();
            q->fullQ.pop();
        }
        bool ok = q->proc->writeStdin(q->bufs[idx].data(), q->frameBytes);
        {
            std::lock_guard<std::mutex> l(q->m);
            if (!ok) q->failed = true;
            q->freeQ.push(idx);
        }
        q->cv.notify_all();
    }
}

// ---------------------------------------------------------------------------
//  Texte du shader : UTF-16 -> UTF-8, fins de ligne normalisees
// ---------------------------------------------------------------------------
void normalizeShaderText(std::string& code, const Hooks& H) {
    const size_t n = code.size();
    bool le = n >= 2 && (uint8_t)code[0] == 0xFF && (uint8_t)code[1] == 0xFE;
    bool be = n >= 2 && (uint8_t)code[0] == 0xFE && (uint8_t)code[1] == 0xFF;
    if (!le && !be && n >= 4) {  // UTF-16 sans BOM : un octet sur deux est nul (texte ASCII)
        size_t zeros = 0, sample = std::min<size_t>(n, 200);
        for (size_t i = 0; i < sample; i++)
            if (code[i] == 0) zeros++;
        if (zeros * 3 > sample) {
            le = code[1] == 0;
            be = !le;
        }
    }
    if (le || be) {
        std::wstring w;
        for (size_t i = (code.size() >= 2 && ((uint8_t)code[0] == 0xFF || (uint8_t)code[0] == 0xFE)) ? 2 : 0; i + 1 < n; i += 2) {
            uint8_t a = (uint8_t)code[i], b = (uint8_t)code[i + 1];
            w += (wchar_t)(le ? (a | (b << 8)) : ((a << 8) | b));
        }
        code = U8(w);
        H.log(L"  " + TX("log.utf16_converted"));
    }
    if (code.size() >= 3 && (uint8_t)code[0] == 0xEF && (uint8_t)code[1] == 0xBB && (uint8_t)code[2] == 0xBF)
        code.erase(0, 3);  // BOM UTF-8
    // \r\n et \r seuls (anciens Mac) -> \n
    std::string out;
    out.reserve(code.size());
    for (size_t i = 0; i < code.size(); i++) {
        if (code[i] == '\r') {
            out += '\n';
            if (i + 1 < code.size() && code[i + 1] == '\n') i++;
        } else {
            out += code[i];
        }
    }
    code.swap(out);
}

// ---------------------------------------------------------------------------
//  Conversion d'un fichier shader -> video / image / apercu
// ---------------------------------------------------------------------------
using Forced = std::vector<std::pair<std::string, std::string>>;  // reglages imposes a une variante

// Mesures recueillies pendant un rendu, pour le rapport (reglage `report`)
struct ReportData {
    bool haveSettings = false;
    Settings s;
    std::wstring output;
    int width = 0, height = 0;
    double fps = 0, mediaSeconds = 0, loopSeconds = 0, waitSeconds = 0;
    long long frames = 0;
    std::string encoder;
    double gpuImageMs = 0, gpuBufferMs = 0, gpuPostMs = 0;  // temps GPU moyen par image, mesure sur quelques images
    int gpuSamples = 0;
    double gpuPassMs[5] = {0, 0, 0, 0, 0};
    bool loopChecked = false;  // controle de la couture apres le rendu (looptools.h)
    double loopSeam = 0, loopTypical = 0;
    bool loopPerfect = false;
};

static Result convertOneImpl(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H, std::wstring& err,
                             const Forced* forced, ReportData* rd);

// Un rendu (une variante si `forced` est donne : ses reglages priment sur ceux du shader). Ecrit le rapport si le reglage `report` le demande.
static Result convertOne(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H, std::wstring& err,
                         const Forced* forced) {
    ReportData rd;
    std::vector<std::wstring> warnings;
    Hooks H2 = H;
    H2.log = [&](const std::wstring& line) {
        if (isWarningLine(line)) warnings.push_back(W(trim(U8(line))));
        H.log(line);
    };
    const auto t0 = std::chrono::steady_clock::now();
    const Result result = convertOneImpl(shaderPath, exeDir, base, H2, err, forced, &rd);
    if (!rd.haveSettings || rd.s.report == "off" || H.previewFrame) return result;
    RenderReport rep;
    rep.shader = shaderPath;
    rep.output = rd.output;
    rep.status = result == Result::Ok ? "ok" : result == Result::Cancelled ? "cancelled" : "failed";
    rep.error = result == Result::Failed ? err : L"";
    rep.width = rd.width;
    rep.height = rd.height;
    rep.fps = rd.fps;
    rep.frames = rd.frames;
    rep.mediaSeconds = rd.mediaSeconds;
    rep.loopSeconds = rd.loopSeconds;
    rep.waitSeconds = rd.waitSeconds;
    rep.wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    rep.codec = rd.s.codec;
    rep.container = rd.s.container;
    rep.encoder = rd.encoder;
    rep.gpuImageMs = rd.gpuImageMs;
    rep.gpuBufferMs = rd.gpuBufferMs;
    rep.gpuPostMs = rd.gpuPostMs;
    rep.gpuSamples = rd.gpuSamples;
    for (int k = 0; k < 5; k++) rep.gpuPassMs[k] = rd.gpuPassMs[k];
    rep.loopChecked = rd.loopChecked;
    rep.loopSeam = rd.loopSeam;
    rep.loopTypical = rd.loopTypical;
    rep.loopPerfect = rd.loopPerfect;
    rep.warnings = warnings;
    uint64_t bytes = 0;
    if (!rd.output.empty() && platform::fileSize(rd.output, bytes)) {
        rep.bytes = bytes;
        if (result == Result::Ok) platform::sha256File(rd.output, rep.sha256);
    }
    // a cote de la video : <nom>.report.json / .txt (sans sortie connue : a cote du shader, dans le dossier de sortie choisi)
    std::wstring stem = rd.output.empty() ? outputFile(shaderPath, rd.s, false) : rd.output;
    const size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > stem.find_last_of(L"\\/") + 1) stem = stem.substr(0, dot);
    const std::wstring path = stem + L".report" + (rd.s.report == "text" ? L".txt" : L".json");
    if (writeRenderReport(rep, path, rd.s.report)) H.log(L"  " + TXF("log.report_written", path.c_str()));
    else H.log(L"  " + TXF("log.report_failed", path.c_str()));
    return result;
}

static Result convertOneImpl(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H,
                             std::wstring& err, const Forced* forced, ReportData* rd) {
    if (isMontageFile(shaderPath)) {  // un montage n'est pas un shader : ni apercu ni image fixe
        err = TX("error.montage_preview");
        return Result::Failed;
    }
    std::string code;
    if (!readFile(shaderPath, code)) { err = TX("error.read_file"); return Result::Failed; }
    normalizeShaderText(code, H);
    std::vector<std::wstring> notes;  // adaptations et avertissements de compatibilite, journalises avant le rendu
    code = adaptShaderCode(code, notes);
    if (code.find("mainImage") == std::string::npos && code.find("mainVR") == std::string::npos) {
        err = TX("error.no_main_image");
        return Result::Failed;
    }

    Settings s = base;
    parseLines(s, code, true);
    if (forced)
        for (const auto& kv : *forced) applySetting(s, kv.first, kv.second);
    for (int i = 0; i < 4; i++)  // canal lu par le shader mais sans texture : il lit du noir
        if (s.channel[i].empty() && code.find("iChannel" + std::to_string(i)) != std::string::npos)
            H.log(L"  " + TXF("log.warn_ichannel", i, i));
    if (!forced && !sameSettings(s, base)) H.log(L"  " + TX("log.shader_overrides"));
    Target activeTarget;  // cible de publication : ses valeurs s'appliquent, sauf celles que le shader impose
    bool haveTarget = false;
    if (!s.target.empty() && !H.previewFrame && !H.snapshot) {
        const std::vector<Target> all = publishTargets(exeDir);
        if (const Target* tg = findTarget(all, s.target)) {
            std::vector<std::string> skip;
            for (const Override& o : shaderOverrides(shaderPath)) skip.push_back(o.key);
            if (forced)
                for (const auto& kv : *forced) skip.push_back(kv.first);
            activeTarget = *tg;
            haveTarget = true;
            applyTarget(s, *tg, skip);
            H.log(L"  " + TXF("log.target_applied", tg->label.c_str()));
        } else {
            H.log(L"  " + TXF("log.target_unknown", W(s.target).c_str()));
        }
    }
    s.width = std::max(16, s.width & ~1);
    s.height = std::max(16, s.height & ~1);
    if (rd) {
        rd->s = s;
        rd->haveSettings = true;
    }
    if (s.fps <= 0) s.fps = 60;
    if (s.duration <= 0) s.duration = 10;
    s.crf = std::min(51, std::max(0, s.crf));
    const bool preview = (bool)H.previewFrame;
    const bool sheet = !preview && !H.snapshot && H.sheetCols > 0 && H.sheetRows > 0;  // planche-contact : N images tirees sur la duree
    const bool thumb = !preview && H.snapshot && H.thumbTime != -1;                     // vignette : une image, 1280 pixels au plus
    if (thumb) s.start = H.thumbTime == -2 ? s.start + s.duration / 2 : H.thumbTime;
    const bool videoMode = !preview && !H.snapshot && !sheet;
    const int SMP = (!preview && H.snapshot && !thumb) ? std::min(100000, std::max(1, s.samples)) : 1;  // image fixe : echantillons moyennes
    const OutKind kind = outKind(s.container);
    const bool still = !preview && H.snapshot && !thumb;  // image PNG (16 bits par composante possibles)
    if (videoMode ? (kind == OutKind::Gif || kind == OutKind::Webp) : !still) s.depth = 8;  // 10 bits : video, sequences et image fixe seulement
    if (!videoMode) s.hdr = "off";  // l'apercu et l'image fixe restent en SDR
    if (videoMode && s.alpha && !alphaSupported(s.codec, s.container)) {  // transparence : refusee avant le rendu
        err = TXF("error.alpha_unsupported", codecLabel(s.codec), W(s.container).c_str());
        return Result::Failed;
    }
    if (videoMode && s.hdr != "off") {
        if (!hdrSupported(s.codec, s.container)) {
            err = TXF("error.hdr_unsupported", codecLabel(s.codec), W(s.container).c_str());
            return Result::Failed;
        }
        if (s.alpha) {
            err = TX("error.hdr_alpha");
            return Result::Failed;
        }
        if (s.depth < 10) {  // le HDR demande 10 bits
            s.depth = 10;
            H.log(L"  " + TX("log.hdr_depth"));
        }
    }
    if (videoMode && s.interp > 0) {
        if (kind != OutKind::Video) {
            H.log(L"  " + TX("log.interp_video_only"));
            s.interp = 0;
        } else if (s.alpha) {  // minterpolate ne traite pas l'alpha
            err = TX("error.interp_alpha");
            return Result::Failed;
        } else if (s.depth >= 10) {
            H.log(L"  " + TX("log.interp_8bit"));
        }
    }
    LicenseInfo lic;  // licence et auteur lus dans l'en-tete du shader
    if (videoMode) {
        lic = detectLicense(code);
        if (lic.found) {
            H.log(L"  " + TXF(lic.assumed ? "log.license_assumed" : "log.license", lic.id.empty() ? L"?" : licenseText(lic).c_str(), lic.author.empty() ? L"?" : lic.author.c_str()));
            if (lic.nonCommercial) H.log(L"  " + TX("log.license_nc"));
            if (s.autoCredit && s.credit.empty() && (!lic.author.empty() || !lic.title.empty())) {  // credit incruste : titre, auteur et licence
                s.credit = (lic.title.empty() ? L"" : lic.title + L" - ") + lic.author + (lic.id.empty() ? L"" : L" (" + licenseText(lic) + L")");
                H.log(L"  " + TXF("log.credit_auto", s.credit.c_str()));
            }
        }
    }
    const bool deep = s.depth >= 10;
    const size_t bpc = deep ? 2 : 1;  // octets par composante lue sur le GPU
    const int B = std::min(64, std::max(1, s.blur));  // sous-images par image (flou de mouvement)
    const int D = std::min(8, std::max(2, s.pbo));    // profondeur de la file de lecture GPU

    // Duree : multiple entier de la periode si "loop" est donne (boucle parfaite : la derniere image
    // s'arrete juste avant le retour au point de depart)
    double duration = s.duration;
    if (s.loop > 0) duration = s.loop * std::max(1.0, std::floor(s.duration / s.loop + 0.5));
    const long long N = videoMode ? std::max(1LL, std::llround(duration * s.fps)) : sheet ? (long long)H.sheetCols * H.sheetRows : 1;
    const double totalSec = N / s.fps;
    // Courbe de temps, aller-retour et boucle par fondu : la video seulement (l'apercu et les images fixes restent au temps du shader)
    TimeMap tm;
    const bool crossfade = videoMode && s.loopMode == "crossfade";
    if (videoMode) {
        if (!TimeMap::parse(s.timemap, tm, err)) return Result::Failed;
        if (s.loopMode == "pingpong") {
            if (tm.hasCurve()) {
                err = TX("error.loop_timemap");
                return Result::Failed;
            }
            tm = TimeMap::pingPong(totalSec / 2);
        }
        tm.setSpeed(s.speed);
        if (crossfade && !tm.identity()) {
            err = TX("error.loop_timemap");
            return Result::Failed;
        }
    }
    const double loopFade = std::max(0.05, std::min(s.loopFade, totalSec / 2));
    auto shaderTime = [&](double u) { return s.start + tm.at(u); };  // instant du shader pour l'instant u de la video
    auto deltaAt = [&](long long n) { return tm.identity() ? 1.0 / s.fps : std::max(1e-6, std::fabs(tm.speedAt((double)n / s.fps)) / s.fps); };
    if (rd) {
        rd->frames = N;
        rd->fps = s.interp > 0 && videoMode ? s.interp : s.fps;
        rd->mediaSeconds = totalSec;
        rd->width = s.width;
        rd->height = s.height;
    }
    const int W_ = s.width, H_ = s.height;
    int CW = W_, CH = H_;  // zone gardee apres le recadrage (crop=) : tout le rendu sans recadrage
    if (videoMode && !s.crop.empty() && !cropSize(s.crop, W_, H_, CW, CH)) {
        err = TXF("error.crop_invalid", W(s.crop).c_str());
        return Result::Failed;
    }
    int OW = CW, OH = CH;  // taille de sortie : celle de la zone gardee, ou celle de la mise a l'echelle (scale=)
    if (videoMode) {
        Settings sc = s;
        sc.width = CW;
        sc.height = CH;
        if (!outputSize(sc, OW, OH)) {
            err = TXF("error.scale_invalid", W(s.scale).c_str());
            return Result::Failed;
        }
    }
    if (videoMode && haveTarget)
        for (const std::wstring& w : checkTarget(activeTarget, s, totalSec, OW, OH)) H.log(L"  " + w);
    if (videoMode && (CW != W_ || CH != H_)) H.log(L"  " + TXF("log.crop_applied", W_, H_, CW, CH));
    const int SS = preview ? 1 : std::min(4, std::max(1, s.ss));  // supersampling : reduit par ffmpeg
    const int FW = W_ * SS, FH = H_ * SS;  // taille complete du rendu
    // Image fixe plus grande que le GPU ne le permet (ou `tile` demande) : calculee par tuiles
    GLint maxTexEarly = 0;
    glGetIntegerv(0x0D33 /*GL_MAX_TEXTURE_SIZE*/, &maxTexEarly);
    const int gpuEdge = maxTexEarly > 0 ? (int)maxTexEarly : 4096;
    bool tiled = false;
    int TW = FW, TH = FH;  // taille d'une tuile
    if (still) {
        if (s.tile > 0) {
            const int edge = std::max(SS, std::min(s.tile, gpuEdge) / SS * SS);
            tiled = FW > edge || FH > edge;
            TW = std::min(FW, edge);
            TH = std::min(FH, edge);
        } else if (FW > std::min(gpuEdge, 8192) || FH > std::min(gpuEdge, 8192)) {
            tiled = true;
            TW = std::min(FW, std::max(SS, std::min(gpuEdge, 2048) / SS * SS));
            TH = std::min(FH, std::max(SS, std::min(gpuEdge, 512) / SS * SS));
        }
        if (tiled && (W_ > 65536 || H_ > 65536)) {
            err = TX("error.poster_size");
            return Result::Failed;
        }
    }
    const int RW = tiled ? TW : FW, RH = tiled ? TH : FH;  // taille des cibles de rendu : une tuile, ou l'image entiere
    const int PW = FW, PH = FH;                            // taille des passes (tampons) : toujours l'image entiere
    const size_t frameBytes = (size_t)RW * RH * 4 * bpc;
    bool hasAudio = videoMode && kind == OutKind::Video && !s.audio.empty();
    if (videoMode && kind != OutKind::Video && !s.audio.empty()) H.log(L"  " + TX("log.audio_ignored"));
    if (hasAudio && !fileExists(s.audio)) { err = TXF("error.audio_missing", s.audio.c_str()); return Result::Failed; }

    // Chemin de sortie : dossier choisi (sinon celui du shader), meme nom, extension du conteneur (ou .png)
    if (videoMode && !codecContainerOk(s.codec, s.container)) {
        err = TXF("error.codec_container", codecLabel(s.codec), W(s.container).c_str());
        return Result::Failed;
    }
    if (videoMode && kind == OutKind::Video && isDnxhr(s.codec) && (OW < 256 || OH < 120)) {
        err = TX("error.dnxhr_size");
        return Result::Failed;
    }
    if (!preview && !s.outdir.empty()) platform::makeDir(s.outdir);  // sans effet si deja present
    const bool tiff = still && s.stillFormat == "tiff";  // image fixe en TIFF
    const std::wstring stillExt = tiff ? L".tif" : L".png";
    const std::wstring ext = H.cubeFace >= 0 && H.snapshot ? std::wstring(L"_") + vrFaceName(H.cubeFace) + stillExt : thumb ? L"_thumb" + H.thumbSuffix + L".png" : H.snapshot ? stillExt : sheet ? L"_sheet.png" : L"." + W(s.container);
    bool skipExisting = false;
    const std::wstring out = !videoMode ? outputPath(shaderPath, s.outdir, ext) : outputFile(shaderPath, s, true, &skipExisting);
    if (skipExisting) {
        H.log(L"  " + TXF("log.skip_existing", out.c_str()));
        return Result::Ok;
    }
    const bool sequence = videoMode && kind == OutKind::Sequence;
    const std::wstring seqDir = sequence ? sequenceDir(out) : L"";
    const std::wstring seqPattern = sequence ? sequencePattern(out) : L"";
    const std::wstring& outRef = sequence ? seqDir : out;
    const std::wstring outDir = outRef.substr(0, outRef.find_last_of(L"\\/"));

    // --- Verifications avant de commencer : espace disque ------------------------
    if (videoMode) {
        const double est = estimateSizeBytes(s, totalSec, hasAudio);
        uint64_t freeB = 0;
        if (platform::freeSpace(outDir, freeB)) {
            H.log(L"  " + TXF("log.size_estimate", est / 1048576.0, freeB / 1048576.0));
            const bool chunkedGuess = s.chunk > 0 && N > std::llround(s.chunk * s.fps);
            const double need = est * (chunkedGuess ? 2.2 : 1.2) + 50e6;  // les parties s'ajoutent au fichier final
            if ((double)freeB < need) {
                err = TXF("error.disk_full", outDir.c_str(), need / 1048576.0, freeB / 1048576.0);
                return Result::Failed;
            }
        }
        H.log(TXF("log.render_summary", OW, OH, s.interp > 0 ? s.interp : s.fps, totalSec, N,
                   s.bitrate > 0 ? wfmt(L"%d kb/s", s.bitrate).c_str() : wfmt(L"crf %d", s.crf).c_str(),
                   W(s.preset).c_str(), kind == OutKind::Video ? codecLabel(s.codec) : containerAt(containerIndex(s.container)).label));
        if (deep) H.log(L"  " + TX("log.depth10"));
        if (kind == OutKind::Gif && s.fps > 30) H.log(L"  " + TXF("log.gif_fps", s.fps));
        if (kind == OutKind::Gif && (double)OW * OH * N * 3 > 2.0e9) {
            err = TXF("error.gif_too_big", OW, OH, (long long)N);
            return Result::Failed;
        }
        if (OW != CW || OH != CH) H.log(L"  " + TXF("log.scale_output", CW, CH, OW, OH, W(s.scaleFilter).c_str()));
        if (s.interp > 0) H.log(L"  " + TXF("log.interp", s.fps, s.interp));
        if (s.alpha) H.log(L"  " + TX("log.alpha_on"));
        if (s.hdr != "off") H.log(L"  " + TXF("log.hdr_on", s.hdr == "pq" ? L"HDR10 (PQ, BT.2020)" : L"HLG (BT.2020)", s.hdrWhite));
        if (s.loudnorm != 0) H.log(L"  " + TXF("log.loudnorm", s.loudnorm));
        if (B > 1) H.log(L"  " + TXF("log.motion_blur", B, s.shutter));
        if (s.loop > 0) H.log(L"  " + TXF("log.perfect_loop", s.loop, duration));
        if (crossfade) H.log(L"  " + TXF("log.loop_crossfade", loopFade));
        if (tm.pingpong()) H.log(L"  " + TXF("log.loop_pingpong", totalSec / 2));
        if (!tm.identity() && !tm.pingpong()) {
            double lo = 1, hi = 1;
            tm.speedRange(totalSec, lo, hi);
            H.log(L"  " + TXF("log.timemap", lo, hi));
        }
    }

    if (videoMode && s.precheck && !H.cancelled()) {  // controle rapide avant le rendu : flashs et valeurs invalides, avertissements seulement
        Hooks Hc = H;
        Hc.progress = nullptr;
        Hc.previewFrame = nullptr;
        Hc.output = nullptr;
        CheckOptions co;
        co.alias = false;
        co.portability = false;
        co.flicker = false;
        co.maxSamples = 600;
        CheckReport cr;
        std::wstring ce;
        if (runChecks(shaderPath, exeDir, s, co, Hc, cr, ce)) {
            for (const CheckIssue& i : cr.issues) H.log(L"  " + std::wstring(i.severity >= 1 ? L"! " : L"- ") + i.text);
            std::wstring stem = outputFile(shaderPath, s, false);  // rapport a cote de la video : <nom>.check.json et .check.txt
            const size_t dot = stem.find_last_of(L'.');
            if (dot != std::wstring::npos && dot > stem.find_last_of(L"\\/") + 1) stem = stem.substr(0, dot);
            if (!s.outdir.empty()) platform::makeDir(s.outdir);
            writeCheckReport(cr, stem);
            if (cr.issues.empty()) H.log(L"  " + TXF("log.check.none_short", cr.samples));
        } else {
            H.log(L"  " + TXF("log.check.failed", ce.c_str()));
        }
    }
    // --- Shader + FBO + PBO ---------------------------------------------------
    int vrMode = 0;  // mainVR : 360 equirectangulaire mono (1), stereo haut / bas (2), cote a cote (3), anaglyphe (4), fulldome (5), face de cubemap (6)
    if (H.cubeFace >= 0 && H.snapshot && !preview) {  // une face de cubemap : exige mainVR
        Settings forceVr = s;
        forceVr.vr = "mono";
        if (!vrModeFor(code, forceVr, vrMode, err)) return Result::Failed;
        vrMode = 6;
    } else if (!vrModeFor(code, s, vrMode, err)) {
        return Result::Failed;
    }
    if (vrMode > 0 && !preview) {
        const char* const vrLog[8] = {"", "log.compat_vr", "log.vr_stereo", "log.vr_sbs", "log.vr_anaglyph", "log.vr_fulldome", "log.vr_face", "log.vr_fulldome_flat"};
        H.log(L"  " + TX(vrLog[vrMode]));
        if (!vrAspectOk(W_, H_, vrMode)) H.log(L"  " + TXF("log.vr_aspect", W_, H_, vrAspectText(vrMode)));
    }
    int eyeW = FW, eyeH = FH;  // iResolution du programme principal : un oeil en stereo
    vrEyeSize(vrMode, FW, FH, eyeW, eyeH);
    BufferPasses bp;  // Buffer A-D et Common : fichiers a cote du shader
    PostChain post;   // post-traitement en virgule flottante (passe .post.glsl, effets, LUT)
    bp.discover(shaderPath, [&](const std::wstring& p, std::string& out) {
        if (!readFile(p, out)) return false;
        normalizeShaderText(out, H);
        out = adaptShaderCode(out, notes);
        return true;
    }, H);
    if (!preview) {
        checkCompatibility(code + "\n" + bp.allCode(), s, notes);
        bp.report(s, code, notes);
        for (const auto& n : notes) H.log(L"  " + n);
    }
    if (videoMode && bp.any() && crossfade) {  // le deuxieme tirage du fondu demanderait l'etat des tampons a un autre instant, en meme temps que le premier
        err = TX("error.loop_history");
        bp.release();
        return Result::Failed;
    }
    // Passes a historique avec un temps qui recule (aller-retour, courbe decroissante) : l'etat ne se calcule que dans le sens du temps, il est donc
    // rejoue depuis le debut de la plage chaque fois que le temps recule (cout borne, annonce)
    const bool replayHist = videoMode && bp.any() && (tm.pingpong() || (!tm.identity() && !tm.monotone()));
    if (replayHist) {
        constexpr long long kReplayLimit = 400000;
        long long steps = 0, pos = -(long long)s.warmup;
        for (long long n = 0; n < N; n++) {
            const long long m = std::max(0LL, std::llround(tm.at((double)n / s.fps) * s.fps));
            if (m < pos) pos = -(long long)s.warmup;
            steps += m + 1 - pos;
            pos = m + 1;
        }
        if (steps > kReplayLimit) {
            err = TXF("error.timemap_history", steps, kReplayLimit);
            bp.release();
            return Result::Failed;
        }
        H.log(L"  " + TXF("log.timemap_replay", steps));
    }
    // Son : la vitesse constante peut etre suivie (atempo, hauteur conservee) ; une courbe, l'aller-retour ou une vitesse negative ne le peuvent pas
    double followSpeed = 1.0;
    if (videoMode && !tm.identity() && (hasAudio || bp.hasSound())) {
        if (s.audioFollow && !tm.hasCurve() && !tm.pingpong() && s.speed > 0) {
            followSpeed = s.speed;
            H.log(L"  " + TXF("log.audio_follow", followSpeed));
        } else {
            H.log(L"  " + TX(s.audioFollow ? "log.audio_follow_unsupported" : "log.timemap_audio"));
        }
    }
    const double soundSec = totalSec * followSpeed;  // duree de son necessaire : la video montre `followSpeed` secondes de shader par seconde
    // Parametres (// uniform ...) : declarations injectees dans tous les programmes, valeurs reglees a chaque image
    ParamSet params;
    {
        std::vector<std::wstring> pnotes;
        if (!params.load(code + "\n" + bp.allCode(), s, paramsFile(exeDir, shaderPath), err, &pnotes)) return Result::Failed;
        if (!preview)
            for (const auto& n : pnotes) H.log(L"  " + n);
        if (params.needsAudio()) {
            if (s.audio.empty()) {
                if (!preview) H.log(L"  " + TX("log.param_audio_missing"));
            } else if (!params.setAudio(findFfmpeg(exeDir), s.audio, err)) {
                return Result::Failed;
            }
        }
        bp.setPreamble(params.declarations());
    }
    std::string chTypes[4];
    channelTypes(s, chTypes);
    GLuint prog = buildProgram(bp.withCommon(code), err, false, chTypes, false, false, vrMode, params.declarations(), s.alpha);
    if (!prog) {  // erreurs « fichier:ligne: message », cliquables dans le journal
        err = locateGlslErrors(err, shaderPath, 1 + (int)std::count(code.begin(), code.end(), '\n'), bp.commonPath());
        return Result::Failed;
    }

    ChannelSet chans;
    GLuint vao = 0, tex = 0, fbo = 0, accTex = 0, accFbo = 0, present = 0;
    GLuint pbo[8] = {0};
    auto cleanupGL = [&] {
        ogl::BindBuffer(ogl::K_PIXEL_PACK_BUFFER, 0);
        if (pbo[0]) ogl::DeleteBuffers(D, pbo);
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, 0);
        if (fbo) ogl::DeleteFramebuffers(1, &fbo);
        if (accFbo) ogl::DeleteFramebuffers(1, &accFbo);
        if (tex) glDeleteTextures(1, &tex);
        if (accTex) glDeleteTextures(1, &accTex);
        ogl::UseProgram(0);
        freeChannels(chans);
        post.release();
        bp.release();
        if (present) ogl::DeleteProgram(present);
        ogl::DeleteProgram(prog);
    };

    GLint maxTex = 0;  // limite du GPU
    glGetIntegerv(0x0D33 /*GL_MAX_TEXTURE_SIZE*/, &maxTex);
    if (maxTex > 0 && (RW > maxTex || RH > maxTex)) {
        err = TXF("error.render_size", RW, RH, (int)maxTex);
        cleanupGL();
        return Result::Failed;
    }

    ogl::GenVertexArrays(1, &vao);
    ogl::BindVertexArray(vao);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, deep ? ogl::K_RGBA16 : ogl::K_RGBA8, RW, RH, 0, GL_RGBA, deep ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ogl::GenFramebuffers(1, &fbo);
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);
    ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (ogl::CheckFramebufferStatus(ogl::K_FRAMEBUFFER) != ogl::K_FRAMEBUFFER_COMPLETE) {
        err = TX("error.framebuffer");
        cleanupGL();
        return Result::Failed;
    }

    if (B > 1 || crossfade || SMP > 1) {  // accumulation flottante : flou de mouvement, fondu de boucle, echantillons d'une image fixe
        present = buildPresentProgram(err, s.alpha);
        if (!present) { cleanupGL(); return Result::Failed; }
        glGenTextures(1, &accTex);
        glBindTexture(GL_TEXTURE_2D, accTex);
        glTexImage2D(GL_TEXTURE_2D, 0, ogl::K_RGBA32F, RW, RH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ogl::GenFramebuffers(1, &accFbo);
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, accFbo);
        ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accTex, 0);
        if (ogl::CheckFramebufferStatus(ogl::K_FRAMEBUFFER) != ogl::K_FRAMEBUFFER_COMPLETE) {
            err = TX("error.blur_unavailable");
            cleanupGL();
            return Result::Failed;
        }
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);
    }
    const std::wstring shaderDir = shaderPath.find_last_of(L"\\/") == std::wstring::npos ? L"" : shaderPath.substr(0, shaderPath.find_last_of(L"\\/") + 1);
    if (tiled && bp.any() && ((long long)FW * FH > 8192LL * 8192 || FW > gpuEdge || FH > gpuEdge)) {  // les tampons sont calcules une fois a pleine taille : ils doivent tenir
        err = TXF("error.tile_passes", FW, FH);
        cleanupGL();
        return Result::Failed;
    }
    if (!bp.build(PW, PH, shaderDir, findFfmpeg(exeDir), H, err, s.start, s.fps, &s)) {
        cleanupGL();
        return Result::Failed;
    }
    if (!post.build(s, bp, shaderDir, findFfmpeg(exeDir), H, err, RW, RH, params.declarations())) {
        cleanupGL();
        return Result::Failed;
    }
    if (tiled && post.active()) {  // le post-traitement lit les voisins de chaque pixel : pas decoupable
        err = TX("error.tile_post");
        cleanupGL();
        return Result::Failed;
    }
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);  // la creation des tampons a change la cible
    glViewport(0, 0, RW, RH);

    ogl::UseProgram(prog);
    GLint uRes = ogl::GetUniformLocation(prog, "iResolution");
    GLint uTime = ogl::GetUniformLocation(prog, "iTime");
    GLint uDelta = ogl::GetUniformLocation(prog, "iTimeDelta");
    GLint uFrame = ogl::GetUniformLocation(prog, "iFrame");
    GLint uRate = ogl::GetUniformLocation(prog, "iFrameRate");
    GLint uMouse = ogl::GetUniformLocation(prog, "iMouse");
    GLint uDate = ogl::GetUniformLocation(prog, "iDate");
    GLint uSR = ogl::GetUniformLocation(prog, "iSampleRate");
    GLint uWeight = ogl::GetUniformLocation(prog, "st_weight");
    ogl::Uniform3f(uRes, (float)eyeW, (float)eyeH, 1.0f);  // stereo : iResolution = taille d'un oeil
    ogl::Uniform1i(ogl::GetUniformLocation(prog, "st_face"), std::max(0, H.cubeFace));
    const GLint uEye = ogl::GetUniformLocation(prog, "st_eye");
    ogl::Uniform1f(ogl::GetUniformLocation(prog, "st_ipd"), (float)s.vrIpd);
    ogl::Uniform1f(uDelta, (float)(1.0 / s.fps));
    ogl::Uniform1f(uRate, (float)s.fps);
    // iMouse simule : valeur en pixels du rendu au temps t (voir mouse.h)
    auto setMouse = [&](GLint loc, double t) {
        float m[4];
        mouseAt(s.mouseKeys, t, 1.0 / s.fps, FW, FH, m);
        ogl::Uniform4f(loc, m[0], m[1], m[2], m[3]);
    };
    setMouse(uMouse, s.start);
    if (!loadChannels(s, shaderDir, findFfmpeg(exeDir), chans, H, err, PW, PH, s.start, s.fps)) {  // canaux d'entree (textures, tampons)
        cleanupGL();
        return Result::Failed;
    }
    bp.fixChannelSizes(chans);
    bindChannels(chans, prog);
    ogl::Uniform1f(uSR, 44100.0f);
    ogl::Uniform1f(uWeight, 1.0f);
    const DateInput dateIn = dateInputFrom(s.date);  // date fixe ou reelle au depart, qui avance avec iTime
    auto setDate = [&](GLint loc, double t) {
        float d[4];
        dateAt(dateIn, t, d);
        ogl::Uniform4f(loc, d[0], d[1], d[2], d[3]);
    };

    // Passes de tampon : memes variables que l'image, renseignees pour chaque programme
    bool inSim = false;  // en cours de simulation des tampons (historique rejoue) : le temps est celui de l'image simulee, pas celui de la video
    auto setPassUniforms = [&](GLuint p, long long n) {
        const double t = inSim ? s.start + (double)n / s.fps : shaderTime((double)n / s.fps);
        ogl::Uniform3f(ogl::GetUniformLocation(p, "iResolution"), (float)PW, (float)PH, 1.0f);
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iTime"), (float)t);
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iTimeDelta"), (float)(inSim ? 1.0 / s.fps : deltaAt(n)));
        ogl::Uniform1i(ogl::GetUniformLocation(p, "iFrame"), (GLint)(n + s.warmup));
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iFrameRate"), (float)s.fps);
        setMouse(ogl::GetUniformLocation(p, "iMouse"), t);
        setDate(ogl::GetUniformLocation(p, "iDate"), t);
        params.apply(p, t);
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iSampleRate"), 44100.0f);
    };
    // Les tampons avancent image par image : si le rendu demarre plus loin (reprise, apercu), on rejoue les images
    // precedentes sans rien encoder
    // Chauffe (warmup) : les N images avant la premiere sont simulees aussi, et iFrame est decale de N
    long long simFrame = -(long long)s.warmup;
    bool replayLogged = false;
    // Temps GPU par passe : mesure sur quelques images seulement (le GPU est vide avant et apres, ce qui coute du debit) ; ms cumules
    bool timing = false;
    double gpuMs[3] = {0, 0, 0};  // image, tampons, post-traitement
    double gpuPassMs[5] = {0, 0, 0, 0, 0};  // par passe : Buffer A a D, Cubemap A
    int gpuSamples = 0;
    using Tick = std::chrono::steady_clock::time_point;
    auto tickStart = [&]() -> Tick {
        glFinish();
        return std::chrono::steady_clock::now();
    };
    auto tickStop = [&](Tick t0, int slot) {
        glFinish();
        gpuMs[slot] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    };
    auto advanceBuffers = [&](long long n) {
        if (!bp.any()) return;
        const Tick tb = timing ? tickStart() : Tick();
        bp.timePasses = timing;
        long long target = n;  // image des tampons voulue : celle de l'instant du shader quand le temps est decale ou recule
        if (replayHist) {
            target = std::max(0LL, std::llround(tm.at((double)n / s.fps) * s.fps));
            if (target < simFrame) {
                bp.reset();
                simFrame = -(long long)s.warmup;
            }
            inSim = true;
        }
        for (; simFrame < target && !H.cancelled(); simFrame++) {
            if (!replayLogged && !replayHist) {
                H.log(L"  " + TXF("log.buffers_replay", (long long)(n + s.warmup)));
                replayLogged = true;
            }
            bp.step(simFrame, replayHist ? s.start + (double)simFrame / s.fps : shaderTime((double)simFrame / s.fps), PW, PH, setPassUniforms);
        }
        bp.step(target, replayHist ? s.start + (double)target / s.fps : shaderTime((double)target / s.fps), PW, PH, setPassUniforms);
        inSim = false;
        simFrame = target + 1;
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);  // retour a l'image
        glViewport(0, 0, RW, RH);
        ogl::UseProgram(prog);
        if (timing) {
            tickStop(tb, 1);
            for (int k = 0; k < 5; k++) gpuPassMs[k] += bp.passMs[k];
            for (double& v : bp.passMs) v = 0;
            bp.timePasses = false;
        }
    };

    auto drawAt = [&](double t, long long n, long long extraFrame = 0) {
        ogl::Uniform1f(uTime, (float)t);
        if (!tm.identity()) ogl::Uniform1f(uDelta, (float)deltaAt(n));
        setMouse(uMouse, t);
        ogl::Uniform1i(uFrame, (GLint)(n + s.warmup + extraFrame));
        setDate(uDate, t);
        params.apply(prog, t);
        if (vrMode == 4) {  // anaglyphe : l'oeil gauche ne dessine que le rouge (et l'alpha), le droit le vert et le bleu
            ogl::Uniform1f(uEye, -1.0f);
            glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            ogl::Uniform1f(uEye, 1.0f);
            glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    };
    // Rend l'image n (temps t, facteur de fondu "fade") dans fbo ;
    // avec flou : moyenne de B sous-images etalees sur l'obturateur
    auto prepareFrame = [&](long long n, double t) {
        updateChannels(chans, t, n);  // audio, video et clavier au temps exact de l'image
        setChannelTimes(chans, prog);
        if (chans.anyBuffer()) {  // le tampon le plus recent change a chaque image
            GLuint latest[5];
            bp.latest(latest);
            bindChannels(chans, prog, latest);
        }
    };
    // Fin d'une image : post-traitement en virgule flottante (le fondu s'applique a la fin de la chaine)
    auto finishFrame = [&](long long n, double t, float fade) {
        if (!post.active()) return;
        const Tick tp = timing ? tickStart() : Tick();
        GLuint latest[5] = {0, 0, 0, 0, 0};
        if (bp.any()) bp.latest(latest);
        post.apply(fbo, n, t, fade, setPassUniforms, latest);
        ogl::UseProgram(prog);
        if (timing) tickStop(tp, 2);
    };
    // Plusieurs echantillons d'un meme instant (image fixe) : iFrame different a chaque tirage, moyenne en virgule flottante ;
    // une interruption garde ce qui est deja accumule (la moyenne est remise a l'echelle)
    auto renderSamples = [&](long long n, double t, float fade) {
        prepareFrame(n, t);
        const bool pp = post.active();
        const GLuint imgFbo = pp ? post.inputFbo() : fbo;
        const float weight = pp ? 1.0f : fade;
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, accFbo);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        ogl::Uniform1f(uWeight, weight / SMP);
        ogl::Uniform1i(ogl::GetUniformLocation(prog, "st_premul"), s.alpha ? 1 : 0);
        const auto t0 = std::chrono::steady_clock::now();
        auto tLog = t0;
        int done = 0;
        for (int k = 0; k < SMP; k++) {
            if (k > 0 && H.cancelled()) break;
            drawAt(t, n, k);
            done++;
            if ((k & 7) == 7) glFinish();  // un tirage long ne doit pas bloquer le pilote graphique
            const auto now = std::chrono::steady_clock::now();
            if (now - tLog > std::chrono::milliseconds(250) || done == SMP) {
                tLog = now;
                const double speed = done / std::max(1e-6, std::chrono::duration<double>(now - t0).count());
                if (H.progress) H.progress(done, SMP, speed, (SMP - done) / speed);
            }
        }
        glDisable(GL_BLEND);
        ogl::Uniform1i(ogl::GetUniformLocation(prog, "st_premul"), 0);
        ogl::Uniform1f(uWeight, 1.0f);
        if (done < SMP) H.log(L"  " + TXF("log.samples_partial", done, SMP));
        else H.log(L"  " + TXF("log.samples_done", SMP));
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, imgFbo);
        ogl::UseProgram(present);
        ogl::Uniform1f(ogl::GetUniformLocation(present, "st_scale"), (float)SMP / (float)done);
        glBindTexture(GL_TEXTURE_2D, accTex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        ogl::UseProgram(prog);
        finishFrame(n, t, fade);
    };
    auto renderFrame = [&](long long n, double t, float fade) {
        prepareFrame(n, t);
        // avec post-traitement, l'image va dans une texture flottante (fondu applique a la fin de la chaine)
        const bool pp = post.active();
        const GLuint imgFbo = pp ? post.inputFbo() : fbo;
        const float weight = pp ? 1.0f : fade;
        // Tirages de l'image : sous-images du flou de mouvement (reparties sur l'obturateur, dans le temps du shader : en ralenti le flou
        // se reduit tout seul) et, pour la boucle par fondu, deuxieme tirage une duree plus loin dont le poids monte de 0 a 1 en loopFade secondes
        struct Draw {
            double t;
            float w;
        };
        std::vector<Draw> draws;
        const Tick ti = timing ? tickStart() : Tick();
        const double u = (double)n / s.fps;
        for (int k = 0; k < B; k++) {
            const double dk = B > 1 ? (double)k / B * s.shutter / s.fps : 0.0;
            const double tk = t + (tm.at(u + dk) - tm.at(u));
            const float wk = 1.0f / B;
            if (crossfade) {
                const double x = std::min(1.0, std::max(0.0, (u + dk) / loopFade));
                const float m = (float)(x * x * (3.0 - 2.0 * x));
                if (m < 1.0f) draws.push_back({tk + totalSec, wk * (1.0f - m)});  // la fin de la boucle, une duree plus loin
                if (m > 0.0f) draws.push_back({tk, wk * m});
            } else {
                draws.push_back({tk, wk});
            }
        }
        if (draws.size() == 1) {
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, imgFbo);
            ogl::Uniform1f(uWeight, weight * draws[0].w);
            drawAt(draws[0].t, n);
        } else {
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, accFbo);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            ogl::Uniform1i(ogl::GetUniformLocation(prog, "st_premul"), s.alpha ? 1 : 0);  // alpha : somme de couleurs prémultipliées
            for (const Draw& d : draws) {
                ogl::Uniform1f(uWeight, weight * d.w);
                drawAt(d.t, n);
            }
            glDisable(GL_BLEND);
            ogl::Uniform1i(ogl::GetUniformLocation(prog, "st_premul"), 0);
            ogl::Uniform1f(uWeight, 1.0f);
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, imgFbo);
            ogl::UseProgram(present);
            ogl::Uniform1f(ogl::GetUniformLocation(present, "st_scale"), 1.0f);
            glBindTexture(GL_TEXTURE_2D, accTex);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            ogl::UseProgram(prog);
        }
        if (timing) tickStop(ti, 0);
        finishFrame(n, t, fade);
    };

    // --- Apercu : une image renvoyee a l'appelant, sans ffmpeg -------------------
    if (preview) {
        long long pf = 0;
        double pt = H.previewTime;
        if (bp.any()) {  // les tampons dependent de l'historique : on les simule jusqu'a l'image voulue (600 images au plus)
            pf = std::min(600LL, std::max(0LL, std::llround((H.previewTime - s.start) * s.fps)));
            pt = s.start + (double)pf / s.fps;
            advanceBuffers(pf);
        }
        renderFrame(pf, pt, 1.0f);
        std::vector<uint8_t> px(frameBytes);
        glReadPixels(0, 0, RW, RH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        std::vector<uint8_t> flipped = previewBgra(px.data(), RW, RH, s.alpha);  // haut en bas, BGRA ; transparence sur un damier
        cleanupGL();
        H.previewFrame(RW, RH, std::move(flipped));
        return Result::Ok;
    }

    // --- Image fixe par tuiles : chaque tuile est calculee avec le iResolution complet et un fragCoord decale (st_offset), lue sur le GPU, reduite du
    // sur-echantillonnage, rangee dans une bande ; chaque bande est encodee en PNG par ffmpeg (bandes gardees pour reprendre), puis les bandes sont empilees.
    if (tiled) {
        const std::wstring ffm = findFfmpeg(exeDir);
        const int nx = (FW + TW - 1) / TW, ny = (FH + TH - 1) / TH;
        const long long totalTiles = (long long)nx * ny;
        const std::wstring tilesDir = out.substr(0, out.find_last_of(L'.')) + L".tiles";
        platform::makeDir(tilesDir);
        std::string sigSrc = code + params.signature() + "|poster|" + std::to_string(W_) + "x" + std::to_string(H_) + "|" + std::to_string(SS) + "|" + std::to_string(TW) + "x" + std::to_string(TH) +
                             "|" + std::to_string(deep) + std::to_string(s.alpha) + "|" + std::to_string(SMP) + "|" + std::to_string(s.start) + "|" + std::to_string(s.dpi) + "|" +
                             std::to_string(vrMode) + std::to_string(s.vrIpd) + "|" + std::to_string(H.cubeFace) + "|" + s.date + "|" + formatMouseKeys(s.mouseKeys) + "|" + std::to_string(s.warmup);
        for (int i = 0; i < 4; i++) sigSrc += "|" + U8(s.channel[i]) + s.chFilter[i] + s.chWrap[i] + std::to_string(s.chVflip[i]);
        const std::string sig = fnvHex(sigSrc);
        std::string oldSig;
        const std::wstring sigFile = tilesDir + L"/signature.txt";
        if (!readFile(sigFile, oldSig) || trim(oldSig) != sig) {  // autre image : les anciennes bandes sont jetees
            clearDir(tilesDir);
            if (FILE* f = platform::openFile(sigFile, "wb")) {
                fputs(sig.c_str(), f);
                fclose(f);
            }
        }
        H.log(L"  " + TXF("log.tiles", nx, ny, TW, TH, W_, H_));
        const wchar_t* inFmt = deep ? L"rgba64le" : L"rgba";
        const wchar_t* outFmt = s.alpha ? (deep ? L"rgba64be" : L"rgba") : (deep ? L"rgb48be" : L"rgb24");  // bandes : toujours en PNG
        const wchar_t* finalFmt = !tiff ? outFmt : s.alpha ? (deep ? L"rgba64le" : L"rgba") : (deep ? L"rgb48le" : L"rgb24");
        const std::wstring dpiArg = s.dpi > 0 ? wfmt(L" -dpi %d", s.dpi) : std::wstring();
        const int OW = W_;
        const GLint uOff = ogl::GetUniformLocation(prog, "st_offset");
        std::vector<uint8_t> tileBuf((size_t)TW * TH * 4 * bpc);
        if (bp.any()) advanceBuffers(0);  // les tampons de l'image fixe : une fois, a pleine taille ; chaque tuile les lit
        long long doneTiles = 0;
        bool stopped = false;
        std::vector<std::wstring> strips;
        auto fail = [&](const std::wstring& m) {
            err = m;
            cleanupGL();
            return Result::Failed;
        };
        for (int bi = 0; bi < ny && !stopped; bi++) {  // bi = 0 : bande du haut (GL a l'origine en bas)
            const int ty = ny - 1 - bi;
            const int r0 = ty * TH, r1 = std::min(FH, r0 + TH);
            const int bh = (r1 - r0) / SS;
            const std::wstring strip = tilesDir + L"/" + wfmt(L"band_%05d.png", bi);
            strips.push_back(strip);
            if (fileExists(strip)) {
                doneTiles += nx;
                continue;
            }
            std::vector<uint8_t> band((size_t)OW * bh * 4 * bpc, 0);
            for (int tx = 0; tx < nx; tx++) {
                if (H.cancelled()) {
                    stopped = true;
                    break;
                }
                const int c0 = tx * TW, c1 = std::min(FW, c0 + TW);
                ogl::Uniform2f(uOff, (float)c0, (float)r0);
                if (SMP > 1) renderSamples(0, s.start, 1.0f);
                else renderFrame(0, s.start, 1.0f);
                ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);
                glReadPixels(0, 0, RW, RH, GL_RGBA, deep ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, tileBuf.data());
                const int cols = (c1 - c0) / SS, ox0 = c0 / SS;
                auto copyTile = [&](auto* src, auto* dst) {  // moyenne de SS x SS pixels ; premiere ligne du haut dans la bande
                    const unsigned area = (unsigned)(SS * SS);
                    for (int y = 0; y < bh; y++)
                        for (int x = 0; x < cols; x++)
                            for (int c = 0; c < 4; c++) {
                                unsigned long long sum = 0;
                                for (int sy = 0; sy < SS; sy++)
                                    for (int sx = 0; sx < SS; sx++) sum += src[((size_t)(y * SS + sy) * RW + (size_t)(x * SS + sx)) * 4 + c];
                                dst[((size_t)(bh - 1 - y) * OW + (size_t)(ox0 + x)) * 4 + c] = (std::remove_pointer_t<decltype(dst)>)((sum + area / 2) / area);
                            }
                };
                if (deep) copyTile((const uint16_t*)tileBuf.data(), (uint16_t*)band.data());
                else copyTile((const uint8_t*)tileBuf.data(), (uint8_t*)band.data());
                doneTiles++;
                if (H.progress) H.progress(doneTiles, totalTiles, 0.0, 0.0);
            }
            if (stopped) break;
            const std::wstring tmp = strip + L".part.png";
            const std::wstring cmd = L"\"" + ffm + L"\" -hide_banner -loglevel error -y -f rawvideo -pix_fmt " + inFmt + L" -video_size " + std::to_wstring(OW) + L"x" + std::to_wstring(bh) +
                                     L" -i - -frames:v 1 -pix_fmt " + outFmt + dpiArg + L" \"" + tmp + L"\"";
            auto proc = platform::Process::start(platform::splitCommand(cmd), true, true);
            if (!proc) return fail(TX("error.ffmpeg_launch"));
            bool wrote = true;
            for (size_t off = 0; off < band.size() && wrote; off += (size_t)16 << 20)
                wrote = proc->writeStdin(band.data() + off, std::min((size_t)16 << 20, band.size() - off));
            proc->closeStdin();
            char drain[4096];
            std::string ffLog;
            size_t got;
            while ((got = proc->readOutput(drain, sizeof(drain))) > 0) ffLog.append(drain, got);
            const int code2 = proc->wait();
            if (!wrote || code2 != 0 || !platform::moveFile(tmp, strip, true)) {
                if (!ffLog.empty()) H.log(TX("log.ffmpeg_prefix") + W(trim(ffLog)));
                return fail(TX("error.ffmpeg_failed"));
            }
        }
        if (stopped) {
            H.log(L"  " + TXF("log.tiles_interrupted", (long long)doneTiles, totalTiles, tilesDir.c_str()));
            cleanupGL();
            return Result::Cancelled;
        }
        H.log(L"  " + TX("log.assembling"));
        bool joined = true;
        if (strips.size() == 1 && !tiff) {
            joined = platform::moveFile(strips[0], out, true);
        } else if (strips.size() == 1) {  // une seule bande en TIFF : conversion
            const std::wstring cmd = L"\"" + ffm + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + strips[0] + L"\" -frames:v 1 -pix_fmt " + finalFmt + L" -compression_algo deflate" + dpiArg + L" \"" + out + L"\"";
            joined = runProcess(cmd, [&](const std::wstring& line) { H.log(TX("log.ffmpeg_prefix") + line); }) == 0;
        } else {
            std::wstring cmd = L"\"" + ffm + L"\" -hide_banner -loglevel error -nostdin -y";
            std::wstring labels;
            for (size_t i = 0; i < strips.size(); i++) {
                cmd += L" -i \"" + strips[i] + L"\"";
                labels += L"[" + std::to_wstring(i) + L":v]";
            }
            cmd += L" -filter_complex \"" + labels + L"vstack=inputs=" + std::to_wstring(strips.size()) + L"[v]\" -map [v] -frames:v 1 -pix_fmt " + finalFmt + (tiff ? L" -compression_algo deflate" : L"") + dpiArg + L" \"" + out + L"\"";
            joined = runProcess(cmd, [&](const std::wstring& line) { H.log(TX("log.ffmpeg_prefix") + line); }) == 0;
        }
        if (!joined) return fail(TX("error.assemble_failed"));
        clearDir(tilesDir);
        platform::removeDir(tilesDir);
        cleanupGL();
        if (!tiff) pngTagSrgb(out);  // declare l'image en sRGB
        H.log(L"  " + TXF("log.ok_output", out.c_str()));
        if (H.output) H.output(out);
        if (rd) rd->output = out;
        return Result::Ok;
    }

    ogl::GenBuffers(D, pbo);
    for (int i = 0; i < D; i++) {
        ogl::BindBuffer(ogl::K_PIXEL_PACK_BUFFER, pbo[i]);
        ogl::BufferData(ogl::K_PIXEL_PACK_BUFFER, (ptrdiff_t)frameBytes, nullptr, ogl::K_STREAM_READ);
    }

    // --- ffmpeg (ffmpeg.exe a cote du programme) ------------------------------
    const std::wstring ffmpeg = findFfmpeg(exeDir);
    if (videoMode && !formatAvailable(ffmpeg, s.codec, s.container)) {
        err = TXF("error.encoder_missing", W(requiredEncoders(s.codec, s.container).front()).c_str());
        cleanupGL();
        return Result::Failed;
    }
    if (videoMode) {  // texte incruste : drawtext et une police doivent exister (verifie avant le premier rendu)
        std::wstring unused;
        if (!overlayFilter(s, 0, OW, OH, ffmpeg, unused, err)) {
            cleanupGL();
            return Result::Failed;
        }
    }
    const std::string enc = videoMode && kind == OutKind::Video ? resolveEncoder(s, ffmpeg, H) : "cpu";
    if (rd) rd->encoder = enc;

    const std::wstring fpsStr = wfmt(L"%g", s.fps);
    // reduction du supersampling (area), puis mise a l'echelle de sortie (scale= : filtre au choix) ; les deux finissent par une virgule
    const std::wstring cropFilter = (videoMode && (CW != W_ || CH != H_)) ? wfmt(L"crop=%d:%d,", CW, CH) : std::wstring();
    const std::wstring stageB = cropFilter + (videoMode ? outScaleFilter(s, CW, CH, OW, OH) : L"");  // ce qui reste a faire apres le rendu : recadrage, mise a l'echelle
    const std::wstring ssFilter = (SS > 1 ? L"scale=" + std::to_wstring(W_) + L":" + std::to_wstring(H_) + L":flags=area," : L"") + stageB;
    const std::wstring head = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -y" +
        std::wstring(deep ? L" -f rawvideo -pix_fmt rgba64le -video_size " : L" -f rawvideo -pix_fmt rgba -video_size ") + std::to_wstring(RW) + L"x" + std::to_wstring(RH) +
        L" -framerate " + fpsStr + L" -i -";
    const ColorSpec cs = colorSpecFor(s);
    const double fi = videoMode ? std::max(0.0, std::min(s.fadein, totalSec)) : 0.0;
    const double fo = videoMode ? std::max(0.0, std::min(s.fadeout, totalSec)) : 0.0;
    // Fondus d'image : appliques dans le shader (facteur par image), donc exacts meme par parties
    auto fadeAt = [&](long long n) -> float {
        double f = 1.0;
        if (fi > 0) f = std::min(f, n / (fi * s.fps));
        if (fo > 0) f = std::min(f, (N - 1 - n) / (fo * s.fps));
        return (float)std::max(0.0, std::min(1.0, f));
    };
    // Passe Sound (<nom>.sound.glsl) : rendue en WAV avant la video, puis ajoutee comme piste audio ; le reglage « audio » est prioritaire
    std::wstring audioFile = s.audio;
    bool audioSeek = true;  // -ss : seulement pour un fichier audio complet (le WAV produit commence deja a « start »)
    struct WavGuard {
        std::wstring path;
        ~WavGuard() {
            if (!path.empty()) platform::removeFile(path);
        }
    } wavGuard;
    if (bp.hasSound() && videoMode && kind == OutKind::Video) {
        if (!s.audio.empty()) {
            H.log(L"  " + TX("log.sound_audio_wins"));
        } else {
            H.log(L"  " + TX("log.sound_rendering"));
            wavGuard.path = out + L".sound.wav";
            const SoundResult sr = renderSoundWav(bp.withCommon(bp.soundCode()), bp.soundPath(), 1 + (int)std::count(bp.soundCode().begin(), bp.soundCode().end(), '\n'),
                                                  bp.commonPath(), s.start, soundSec, wavGuard.path, H, err);
            if (sr != SoundResult::Ok) {
                wavGuard.path.clear();
                cleanupGL();
                return sr == SoundResult::Cancelled ? Result::Cancelled : Result::Failed;
            }
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);  // l'etat du rendu principal, modifie par la passe Sound
            glViewport(0, 0, RW, RH);
            ogl::UseProgram(prog);
            ogl::BindVertexArray(vao);
            audioFile = wavGuard.path;
            audioSeek = false;
            hasAudio = true;
        }
    }
    std::wstring audioFilter;
    if (followSpeed != 1.0) {  // atempo accepte 0.5 a 2 : on enchaine
        double f = followSpeed;
        while (f > 2.0) {
            audioFilter += L"atempo=2.0,";
            f /= 2.0;
        }
        while (f < 0.5) {
            audioFilter += L"atempo=0.5,";
            f /= 0.5;
        }
        audioFilter += wfmt(L"atempo=%.6f,", f);
    }
    audioFilter += loudnormFilter(s);  // normalisation du son (LUFS), puis fondus
    if (fi > 0) audioFilter += wfmt(L"afade=t=in:st=0:d=%g,", fi);
    if (fo > 0) audioFilter += wfmt(L"afade=t=out:st=%g:d=%g,", totalSec - fo, fo);
    audioFilter += L"apad";  // son plus court que la video : complete par du silence
    const std::wstring audioArgs = (audioSeek ? wfmt(L" -ss %g", s.start) : std::wstring()) + L" -i \"" + audioFile + L"\"";

    // Metadonnees du conteneur : titre = nom du shader, commentaire = taille, cadence et version
    std::wstring metaArgs;
    if (s.metadata && videoMode && kind == OutKind::Video) {
        std::wstring title = shaderPath.substr(shaderPath.find_last_of(L"\\/") + 1);
        if (size_t dot = title.find_last_of(L'.'); dot != std::wstring::npos && dot > 0) title.erase(dot);
        title.erase(std::remove(title.begin(), title.end(), L'"'), title.end());
        metaArgs = L" -metadata \"title=" + title + L"\" -metadata \"comment=" + wfmt(L"%dx%d, %g fps, ", OW, OH, s.interp > 0 ? s.interp : s.fps) +
                   APP_NAME_WSTR + L" " + APP_VER_WSTR + L"\"";
        if (lic.found) {  // auteur et licence du shader dans le fichier
            auto clean = [](std::wstring v) {
                v.erase(std::remove(v.begin(), v.end(), L'"'), v.end());
                return v;
            };
            if (!lic.author.empty()) metaArgs += L" -metadata \"author=" + clean(lic.author) + L"\"";
            if (!lic.id.empty()) metaArgs += L" -metadata \"license=" + licenseText(lic) + L"\" -metadata \"copyright=" + clean(lic.author) + L" (" + licenseText(lic) + L")\"";
        }
    }

    const std::wstring masterPix = deep ? L"rgba64le" : s.alpha ? L"bgra" : L"bgr0";  // format des pixels du maitre sans perte
    // Tout ce qui est regle pour une image depuis la boucle de rendu
    using clk = std::chrono::steady_clock;
    double waitSec = 0, loopSec = 0;  // mesure : temps bloque sur ffmpeg / temps total de rendu

    // Rend les images [n0, n1) vers ffmpeg -> dest (muxAudio : son inclus). doneBefore : images deja faites (progression).
    auto encodeRange = [&](long long n0, long long n1, const std::wstring& dest, bool muxAudio, long long doneBefore,
                           std::wstring& e, bool master = false) -> Result {
        std::wstring cmd;
        if (master) {  // maitre : les images retournees et reduites du sur-echantillonnage, sans perte
            cmd = head + L" -vf \"vflip" + (SS > 1 ? L",scale=" + std::to_wstring(W_) + L":" + std::to_wstring(H_) + L":flags=area" : std::wstring()) + L"\" -c:v ffv1 -level 3 -pix_fmt " +
                  masterPix + L" \"" + dest + L"\"";
        } else if (sheet) {  // planche-contact : les N images sont assemblees en grille par le filtre tile (vignettes de 1920 pixels de large au total au plus)
            const int tw = std::max(160, std::min(W_, 1920 / H.sheetCols)) & ~1;
            cmd = head + L" -vf \"vflip," + ssFilter + wfmt(L"scale=%d:-2:flags=area,tile=%dx%d:padding=4:margin=4:color=0x202020,format=rgb24\"", tw, H.sheetCols, H.sheetRows) +
                  L" -frames:v 1 -pix_fmt rgb24 \"" + dest + L"\"";
        } else if (H.snapshot) {  // image RGB pleine plage, sans conversion YUV
            const std::wstring thumbScale = thumb ? L"scale='min(1280,iw)':-2:flags=area," : L"";
            const std::wstring still8 = tiff ? (s.alpha ? (deep ? L"rgba64le" : L"rgba") : (deep ? L"rgb48le" : L"rgb24")) : (s.alpha ? (deep ? L"rgba64be" : L"rgba") : (deep ? L"rgb48be" : L"rgb24"));
            cmd = head + L" -vf \"vflip," + ssFilter + thumbScale + L"format=" + still8 + L"\" -frames:v 1 -pix_fmt " + still8 + (tiff ? L" -compression_algo deflate" : L"") + (s.dpi > 0 && !thumb ? wfmt(L" -dpi %d", s.dpi) : std::wstring()) + L" \"" + dest + L"\"";
        } else if (kind != OutKind::Video) {  // image animee ou sequence : RGB pleine plage, pas de YUV
            std::wstring text;  // texte incruste (le temps de la video : une image par 1/fps)
            std::wstring oe;
            overlayFilter(s, (double)n0 / s.fps, OW, OH, ffmpeg, text, oe);
            cmd = head + L" -vf \"vflip," + ssFilter + text + imageFilterTail(s) + L"\"" + imageEncoderArgs(s) + L" \"" + dest + L"\"";
        } else {
            std::wstring text, oe;
            overlayFilter(s, (double)n0 / s.fps, OW, OH, ffmpeg, text, oe);  // la partie commence a n0 / fps dans la video
            cmd = head;
            if (muxAudio) cmd += audioArgs;
            cmd += L" -vf \"vflip," + ssFilter + text + L"scale=in_range=full:out_range=" + cs.range +
                   L":out_color_matrix=" + cs.matrix + L":flags=accurate_rnd+full_chroma_int," + interpFilter(s) + outFormat(s, enc) + L"\"";
            if (muxAudio) cmd += L" -map 0:v -map 1:a" + audioCodecArgs(s.container) + L" -af \"" + audioFilter + L"\" -shortest";
            cmd += encoderArgs(s, enc) + outPixFmtArg(s, enc) + L" " + cs.tags + containerArgs(s.container) + metaArgs + L" \"" + dest + L"\"";
        }

        // stdin de ffmpeg = les images ; stdout/stderr = tube lu par un thread et envoye au journal
        auto proc = platform::Process::start(platform::splitCommand(cmd), true, true);
        if (!proc) {
            e = TX("error.ffmpeg_launch");
            return Result::Failed;
        }

        std::thread errReader([&] {
            std::string acc;
            char buf[4096];
            size_t got;
            while ((got = proc->readOutput(buf, sizeof(buf))) > 0) {
                acc.append(buf, got);
                size_t p;
                while ((p = acc.find('\n')) != std::string::npos) {
                    std::string line = trim(acc.substr(0, p));
                    acc.erase(0, p + 1);
                    if (!line.empty()) H.log(TX("log.ffmpeg_prefix") + W(line));
                }
            }
            acc = trim(acc);
            if (!acc.empty()) H.log(TX("log.ffmpeg_prefix") + W(acc));
        });

        FrameQueue q;
        q.proc = proc.get();
        q.frameBytes = frameBytes;
        q.bufs.assign(4, std::vector<uint8_t>(frameBytes));
        for (int i = 0; i < 4; i++) q.freeQ.push(i);
        std::thread writer(writerThread, &q);

        bool aborted = false;
        auto fetch = [&](long long idx) -> bool {
            ogl::BindBuffer(ogl::K_PIXEL_PACK_BUFFER, pbo[idx % D]);
            const uint8_t* src = (const uint8_t*)ogl::MapBuffer(ogl::K_PIXEL_PACK_BUFFER, ogl::K_READ_ONLY);
            if (!src) return false;
            int slot;
            {
                std::unique_lock<std::mutex> l(q.m);
                auto w0 = clk::now();
                while (q.freeQ.empty() && !q.failed) {
                    if (H.cancelled()) {
                        ogl::UnmapBuffer(ogl::K_PIXEL_PACK_BUFFER);
                        return false;
                    }
                    q.cv.wait_for(l, std::chrono::milliseconds(100));
                }
                waitSec += std::chrono::duration<double>(clk::now() - w0).count();
                if (q.failed) {
                    ogl::UnmapBuffer(ogl::K_PIXEL_PACK_BUFFER);
                    return false;
                }
                slot = q.freeQ.front();
                q.freeQ.pop();
            }
            memcpy(q.bufs[slot].data(), src, frameBytes);
            ogl::UnmapBuffer(ogl::K_PIXEL_PACK_BUFFER);
            {
                std::lock_guard<std::mutex> l(q.m);
                q.fullQ.push(slot);
            }
            q.cv.notify_all();
            return true;
        };

        const long long cnt = n1 - n0;
        auto t0 = clk::now(), tLast = t0;
        for (long long k = 0; k < cnt && !aborted; ++k) {
            if (H.paused()) {  // pause : on attend, sans compter ce temps dans la vitesse affichee
                auto ps = clk::now();
                while (H.paused() && !H.cancelled()) platform::sleepMs(50);
                auto d = clk::now() - ps;
                t0 += d;
                tLast += d;
            }
            if (H.cancelled()) { aborted = true; break; }
            long long n = n0 + k;
            double t = shaderTime((double)n / s.fps);  // temps exact
            if (sheet) {  // vignette k : au milieu du k-ieme intervalle de la plage rendue ; n = l'image de ce temps (les tampons y sont rejoues)
                t = s.start + ((double)n + 0.5) * duration / (double)N;
                n = std::llround((t - s.start) * s.fps);
            }
            timing = videoMode && k % std::max(1LL, cnt / 24) == 0;  // une vingtaine d'images mesurees
            advanceBuffers(n);
            if (SMP > 1) renderSamples(n, t, 1.0f);
            else renderFrame(n, t, videoMode ? fadeAt(n) : 1.0f);

            if (timing) {
                gpuSamples++;
                if (H.gpuTimes) H.gpuTimes(gpuMs[0] / gpuSamples, gpuMs[1] / gpuSamples, gpuMs[2] / gpuSamples);
            }
            timing = false;
            ogl::BindBuffer(ogl::K_PIXEL_PACK_BUFFER, pbo[k % D]);
            glReadPixels(0, 0, RW, RH, GL_RGBA, deep ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, nullptr);
            if (k >= D - 1 && !fetch(k - (D - 1))) aborted = true;

            auto now = clk::now();
            if (now - tLast > std::chrono::milliseconds(250) || k == cnt - 1) {
                tLast = now;
                double el = std::chrono::duration<double>(now - t0).count();
                double speed = (k + 1) / std::max(el, 1e-6);
                if (H.progress) H.progress(doneBefore + k + 1, N, speed, (N - doneBefore - k - 1) / speed);
            }
        }
        for (long long m = std::max(0LL, cnt - (D - 1)); m < cnt && !aborted; ++m)
            if (!fetch(m)) aborted = true;
        loopSec += std::chrono::duration<double>(clk::now() - t0).count();

        // --- Fin : vider la file, fermer le pipe, attendre ffmpeg ----------------
        const bool cancelled = aborted && H.cancelled();
        if (cancelled) proc->terminate();  // debloque aussi le thread d'ecriture
        {
            std::lock_guard<std::mutex> l(q.m);
            q.finished = true;
        }
        q.cv.notify_all();
        writer.join();
        proc->closeStdin();
        const int code2 = proc->wait();
        errReader.join();

        if (cancelled) {
            platform::removeFile(dest);  // fichier partiel inutilisable
            return Result::Cancelled;
        }
        if (aborted || code2 != 0) {
            e = TX("error.ffmpeg_failed");
            return Result::Failed;
        }
        return Result::Ok;
    };

    // --- Plan : un seul fichier, ou des parties reprenables puis assemblees ----------
    // --- Maitre sans perte en cache, taille cible, formats tires du maitre ---------------------------------------------------------------------
    const bool masterKind = videoMode && (kind == OutKind::Video || kind == OutKind::Gif || kind == OutKind::Webp);
    const bool wantTarget = videoMode && kind == OutKind::Video && s.targetSizeMB > 0;
    const bool useMaster = masterKind && (s.masterCache || wantTarget);
    if (videoMode && !masterKind && (s.masterCache || s.targetSizeMB > 0)) H.log(L"  " + TX("log.master_unsupported"));
    // Tout ce qui change les images rendues (et rien de ce qui ne change que l'encodage) : l'empreinte du maitre
    auto frameSig = [&]() {
        char nums[256];
        snprintf(nums, sizeof(nums), "|%d|%d|%d|%.9g|%.9g|%lld|%d|%.9g|%.9g|%.9g|%d|%d|%d|%.9g|%d", W_, H_, SS, s.fps, s.start, (long long)N, B, s.shutter, fi, fo, (int)deep, (int)s.alpha,
                 vrMode, s.vrIpd, s.warmup);
        std::string sg = code + "|fr|" + bp.allCode() + params.signature() + nums + "|" + tm.canonical() + "|loop:" + s.loopMode + "," + std::to_string(loopFade) + "|" + APP_VER_STR;
        std::string lutBytes;
        if (!s.lut.empty()) {
            const bool absolute = s.lut.size() > 1 && (s.lut[1] == L':' || s.lut[0] == L'/' || s.lut[0] == L'\\');
            readFile(absolute ? s.lut : shaderDir + s.lut, lutBytes);
        }
        sg += "|post:" + s.post + "|" + fnvHex(bp.postCode()) + "|" + fnvHex(lutBytes) + "|" + s.date + "|" + formatMouseKeys(s.mouseKeys) + "|" + formatKeyPresses(s.keys);
        for (int i = 0; i < 4; i++) {
            sg += "|ch" + std::to_string(i) + ":" + U8(s.channel[i]) + s.chFilter[i] + s.chWrap[i] + std::to_string(s.chVflip[i]);
            sg += "|buf" + std::to_string(i) + ":" + std::to_string(s.bufScale[i]) + "," + std::to_string(s.bufW[i]) + "x" + std::to_string(s.bufH[i]);
        }
        return sg;
    };
    // Lance une commande ffmpeg ; la progression et l'annulation sont suivies. Rend le code de sortie, ou -1 si annule.
    auto runFfmpeg = [&](const std::wstring& cmd, long long frameTotal) -> int {
        auto proc = platform::Process::start(platform::splitCommand(cmd), false, true);
        if (!proc) return -2;
        std::atomic<long long> frameNow{0};
        std::atomic<bool> finished{false};
        std::thread reader([&] {
            std::string acc;
            char buf[4096];
            size_t got;
            while ((got = proc->readOutput(buf, sizeof(buf))) > 0) {
                acc.append(buf, got);
                size_t p;
                while ((p = acc.find_first_of("\r\n")) != std::string::npos) {
                    const std::string line = trim(acc.substr(0, p));
                    acc.erase(0, p + 1);
                    if (line.compare(0, 6, "frame=") == 0) frameNow = atoll(line.c_str() + 6);
                    else if (!line.empty()) H.log(TX("log.ffmpeg_prefix") + W(line));
                }
            }
            finished = true;
        });
        bool cancelledRun = false;
        while (!finished) {
            if (H.cancelled()) {
                proc->terminate();
                cancelledRun = true;
                break;
            }
            if (H.progress) H.progress(std::min(frameNow.load(), frameTotal), frameTotal, 0.0, 0.0);
            platform::sleepMs(100);
        }
        reader.join();
        const int rc = proc->wait();
        return cancelledRun ? -1 : rc;
    };
    // Rend le maitre : par parties reprenables (comme une video), puis assemblage sans reencodage
    auto renderMaster = [&](const std::wstring& masterPath) -> Result {
        platform::makeDir(cacheDir(exeDir));
        const std::wstring base = masterPath.substr(0, masterPath.find_last_of(L'.'));
        const std::wstring tmpMaster = base + L".part.mkv";
        const long long cf = s.chunk > 0 ? std::max(1LL, std::llround(s.chunk * s.fps)) : 0;
        Result r = Result::Ok;
        if (cf == 0 || N <= cf) {
            r = encodeRange(0, N, tmpMaster, false, 0, err, true);
        } else {
            const std::wstring partsDir = base + L".parts";
            platform::makeDir(partsDir);
            const long long nChunks = (N + cf - 1) / cf;
            long long done = 0;
            int resumed = 0;
            std::vector<std::wstring> parts;
            for (long long i = 0; i < nChunks && r == Result::Ok; i++) {
                const long long n0 = i * cf, n1 = std::min(N, n0 + cf);
                const std::wstring part = partsDir + L"/" + wfmt(L"%05lld", i) + L".mkv";
                parts.push_back(part);
                if (fileExists(part)) {
                    done += n1 - n0;
                    resumed++;
                    continue;
                }
                if (resumed) {
                    H.log(L"  " + TXF("log.resume", resumed, (long long)nChunks));
                    resumed = 0;
                }
                const std::wstring tmp = partsDir + L"/" + wfmt(L"%05lld", i) + L".part.mkv";
                r = encodeRange(n0, n1, tmp, false, done, err, true);
                if (r == Result::Ok && !platform::moveFile(tmp, part, true)) {
                    err = TXF("error.part_finalize", partsDir.c_str());
                    r = Result::Failed;
                }
                done += n1 - n0;
            }
            if (r == Result::Ok) {
                H.log(L"  " + TX("log.assembling"));
                std::string list;
                for (const auto& p : parts) list += "file '" + U8(p.substr(p.find_last_of(L"\\/") + 1)) + "'\n";
                writeFileAtomic(partsDir + L"/list.txt", list);
                const int rc = runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -f concat -safe 0 -i \"" + partsDir + L"/list.txt\" -c copy \"" + tmpMaster + L"\"",
                                          [&](const std::wstring& line) { H.log(TX("log.ffmpeg_prefix") + line); });
                if (rc != 0) {
                    err = TX("error.assemble_failed");
                    r = Result::Failed;
                } else {
                    clearDir(partsDir);
                    platform::removeDir(partsDir);
                }
            } else if (r == Result::Cancelled) {
                H.log(L"  " + TXF("log.interrupted", (long long)std::count_if(parts.begin(), parts.end(), [](const std::wstring& p) { return fileExists(p); }), (long long)nChunks, partsDir.c_str()));
            }
        }
        if (r == Result::Ok && !platform::moveFile(tmpMaster, masterPath, true)) {
            err = TXF("error.part_finalize", cacheDir(exeDir).c_str());
            r = Result::Failed;
        }
        return r;
    };
    // Commande qui tire le fichier final du maitre : memes filtres, memes encodeurs que le rendu direct, sans le retournement ni la reduction du
    // sur-echantillonnage (deja faits dans le maitre). pass : 0 normal, 1 premiere passe (rien n'est ecrit), 2 seconde passe.
    auto deriveCmd = [&](const std::wstring& masterPath, int pass, const std::wstring& passLog, int kbps) -> std::wstring {
        Settings sd = s;
        if (kbps > 0) sd.bitrate = kbps;
        std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -stats -y -i \"" + masterPath + L"\"";
        std::wstring text, oe;
        overlayFilter(sd, 0.0, OW, OH, ffmpeg, text, oe);
        if (kind != OutKind::Video) return cmd + L" -vf \"" + stageB + text + imageFilterTail(sd) + L"\"" + imageEncoderArgs(sd) + L" \"" + out + L"\"";
        const bool withAudio = hasAudio && pass != 1;
        if (withAudio) cmd += audioArgs;
        cmd += L" -vf \"" + stageB + text + L"scale=in_range=full:out_range=" + cs.range + L":out_color_matrix=" + cs.matrix + L":flags=accurate_rnd+full_chroma_int," +
               interpFilter(sd) + outFormat(sd, enc) + L"\"";
        if (withAudio) cmd += L" -map 0:v -map 1:a" + audioCodecArgs(s.container) + L" -af \"" + audioFilter + L"\" -shortest";
        else cmd += L" -an";
        cmd += encoderArgs(sd, enc) + outPixFmtArg(sd, enc) + L" " + cs.tags;
        if (pass == 1) return cmd + L" -pass 1 -passlogfile \"" + passLog + L"\" -f null -";
        if (pass == 2) cmd += L" -pass 2 -passlogfile \"" + passLog + L"\"";
        return cmd + containerArgs(s.container) + metaArgs + L" \"" + out + L"\"";
    };
    auto deriveFlow = [&](const std::wstring& masterPath, const std::wstring& fp) -> Result {
        if (!wantTarget) {
            const int rc = runFfmpeg(deriveCmd(masterPath, 0, L"", 0), N);
            if (rc == -1) {
                platform::removeFile(out);
                return Result::Cancelled;
            }
            if (rc != 0) {
                err = TX("error.ffmpeg_failed");
                return Result::Failed;
            }
            return Result::Ok;
        }
        // Taille cible : debit calcule a partir de la duree, du son et du conteneur ; deux passes pour les encodeurs logiciels ; le fichier obtenu est
        // mesure et, s'il depasse, l'encodage recommence avec un debit corrige
        const int audioK = hasAudio ? (s.container == "webm" ? 160 : 192) : 0;
        BitratePlan plan;
        if (!planBitrate(s.targetSizeMB, totalSec, audioK, plan)) {
            err = TXF("error.targetsize_small", s.targetSizeMB, totalSec);
            return Result::Failed;
        }
        const uint64_t targetBytes = (uint64_t)(s.targetSizeMB * 1e6);
        const bool twoPass = enc == "cpu" && (s.codec == "h264" || s.codec == "h265" || s.codec == "vp9" || s.codec == "av1");
        const std::wstring passLog = cacheDir(exeDir) + L"/" + fp + L"_pass";
        int kbps = plan.videoKbps;
        H.log(L"  " + TXF("log.targetsize_plan", s.targetSizeMB, kbps, audioK, TX(twoPass ? "log.targetsize_2pass" : "log.targetsize_1pass").c_str()));
        auto cleanPassFiles = [&] {
            std::vector<platform::DirEntry> files;
            platform::listDir(cacheDir(exeDir), files);
            for (const auto& en : files)
                if (!en.isDir && en.name.compare(0, fp.size() + 5, fp + L"_pass") == 0) platform::removeFile(cacheDir(exeDir) + L"/" + en.name);
        };
        for (int attempt = 1; attempt <= 4; attempt++) {
            if (twoPass) {
                const int rc1 = runFfmpeg(deriveCmd(masterPath, 1, passLog, kbps), N);
                if (rc1 == -1) {
                    cleanPassFiles();
                    return Result::Cancelled;
                }
                if (rc1 != 0) {
                    cleanPassFiles();
                    err = TX("error.ffmpeg_failed");
                    return Result::Failed;
                }
            }
            const int rc = runFfmpeg(deriveCmd(masterPath, twoPass ? 2 : 0, passLog, kbps), N);
            if (rc == -1) {
                platform::removeFile(out);
                cleanPassFiles();
                return Result::Cancelled;
            }
            if (rc != 0) {
                cleanPassFiles();
                err = TX("error.ffmpeg_failed");
                return Result::Failed;
            }
            uint64_t sz = 0;
            platform::fileSize(out, sz);
            H.log(L"  " + TXF("log.targetsize_attempt", attempt, kbps, (double)sz / 1e6, s.targetSizeMB));
            if (sz <= targetBytes) {
                cleanPassFiles();
                return Result::Ok;
            }
            kbps = correctedBitrate(kbps, sz, targetBytes);
            if (kbps < 20 || attempt == 4) {
                platform::removeFile(out);
                cleanPassFiles();
                err = TXF("error.targetsize_miss", s.targetSizeMB, (double)sz / 1e6);
                return Result::Failed;
            }
        }
        return Result::Failed;
    };
    auto masterFlow = [&]() -> Result {
        const std::string sg = frameSig();
        const std::wstring fp = W(fnvHex(sg) + fnvHex(sg + "#2"));
        const std::wstring masterPath = cacheDir(exeDir) + L"/" + fp + L".mkv";
        if (fileExists(masterPath)) {  // meme empreinte : aucun calcul GPU
            H.log(L"  " + TXF("log.master_reuse", masterPath.c_str()));
        } else {
            const Result r = renderMaster(masterPath);
            if (r != Result::Ok) return r;
        }
        cacheTouch(masterPath, shaderPath);
        const Result r = deriveFlow(masterPath, fp);
        cacheTrim(exeDir, (uint64_t)(std::max(0.1, s.cacheMaxGB) * 1e9), masterPath);
        if (!s.masterCache) {  // maitre demande seulement par la taille cible : il n'est pas garde
            platform::removeFile(masterPath);
            platform::removeFile(masterPath.substr(0, masterPath.find_last_of(L'.')) + L".txt");
        }
        return r;
    };
    const long long chunkFrames = (videoMode && kind == OutKind::Video && s.chunk > 0) ? std::max(1LL, std::llround(s.chunk * s.fps)) : 0;
    Result result = Result::Ok;
    if (wantTarget && (s.codec.compare(0, 7, "prores_") == 0 || isDnxhr(s.codec))) {  // ces codecs n'ont pas de debit : le poids est fixe par l'image
        err = TXF("error.targetsize_codec", codecLabel(s.codec));
        result = Result::Failed;
    } else if (useMaster) {
        result = masterFlow();
    } else if (chunkFrames == 0 || N <= chunkFrames) {
        if (sequence && !platform::dirExists(seqDir)) platform::makeDir(seqDir);
        result = encodeRange(0, N, sequence ? seqPattern : out, hasAudio, 0, err);
    } else {
        const std::wstring partsDir = out.substr(0, out.find_last_of(L'.')) + L".parts";
        platform::makeDir(partsDir);
        // Signature : tout ce qui change le resultat. Differente -> les anciennes parties sont jetees.
        std::string sigSrc;
        {
            std::wstring tail = wfmt(L"|%d|%d|%g|%g|%d|%ls|%ls|%ls|%d|%d|%ls|%g|%g|%g|%d|%ls|%lld|%lld|%d", W_, H_, s.fps,
                                     s.start, s.crf, W(s.preset).c_str(), W(s.codec).c_str(), W(enc).c_str(), s.bitrate,
                                     s.blur, W(s.color).c_str(), s.shutter, s.fadein, s.fadeout, s.ss,
                                     W(s.container).c_str(), N, chunkFrames, s.depth);
            sigSrc = code + U8(tail) + "|vr" + std::to_string(vrMode) + (vrMode >= 2 && vrMode <= 4 ? "|" + std::to_string(s.vrIpd) : "") + params.signature();
            {  // post-traitement : passe .post, effets et contenu du fichier LUT
                std::string lutBytes;
                if (!s.lut.empty()) {
                    const bool absolute = s.lut.size() > 1 && (s.lut[1] == L':' || s.lut[0] == L'/' || s.lut[0] == L'\\');
                    readFile(absolute ? s.lut : shaderDir + s.lut, lutBytes);
                }
                sigSrc += "|post:" + s.post + "|" + fnvHex(bp.postCode()) + "|" + fnvHex(lutBytes);
                sigSrc += "|" + tm.canonical() + "|loop:" + s.loopMode + "," + std::to_string(loopFade);
                // sortie : tout ce qui change les images ou l'encodage d'une partie
                sigSrc += "|out:" + std::to_string(s.alpha) + s.hdr + std::to_string(s.hdrPeak) + "," + std::to_string(s.hdrFall) + "," +
                          std::to_string(s.hdrWhite) + "|" + std::to_string(s.interp) + "|" + std::to_string(OW) + "x" + std::to_string(OH) + s.scaleFilter +
                          "|" + U8(s.title) + "|" + U8(s.credit) + "|" + std::to_string(s.titleDur);
            }
        }
        const std::string sig = fnvHex(sigSrc);
        const std::wstring sigFile = partsDir + L"/signature.txt";
        std::string old;
        if (!readFile(sigFile, old) || trim(old) != sig) {
            clearDir(partsDir);
            if (FILE* f = platform::openFile(sigFile, "wb")) {
                fputs(sig.c_str(), f);
                fclose(f);
            }
        }

        const long long nChunks = (N + chunkFrames - 1) / chunkFrames;
        long long done = 0;
        int resumed = 0;
        bool resumeLogged = false;
        std::vector<std::wstring> parts;
        for (long long i = 0; i < nChunks && result == Result::Ok; i++) {
            const long long n0 = i * chunkFrames, n1 = std::min(N, n0 + chunkFrames);
            const std::wstring part = partsDir + L"/" + wfmt(L"%05lld", i) + ext;
            parts.push_back(part);
            if (fileExists(part)) {  // partie deja terminee lors d'un rendu precedent
                done += n1 - n0;
                resumed++;
                continue;
            }
            if (resumed && !resumeLogged) {
                H.log(L"  " + TXF("log.resume", resumed, (long long)nChunks));
                resumeLogged = true;
            }
            const std::wstring tmp = partsDir + L"/" + wfmt(L"%05lld", i) + L".part" + ext;
            result = encodeRange(n0, n1, tmp, false, done, err);
            if (result == Result::Ok) {
                if (!platform::moveFile(tmp, part, true)) {
                    err = TXF("error.part_finalize", partsDir.c_str());
                    result = Result::Failed;
                }
            }
            done += n1 - n0;
        }
        if (result == Result::Ok) {  // assemblage : copie des flux, son ajoute ici
            H.log(L"  " + TX("log.assembling"));
            const std::wstring listFile = partsDir + L"/list.txt";
            std::string list;
            for (const auto& p : parts) list += "file '" + U8(p.substr(p.find_last_of(L"\\/") + 1)) + "'\n";
            if (FILE* f = platform::openFile(listFile, "wb")) {
                fputs(list.c_str(), f);
                fclose(f);
            }
            std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -f concat -safe 0 -i \"" + listFile + L"\"";
            if (hasAudio) cmd += audioArgs;
            cmd += L" -map 0:v";
            if (hasAudio) cmd += L" -map 1:a" + audioCodecArgs(s.container) + L" -af \"" + audioFilter + L"\" -shortest";
            cmd += L" -c:v copy" + std::wstring(s.codec == "h265" ? L" -tag:v hvc1" : L"") + containerArgs(s.container) + metaArgs +
                   L" \"" + out + L"\"";
            int rc = runProcess(cmd, [&](const std::wstring& line) { H.log(TX("log.ffmpeg_prefix") + line); });
            if (rc != 0) {
                err = TX("error.assemble_failed");
                result = Result::Failed;
            } else {
                clearDir(partsDir);
                platform::removeDir(partsDir);
            }
        } else if (result == Result::Cancelled) {
            H.log(L"  " + TXF("log.interrupted",
                       (long long)std::count_if(parts.begin(), parts.end(), [](const std::wstring& p) { return fileExists(p); }),
                       (long long)nChunks, partsDir.c_str()));
        }
    }
    cleanupGL();

    if (rd) {
        rd->loopSeconds = loopSec;
        rd->waitSeconds = waitSec;
        if (gpuSamples > 0) {
            rd->gpuSamples = gpuSamples;
            rd->gpuImageMs = gpuMs[0] / gpuSamples;
            rd->gpuBufferMs = gpuMs[1] / gpuSamples;
            rd->gpuPostMs = gpuMs[2] / gpuSamples;
            for (int k = 0; k < 5; k++) rd->gpuPassMs[k] = gpuPassMs[k] / gpuSamples;
        }
    }
    if (result == Result::Ok && videoMode && gpuSamples > 0)
        H.log(L"  " + TXF("log.gpu_passes", gpuSamples, gpuMs[0] / gpuSamples, gpuMs[1] / gpuSamples, gpuMs[2] / gpuSamples));
    if (result == Result::Ok && videoMode && kind == OutKind::Video && s.embed) {  // source embarquee : le fichier est copie avec la balise shadertomp4
        const std::wstring iniFile = cacheDir(exeDir) + L"/embed.ini", metaFile = cacheDir(exeDir) + L"/embed.ffmetadata";
        platform::makeDir(cacheDir(exeDir));
        writeIni(iniFile, s);
        std::string ini;
        readFile(iniFile, ini);
        const SourceBundle bundle = makeBundle(shaderPath, ini);
        std::vector<std::pair<std::string, std::string>> extra;
        extra.push_back({"title", U8(shaderPath.substr(shaderPath.find_last_of(L"\\/") + 1))});
        extra.push_back({"comment", U8(wfmt(L"%dx%d, %g fps, ", OW, OH, s.interp > 0 ? s.interp : s.fps) + APP_NAME_WSTR + L" " + APP_VER_WSTR)});
        if (lic.found && !lic.author.empty()) extra.push_back({"author", U8(lic.author)});
        if (lic.found && !lic.id.empty()) extra.push_back({"license", U8(licenseText(lic))});
        const std::wstring tmpOut = out.substr(0, out.find_last_of(L'.')) + L".embed" + out.substr(out.find_last_of(L'.'));
        const bool tags = s.container == "mp4" || s.container == "mov";
        if (writeBundleMetadata(metaFile, bundle, extra) &&
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + out + L"\" -i \"" + metaFile + L"\" -map 0 -c copy -map_metadata 1" +
                           (tags ? L" -movflags +faststart+use_metadata_tags" : L"") + L" \"" + tmpOut + L"\"",
                       [&](const std::wstring& line) { H.log(TX("log.ffmpeg_prefix") + line); }) == 0 &&
            platform::moveFile(tmpOut, out, true)) {
            H.log(L"  " + TXF("log.embed_done", (long long)bundle.files.size()));
        } else {
            platform::removeFile(tmpOut);
            H.log(L"  " + TX("log.embed_failed"));
        }
        platform::removeFile(iniFile);
        platform::removeFile(metaFile);
        if (platform::dirExists(cacheDir(exeDir))) platform::removeDir(cacheDir(exeDir));  // sans effet si le cache contient des maitres
    }
    if (result == Result::Ok && videoMode && kind == OutKind::Video && s.animPreview != "off" && !s.alpha) {  // apercu anime : tire de la video produite
        const bool webp = s.animPreview == "webp";
        const std::wstring dest = out.substr(0, out.find_last_of(L'.')) + L"_preview" + (webp ? L".webp" : L".gif");
        if (makeAnimPreview(ffmpeg, out, dest, webp, totalSec)) H.log(L"  " + TXF("log.anim_preview", dest.c_str()));
        else H.log(L"  " + TX("log.anim_preview_failed"));
    }
    if (result == Result::Ok && videoMode && kind == OutKind::Video && s.markers != "off" && !H.cancelled()) {  // temps forts et sections : chapitres et fichiers de reperes
        std::wstring e;
        if (s.audio.empty()) H.log(L"  " + TX("log.markers_no_audio"));
        else if (!params.loadRhythm(ffmpeg, s.audio, e)) H.log(L"  " + e);
        else applyMarkerSetting(s.markers, *params.rhythm(), ffmpeg, out, s.start, totalSec, followSpeed, s.interp > 0 ? s.interp : s.fps, [&](const std::wstring& l) { H.log(L"  " + l); });
    }
    if (result == Result::Ok) {
        if (videoMode && loopSec > 1.0 && waitSec > 0.3 * loopSec)
            H.log(L"  " + TXF("log.measure_encoder_bound",
                       100.0 * waitSec / loopSec));
        else if (videoMode && loopSec > 1.0)
            H.log(L"  " + TXF("log.measure_gpu_bound", 100.0 * waitSec / loopSec));
        if (vrMode >= 1 && vrMode <= 4 && videoMode && kind == OutKind::Video && s.metadata) {  // video spherique reconnue par YouTube et les lecteurs 360
            std::wstring metaErr;
            if (s.container != "mp4" && s.container != "mov") H.log(L"  " + TXF("log.vr_meta_skipped", W(s.container).c_str()));
            else if (injectSphericalMetadata(out, vrMode == 2 ? 1 : vrMode == 3 ? 2 : 0, metaErr)) H.log(L"  " + TX("log.vr_meta_done"));
            else H.log(L"  " + metaErr);
        }
        if (videoMode && (kind == OutKind::Video || kind == OutKind::Gif || kind == OutKind::Webp) && (s.loopMode != "none" || s.loop > 0) && !H.cancelled()) {
            LoopCheck lc;  // la derniere image rejoint-elle la premiere aussi bien que deux images voisines se suivent ?
            if (checkLoopSeam(ffmpeg, out, lc)) {
                H.log(L"  " + TXF(lc.perfect ? "log.loop_check_ok" : "log.loop_check_bad", lc.seam, lc.typical));
                if (rd) {
                    rd->loopChecked = true;
                    rd->loopSeam = lc.seam;
                    rd->loopTypical = lc.typical;
                    rd->loopPerfect = lc.perfect;
                }
            }
        }
        if (still && !tiff && !sequence && fileExists(out)) pngTagSrgb(out);  // declare l'image en sRGB
        H.log(L"  " + TXF("log.ok_output", out.c_str()));
        if (H.output) H.output(out);
        if (rd) rd->output = out;
    }
    return result;
}

// ---------------------------------------------------------------------------
//  Variantes : le rendu de base, puis un rendu par variante (autre taille, cadence, codec...)
// ---------------------------------------------------------------------------
// « 720p:width=1280|height=720 » -> nom + reglages ; le nom entre dans le nom du fichier de sortie
static bool parseVariant(const std::string& spec, std::string& name, Forced& out) {
    const size_t colon = spec.find(':');
    name = trim(colon == std::string::npos ? spec : spec.substr(0, colon));
    for (auto& c : name)
        if (!isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
    if (name.empty()) return false;
    size_t pos = colon == std::string::npos ? spec.size() : colon + 1;
    while (pos < spec.size()) {
        size_t e = spec.find('|', pos);
        if (e == std::string::npos) e = spec.size();
        const std::string item = spec.substr(pos, e - pos);
        pos = e + 1;
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(item.substr(0, eq)));
        if (isSettingKey(key) && key != "variant") out.push_back({key, trim(item.substr(eq + 1))});
    }
    return true;
}

Result convertFileForced(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H, std::wstring& err,
                         const std::vector<std::pair<std::string, std::string>>& forced) {
    return convertOne(shaderPath, exeDir, base, H, err, &forced);
}

Result convertFile(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H,
                   std::wstring& err) {
    if (isMontageFile(shaderPath) && !H.snapshot && !H.previewFrame) return convertMontage(shaderPath, exeDir, base, H, err);
    if (H.snapshot || H.previewFrame || (H.sheetCols > 0 && H.sheetRows > 0)) return convertOne(shaderPath, exeDir, base, H, err, nullptr);
    std::string code;
    Settings probe = base;
    if (readFile(shaderPath, code)) {
        Hooks quiet;
        quiet.log = [](const std::wstring&) {};
        normalizeShaderText(code, quiet);
        parseLines(probe, code, true);
    }
    if (probe.variants.empty()) return convertOne(shaderPath, exeDir, base, H, err, nullptr);

    Result result = convertOne(shaderPath, exeDir, base, H, err, nullptr);  // rendu de base
    if (result == Result::Cancelled) return result;
    int index = 0;
    for (const auto& spec : probe.variants) {
        std::string name;
        Forced forced;
        index++;
        if (!parseVariant(spec, name, forced)) continue;
        bool hasName = false;
        for (const auto& kv : forced)
            if (kv.first == "name") hasName = true;
        if (!hasName) forced.push_back({"name", (probe.name.empty() ? std::string("{shader}") : U8(probe.name)) + "_" + name});
        H.log(L"  " + TXF("log.variant", W(name).c_str(), index, (int)probe.variants.size()));
        std::wstring e;
        const Result r = convertOne(shaderPath, exeDir, base, H, e, &forced);
        if (r == Result::Cancelled) return r;
        if (r == Result::Failed) {
            err = e;
            result = Result::Failed;
        }
    }
    return result;
}
