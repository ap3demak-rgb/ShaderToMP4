// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "compare.h"
#include "formats.h"
#include "i18n.h"
#include "looptools.h"
#include "player.h"
#include "render.h"
#include "util.h"
#include "platform/platform.h"

namespace {

// Image RGBA bottom-first -> RGB top-first
std::vector<uint8_t> toRgbTop(const std::vector<uint8_t>& rgba, int w, int h) {
    std::vector<uint8_t> out((size_t)w * h * 3);
    for (int y = 0; y < h; y++) {
        const uint8_t* src = &rgba[(size_t)(h - 1 - y) * w * 4];
        uint8_t* dst = &out[(size_t)y * w * 3];
        for (int x = 0; x < w; x++) {
            dst[x * 3] = src[x * 4];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    return out;
}

// <dossier de sortie ou du shader>/golden/<nom> et son fichier de description
std::wstring goldenDir(const std::wstring& shaderPath, const Settings& s) {
    const std::wstring base = outputPath(shaderPath, s.outdir, L"");
    const size_t cut = base.find_last_of(L"\\/");
    if (cut == std::wstring::npos) return L"golden/" + base;
    return base.substr(0, cut) + L"/golden/" + base.substr(cut + 1);
}

// Decode un PNG en RGB 24 bits (ffmpeg)
bool decodeRgb(const std::wstring& ffmpeg, const std::wstring& png, int w, int h, std::vector<uint8_t>& out) {
    const std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -i \"" + png + L"\" -frames:v 1 -f rawvideo -pix_fmt rgb24 -";
    auto proc = platform::Process::start(platform::splitCommand(cmd), false, true);
    if (!proc) return false;
    out.assign((size_t)w * h * 3, 0);
    size_t have = 0;
    while (have < out.size()) {
        const size_t got = proc->readOutput((char*)out.data() + have, out.size() - have);
        if (got == 0) break;
        have += got;
    }
    char drain[512];
    while (proc->readOutput(drain, sizeof(drain)) > 0) {}
    proc->wait();
    return have == out.size();
}

std::wstring numText(double v, int decimals) { return i18n::fixed(v, decimals); }

}  // namespace

void diffRgba(const uint8_t* a, const uint8_t* b, int w, int h, double& meanAbs, int& maxAbs, double& mse) {
    double sum = 0, sq = 0;
    int mx = 0;
    const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++)
        for (int c = 0; c < 3; c++) {
            const int d = std::abs((int)a[i * 4 + c] - (int)b[i * 4 + c]);
            sum += d;
            sq += (double)d * d;
            mx = std::max(mx, d);
        }
    meanAbs = n ? sum / (3.0 * n) : 0;
    mse = n ? sq / (3.0 * n) : 0;
    maxAbs = mx;
}

bool compareShaders(const std::wstring& pathA, const std::wstring& pathB, const std::wstring& exeDir, const Settings& s, const CompareOptions& opt, const Hooks& H,
                    CompareResult& res, std::wstring& err) {
    res = CompareResult();
    Settings st = s;
    applyOverrides(st, shaderOverrides(pathA));
    if (st.fps <= 0) st.fps = 60;
    if (st.duration <= 0) st.duration = 10;
    ShaderProbe a, b;
    if (!a.open(pathA, exeDir, st, 640, err) || !b.open(pathB, exeDir, st, 640, err)) return false;
    if (a.width() != b.width() || a.height() != b.height()) {
        err = TX("error.compare_size");
        return false;
    }
    const int w = a.width(), h = a.height();
    const Settings& ps = a.settings();
    const int N = std::max(1, std::min(500, opt.samples));
    double sumMean = 0, sumMse = 0, maxAll = 0;
    int worst = -1;
    std::vector<uint8_t> fa, fb, worstA, worstB;
    bool exact = true;
    for (int i = 0; i < N; i++) {
        if (H.cancelled()) {
            err = TX("error.loop_find_cancelled");
            return false;
        }
        const double t = ps.start + (i + 0.5) * ps.duration / N;
        if (!a.frameAt(t, fa) || !b.frameAt(t, fb)) {
            err = TX("error.framebuffer");
            return false;
        }
        double mean = 0, mse = 0;
        int mx = 0;
        diffRgba(fa.data(), fb.data(), w, h, mean, mx, mse);
        sumMean += mean;
        sumMse += mse;
        if (mx > 0) exact = false;
        if (mx > maxAll || worst < 0) {
            maxAll = mx;
            worst = i;
            res.worstTime = t;
            worstA = fa;
            worstB = fb;
        }
        if (H.progress) H.progress(i + 1, N, 0, 0);
    }
    res.frames = N;
    res.meanAbs = sumMean / N;
    res.maxAbs = maxAll;
    const double mse = sumMse / N;
    res.psnr = mse > 0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 0.0;
    res.identical = exact;
    res.different = maxAll > opt.tolerance;
    const std::wstring ffmpeg = findFfmpeg(exeDir);
    if (!st.outdir.empty()) platform::makeDir(st.outdir);
    if (!exact && !worstA.empty()) {  // image de difference du pire instant, amplifiee 8 fois
        std::vector<uint8_t> amp((size_t)w * h * 3);
        const std::vector<uint8_t> ta = toRgbTop(worstA, w, h), tb = toRgbTop(worstB, w, h);
        for (size_t i = 0; i < amp.size(); i++) amp[i] = (uint8_t)std::min(255, 8 * std::abs((int)ta[i] - (int)tb[i]));
        const std::wstring f = outputPath(pathA, st.outdir, L"_diff.png");
        if (writePngRgb(ffmpeg, amp, w, h, f)) res.diffFile = f;
    }
    if (!opt.video.empty()) {  // video de comparaison : 30 images par seconde, instants repartis sur la duree
        const int nv = std::max(2, (int)std::lround(opt.videoSeconds * 30));
        const bool sbs = opt.video == "sbs";
        const int ow = sbs ? w * 2 : w;
        const std::wstring f = outputPath(pathA, st.outdir, L"_compare.mp4");
        const std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -video_size " + std::to_wstring(ow) + L"x" + std::to_wstring(h) +
                                 L" -framerate 30 -i - -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p \"" + f + L"\"";
        auto proc = platform::Process::start(platform::splitCommand(cmd), true, true);
        if (!proc) {
            err = TX("error.ffmpeg_launch");
            return false;
        }
        bool wrote = true;
        std::vector<uint8_t> frame((size_t)ow * h * 3);
        for (int i = 0; i < nv && wrote; i++) {
            if (H.cancelled()) break;
            const double t = ps.start + ps.duration * i / nv;
            a.frameAt(t, fa);
            b.frameAt(t, fb);
            const std::vector<uint8_t> ta = toRgbTop(fa, w, h), tb = toRgbTop(fb, w, h);
            if (sbs) {
                for (int y = 0; y < h; y++) {
                    memcpy(&frame[(size_t)y * ow * 3], &ta[(size_t)y * w * 3], (size_t)w * 3);
                    memcpy(&frame[((size_t)y * ow + w) * 3], &tb[(size_t)y * w * 3], (size_t)w * 3);
                }
            } else if (opt.video == "curtain") {  // le rideau balaie l'image de gauche a droite : avant a gauche, apres a droite
                const int cut = (int)((double)w * i / (nv - 1));
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        const std::vector<uint8_t>& src = x < cut ? ta : tb;
                        memcpy(&frame[((size_t)y * w + x) * 3], &src[((size_t)y * w + x) * 3], 3);
                        if (x == cut) frame[((size_t)y * w + x) * 3] = frame[((size_t)y * w + x) * 3 + 1] = frame[((size_t)y * w + x) * 3 + 2] = 255;  // trait du rideau
                    }
            } else {
                for (size_t k = 0; k < frame.size(); k++) frame[k] = (uint8_t)std::min(255, 8 * std::abs((int)ta[k] - (int)tb[k]));
            }
            wrote = proc->writeStdin(frame.data(), frame.size());
        }
        proc->closeStdin();
        char drain[512];
        while (proc->readOutput(drain, sizeof(drain)) > 0) {}
        if (!wrote || proc->wait() != 0) {
            err = TX("error.ffmpeg_failed");
            return false;
        }
        res.videoFile = f;
    }
    return true;
}

std::wstring describeCompare(const CompareResult& r) {
    if (r.identical) return TXF("log.compare.identical", r.frames);
    std::wstring s = TXF("log.compare.result", r.frames, numText(r.meanAbs, 3).c_str(), (int)r.maxAbs, r.psnr > 0 ? numText(r.psnr, 1).c_str() : L"-");
    s += L" " + TXF(r.different ? "log.compare.different" : "log.compare.within", r.worstTime);
    return s;
}

bool compareVersions(const std::wstring& shaderPath, const std::string& before, const std::string& after, const std::wstring& exeDir, const Settings& s, double t,
                     std::vector<uint8_t>& bgraBefore, std::vector<uint8_t>& bgraAfter, int& w, int& h, std::wstring& err) {
    const size_t cut = shaderPath.find_last_of(L'.');
    const std::wstring stem = cut == std::wstring::npos ? shaderPath : shaderPath.substr(0, cut);
    const std::wstring fileA = stem + L"~avant.glsl", fileB = stem + L"~apres.glsl";
    struct Cleanup {
        std::wstring a, b;
        ~Cleanup() {
            platform::removeFile(a);
            platform::removeFile(b);
        }
    } cleanup{fileA, fileB};
    if (!writeFileAtomic(fileA, before) || !writeFileAtomic(fileB, after)) {
        err = TXF("error.write_failed", fileA.c_str());
        return false;
    }
    ShaderProbe a, b;
    if (!a.open(fileA, exeDir, s, 640, err) || !b.open(fileB, exeDir, s, 640, err)) return false;
    if (a.width() != b.width() || a.height() != b.height()) {
        err = TX("error.compare_size");
        return false;
    }
    w = a.width();
    h = a.height();
    std::vector<uint8_t> fa, fb;
    if (!a.frameAt(t, fa) || !b.frameAt(t, fb)) {
        err = TX("error.framebuffer");
        return false;
    }
    auto bgra = [&](const std::vector<uint8_t>& px) {
        std::vector<uint8_t> o((size_t)w * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const uint8_t* p = &px[((size_t)(h - 1 - y) * w + x) * 4];
                uint8_t* d = &o[((size_t)y * w + x) * 4];
                d[0] = p[2];
                d[1] = p[1];
                d[2] = p[0];
                d[3] = 255;
            }
        return o;
    };
    bgraBefore = bgra(fa);
    bgraAfter = bgra(fb);
    return true;
}

bool goldenRecord(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, int count, const Hooks& H, GoldenResult& res, std::wstring& err) {
    res = GoldenResult();
    ShaderProbe probe;
    if (!probe.open(shaderPath, exeDir, s, 192, err)) return false;
    const Settings& ps = probe.settings();
    const int w = probe.width(), h = probe.height();
    res.dir = goldenDir(shaderPath, s);
    platform::makeDir(res.dir.substr(0, res.dir.find_last_of(L"\\/")));
    platform::makeDir(res.dir);
    std::string desc = "size=" + std::to_string(w) + "x" + std::to_string(h) + "\ncount=" + std::to_string(count) + "\n";
    std::vector<uint8_t> f;
    for (int i = 0; i < count; i++) {
        if (H.cancelled()) {
            err = TX("error.loop_find_cancelled");
            return false;
        }
        const double t = ps.start + (i + 0.5) * ps.duration / count;
        if (!probe.frameAt(t, f)) {
            err = TX("error.framebuffer");
            return false;
        }
        if (!writePngRgb(findFfmpeg(exeDir), toRgbTop(f, w, h), w, h, res.dir + L"/" + wfmt(L"frame_%d.png", i))) {
            err = TX("error.ffmpeg_failed");
            return false;
        }
        char b[64];
        snprintf(b, sizeof(b), "time_%d=%.6f\n", i, t);
        desc += b;
    }
    res.frames = count;
    return writeFileAtomic(res.dir + L"/golden.txt", desc);
}

bool goldenVerify(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, double tolerance, const Hooks& H, GoldenResult& res, std::wstring& err) {
    res = GoldenResult();
    res.dir = goldenDir(shaderPath, s);
    std::string desc;
    if (!readFile(res.dir + L"/golden.txt", desc)) {
        err = TXF("error.golden_missing", res.dir.c_str());
        return false;
    }
    int gw = 0, gh = 0, count = 0;
    std::vector<double> times;
    size_t pos = 0;
    while (pos < desc.size()) {
        size_t e = desc.find('\n', pos);
        if (e == std::string::npos) e = desc.size();
        const std::string line = desc.substr(pos, e - pos);
        pos = e + 1;
        int idx = 0;
        double tv = 0;
        if (sscanf(line.c_str(), "size=%dx%d", &gw, &gh) == 2) continue;
        if (sscanf(line.c_str(), "count=%d", &count) == 1) continue;
        if (sscanf(line.c_str(), "time_%d=%lf", &idx, &tv) == 2) {
            if ((int)times.size() <= idx) times.resize((size_t)idx + 1, 0.0);
            times[(size_t)idx] = tv;
        }
    }
    if (gw <= 0 || gh <= 0 || count <= 0 || (int)times.size() != count) {
        err = TXF("error.golden_missing", res.dir.c_str());
        return false;
    }
    ShaderProbe probe;
    if (!probe.open(shaderPath, exeDir, s, 192, err)) return false;
    if (probe.width() != gw || probe.height() != gh) {
        err = TXF("error.golden_size", gw, gh, probe.width(), probe.height());
        return false;
    }
    std::vector<uint8_t> f, ref;
    for (int i = 0; i < count; i++) {
        if (H.cancelled()) {
            err = TX("error.loop_find_cancelled");
            return false;
        }
        if (!probe.frameAt(times[(size_t)i], f) || !decodeRgb(findFfmpeg(exeDir), res.dir + L"/" + wfmt(L"frame_%d.png", i), gw, gh, ref)) {
            err = TXF("error.golden_missing", res.dir.c_str());
            return false;
        }
        const std::vector<uint8_t> cur = toRgbTop(f, gw, gh);
        double sum = 0;
        for (size_t k = 0; k < cur.size(); k++) sum += std::abs((int)cur[k] - (int)ref[k]);
        const double mean = sum / (double)cur.size();
        if (mean > res.worstMean || i == 0) {
            res.worstMean = mean;
            res.worstIndex = i;
            res.worstTime = times[(size_t)i];
        }
    }
    res.frames = count;
    res.ok = res.worstMean <= tolerance;
    return true;
}

std::wstring formatDuration(double seconds) {
    const long long s = std::max(0LL, (long long)std::llround(seconds));
    if (s >= 3600) return TXF("log.duration.hm", s / 3600, (s % 3600) / 60);
    if (s >= 60) return TXF("log.duration.ms", s / 60, s % 60);
    return TXF("log.duration.s", s);
}

void estimateFromSpeeds(const Settings& s, const double* fps, int n, Estimate& est) {
    est.fpsMin = est.fpsMax = fps[0];
    double sum = 0;
    for (int i = 0; i < n; i++) {
        est.fpsMin = std::min(est.fpsMin, fps[i]);
        est.fpsMax = std::max(est.fpsMax, fps[i]);
        sum += fps[i];
    }
    est.fpsMean = sum / n;
    const double variants = 1.0 + (double)s.variants.size();
    est.secLow = (double)est.frames / est.fpsMax * variants;
    est.secMid = (double)est.frames / est.fpsMean * variants;
    est.secHigh = (double)est.frames / est.fpsMin * variants;
    // Trop lent : plus de vingt fois la duree de la video, ou plus de deux secondes par image
    est.slow = est.secMid > 20.0 * est.videoSeconds * variants || 1.0 / est.fpsMean > 2.0;
    est.suggestions.clear();
    if (est.slow) {
        const double ss = std::max(1, s.ss), blur = std::max(1, s.blur);
        if (ss > 1)
            est.suggestions.push_back(TXF("log.estimate.sug_ss", (int)ss - 1, formatDuration(est.secMid * (ss - 1) * (ss - 1) / (ss * ss)).c_str(), formatDuration(est.secMid).c_str()));
        if (blur > 1) {
            const int nb = std::max(1, (int)blur / 2);
            est.suggestions.push_back(TXF("log.estimate.sug_blur", nb, formatDuration(est.secMid * nb / blur).c_str(), formatDuration(est.secMid).c_str()));
        }
        if (s.width > 1280) {
            const int nw = (int)(s.width * 0.707) & ~1, nh = (int)(s.height * 0.707) & ~1;
            est.suggestions.push_back(TXF("log.estimate.sug_size", nw, nh, formatDuration(est.secMid * 0.5).c_str(), formatDuration(est.secMid).c_str()));
        }
    }
}

bool estimateRender(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H, Estimate& est, std::wstring& err) {
    est = Estimate();
    Settings s = base;
    applyOverrides(s, shaderOverrides(shaderPath));
    if (s.fps <= 0) s.fps = 60;
    if (s.duration <= 0) s.duration = 10;
    if (s.loop > 0) s.duration = s.loop * std::max(1.0, std::floor(s.duration / s.loop + 0.5));
    est.frames = std::max(1LL, std::llround(s.duration * s.fps));
    const double outFps = s.interp > 0 ? s.interp : s.fps;
    est.videoSeconds = est.frames / s.fps;
    (void)outFps;
    const long long probeFrames = std::min<long long>(est.frames, 24);
    const std::wstring tmp = exeDir + L"estimate_tmp";
    platform::makeDir(tmp);
    double fps[3] = {0, 0, 0};
    int got = 0;
    const double instants[3] = {0.0, 0.5, 0.9};
    for (int k = 0; k < 3; k++) {
        if (H.cancelled()) {
            err = TX("error.loop_find_cancelled");
            platform::removeDir(tmp);
            return false;
        }
        const double start = s.start + std::max(0.0, std::min(instants[k] * s.duration, s.duration - (double)probeFrames / s.fps));
        std::vector<std::pair<std::string, std::string>> forced = {
            {"start", U8(wfmt(L"%.6f", start))},
            {"duration", U8(wfmt(L"%.6f", (double)probeFrames / s.fps))},
            {"chunk", "0"},    {"report", "off"},  {"outdir", U8(tmp)}, {"name", "est"}, {"exists", "overwrite"}, {"audio", ""},
            {"fadein", "0"},   {"fadeout", "0"},   {"title", ""},       {"credit", ""},  {"loopmode", "none"},   {"timemap", ""},
            {"speed", "1"},    {"loop", "0"},      {"endaction", "none"}, {"samples", "1"}};
        double speed = 0;
        std::wstring produced;
        Hooks Hm;
        Hm.log = [](const std::wstring&) {};
        Hm.cancel = H.cancel;
        Hm.progress = [&](long long, long long, double sp, double) { speed = sp; };
        Hm.output = [&](const std::wstring& p) { produced = p; };
        std::wstring e;
        const Result r = convertFileForced(shaderPath, exeDir, s, Hm, e, forced);
        if (!produced.empty()) platform::removeFile(produced);
        if (r == Result::Cancelled) {
            err = TX("error.loop_find_cancelled");
            platform::removeDir(tmp);
            return false;
        }
        if (r != Result::Ok) {
            err = e;
            clearDir(tmp);
            platform::removeDir(tmp);
            return false;
        }
        if (speed > 0) fps[got++] = speed;
        if (H.progress) H.progress(k + 1, 3, 0, 0);
    }
    clearDir(tmp);
    platform::removeDir(tmp);
    if (got == 0) {
        err = TX("error.estimate_none");
        return false;
    }
    estimateFromSpeeds(s, fps, got, est);
    return true;
}

std::wstring describeEstimate(const Estimate& e) {
    std::wstring s = TXF("log.estimate.file", formatDuration(e.secMid).c_str(), formatDuration(e.secLow).c_str(), formatDuration(e.secHigh).c_str(), (long long)e.frames,
                         numText(e.fpsMean, 1).c_str());
    if (e.slow) {
        s += L"\n" + TXF("log.estimate.slow", formatDuration(e.secMid).c_str(), formatDuration(e.videoSeconds).c_str());
        for (const std::wstring& l : e.suggestions) s += L"\n" + l;
    }
    return s;
}
