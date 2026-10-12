// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "looptools.h"
#include "i18n.h"
#include "player.h"
#include "process.h"
#include "formats.h"
#include "render.h"
#include "util.h"
#include "platform/platform.h"

namespace {

// Écart moyen absolu des composantes rouge, verte et bleue (0 à 255) entre deux tableaux de pixels de `bytesPerPixel` octets
double meanAbsDiff(const uint8_t* a, const uint8_t* b, size_t pixels, int bytesPerPixel) {
    if (pixels == 0) return 0;
    uint64_t sum = 0;
    for (size_t i = 0; i < pixels; i++) {
        const uint8_t* pa = a + i * bytesPerPixel;
        const uint8_t* pb = b + i * bytesPerPixel;
        sum += (uint64_t)std::abs((int)pa[0] - (int)pb[0]) + std::abs((int)pa[1] - (int)pb[1]) + std::abs((int)pa[2] - (int)pb[2]);
    }
    return (double)sum / (3.0 * pixels);
}

}  // namespace

bool checkLoopSeam(const std::wstring& ffmpeg, const std::wstring& file, LoopCheck& out) {
    constexpr int W = 64, H = 36;
    constexpr size_t fb = (size_t)W * H * 3;
    const std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -i \"" + file +
                             L"\" -an -vf scale=64:36:flags=area -pix_fmt rgb24 -f rawvideo -";
    auto proc = platform::Process::start(platform::splitCommand(cmd), false, true);
    if (!proc) return false;
    std::vector<uint8_t> first, prev, cur(fb);
    double sum = 0;
    long long frames = 0;
    for (;;) {
        size_t have = 0;
        while (have < fb) {
            const size_t got = proc->readOutput((char*)cur.data() + have, fb - have);
            if (got == 0) break;
            have += got;
        }
        if (have < fb) break;
        if (frames == 0) first = cur;
        else sum += meanAbsDiff(prev.data(), cur.data(), (size_t)W * H, 3);
        prev = cur;
        frames++;
    }
    proc->wait();
    if (frames < 2) return false;
    out.seam = meanAbsDiff(prev.data(), first.data(), (size_t)W * H, 3);
    out.typical = sum / (double)(frames - 1);
    out.perfect = out.seam <= 1.5 * out.typical + 1.0;
    return true;
}

bool findLoop(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, double maxPeriod, const Hooks& H, LoopFound& out,
              std::wstring& err) {
    out = LoopFound();
    ShaderProbe probe;
    if (!probe.open(shaderPath, exeDir, s, 64, err)) return false;
    if (probe.hasHistory()) {  // l'état des passes dépend du chemin parcouru : une image ne se compare pas à une autre instant plus loin
        err = TX("error.loop_find_history");
        return false;
    }
    const Settings& st = probe.settings();
    const double t0 = st.start;
    double maxP = maxPeriod > 0 ? maxPeriod : st.duration;
    maxP = std::max(0.2, std::min(300.0, maxP));
    const double dt = std::max(1.0 / 120.0, std::min(0.1, maxP / 2400.0));
    const int M = std::max(2, (int)std::floor(maxP / dt));
    const double offs[3] = {0.0, 0.73, 1.91};  // instants de référence : une coïncidence à un seul instant ne suffit pas
    const size_t px = (size_t)probe.width() * probe.height();
    std::vector<uint8_t> ref[3], f;
    for (int j = 0; j < 3; j++)
        if (!probe.frameAt(t0 + offs[j], ref[j])) {
            err = TX("error.framebuffer");
            return false;
        }
    auto cost = [&](double T) {
        double sum = 0;
        for (int j = 0; j < 3; j++) {
            probe.frameAt(t0 + offs[j] + T, f);
            sum += meanAbsDiff(ref[j].data(), f.data(), px, 4);
        }
        return sum / 3.0;
    };
    std::vector<double> d(M + 2, 0.0);
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 1; i <= M; i++) {
        if (H.cancelled()) {
            err = TX("error.loop_find_cancelled");
            return false;
        }
        d[i] = cost(i * dt);
        if (H.progress && (i % 32 == 0 || i == M)) {
            const double el = std::max(1e-6, std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
            H.progress(i, M, i / el, (M - i) * el / i);
        }
    }
    out.searched = M * dt;
    out.typical = d[1];
    std::vector<double> sorted(d.begin() + 1, d.begin() + M + 1);
    std::sort(sorted.begin(), sorted.end());
    const double med = sorted[sorted.size() / 2], maxd = sorted.back();
    if (maxd < 0.6) {  // l'image ne bouge pas (ou si peu que 8 bits ne le voient pas)
        out.found = out.isStatic = true;
        out.quality = 2;
        return true;
    }
    int best = -1;
    bool rose = false;
    for (int i = 1; i <= M && best < 0; i++) {
        if (d[i] > 0.5 * med) rose = true;
        if (!rose || i < 2) continue;
        bool localMin = true;
        for (int k = std::max(1, i - 2); k <= std::min(M, i + 2); k++)
            if (d[k] < d[i]) localMin = false;
        if (localMin && d[i] < 0.25 * med) best = i;
    }
    if (best < 0) return true;  // aucune période dans la fenêtre explorée
    // Affinage : recherche ternaire du minimum entre les deux pas voisins
    double lo = std::max(dt * 0.5, (best - 1) * dt), hi = std::min(maxP + dt, (best + 1) * dt);
    for (int it = 0; it < 16; it++) {
        const double a = lo + (hi - lo) / 3.0, b = hi - (hi - lo) / 3.0;
        if (cost(a) <= cost(b)) hi = b;
        else lo = a;
    }
    out.period = (lo + hi) / 2.0;
    out.distance = cost(out.period);
    out.found = true;
    const double tol = std::max(0.75, 0.02 * med);
    out.quality = out.distance <= tol ? 2 : out.distance < 0.15 * med ? 1 : 0;
    if (out.quality == 0) out.found = false;
    return true;
}

std::wstring describeLoop(const LoopFound& lf) {
    if (lf.isStatic) return TX("log.loop_static");
    if (lf.found) return TXF(lf.quality == 2 ? "log.loop_found" : "log.loop_found_approx", lf.period, lf.distance, lf.period, lf.period);
    return TXF("log.loop_not_found", lf.searched);
}

std::wstring describeHeat(const HeatStats& st) {
    return TXF("log.heat_stats", st.cols, st.rows, st.totalMs, st.minMs, st.maxMs, st.hotCol + 1, st.hotRow + 1, 100.0 * st.hotShare);
}

void heatColor(double x, uint8_t& r, uint8_t& g, uint8_t& b) {
    x = std::min(1.0, std::max(0.0, x));
    // bleu (0) -> cyan -> vert -> jaune -> rouge (1)
    const double stops[5][3] = {{0.0, 0.1, 0.9}, {0.0, 0.75, 0.85}, {0.1, 0.85, 0.2}, {0.95, 0.9, 0.1}, {0.95, 0.1, 0.1}};
    const double p = x * 4.0;
    const int i = std::min(3, (int)p);
    const double f = p - i;
    auto mix = [&](int c) { return (uint8_t)std::lround(255.0 * (stops[i][c] * (1 - f) + stops[i + 1][c] * f)); };
    r = mix(0);
    g = mix(1);
    b = mix(2);
}

bool renderHeatmap(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, int cols, int rows, double t, const Hooks& H,
                   HeatStats& stats, std::wstring& outFile, std::wstring& err) {
    stats = HeatStats();
    cols = std::max(1, std::min(64, cols));
    rows = std::max(1, std::min(64, rows));
    ShaderProbe probe;
    if (!probe.open(shaderPath, exeDir, s, 960, err)) return false;
    const Settings& st = probe.settings();
    if (t < 0) t = st.start + st.duration / 2;
    std::vector<double> ms;
    std::vector<uint8_t> img;
    // la mesure est repetee (trois passes, la meilleure retenue) pour ecarter les accidents du pilote
    std::vector<double> best;
    for (int pass = 0; pass < 3; pass++) {
        if (H.cancelled()) {
            err = TX("error.loop_find_cancelled");
            return false;
        }
        if (!probe.tileCosts(t, cols, rows, 6, ms, img)) {
            err = TX("error.framebuffer");
            return false;
        }
        if (best.empty()) best = ms;
        else
            for (size_t i = 0; i < ms.size(); i++) best[i] = std::min(best[i], ms[i]);
        if (H.progress) H.progress(pass + 1, 3, 0, 0);
    }
    stats.cols = cols;
    stats.rows = rows;
    stats.tileMs = best;
    stats.minMs = *std::min_element(best.begin(), best.end());
    stats.maxMs = *std::max_element(best.begin(), best.end());
    for (double v : best) stats.totalMs += v;
    stats.meanMs = stats.totalMs / (double)best.size();
    const size_t hot = (size_t)(std::max_element(best.begin(), best.end()) - best.begin());
    stats.hotCol = (int)(hot % cols);
    stats.hotRow = (int)(hot / cols);
    stats.hotShare = stats.totalMs > 0 ? stats.maxMs / stats.totalMs : 0.0;
    // image teintee : 55 % de la couleur du cout, 45 % de l'image ; premiere ligne en haut
    const int w = probe.width(), h = probe.height();
    std::vector<uint8_t> out((size_t)w * h * 3);
    const double span = std::max(1e-9, stats.maxMs - stats.minMs);
    for (int y = 0; y < h; y++) {
        const int row = std::min(rows - 1, y * rows / h);
        const uint8_t* src = &img[(size_t)(h - 1 - y) * w * 4];
        for (int x = 0; x < w; x++) {
            const int col = std::min(cols - 1, x * cols / w);
            uint8_t hr, hg, hb;
            heatColor((best[(size_t)row * cols + col] - stats.minMs) / span, hr, hg, hb);
            uint8_t* dst = &out[((size_t)y * w + x) * 3];
            dst[0] = (uint8_t)((src[x * 4] * 45 + hr * 55) / 100);
            dst[1] = (uint8_t)((src[x * 4 + 1] * 45 + hg * 55) / 100);
            dst[2] = (uint8_t)((src[x * 4 + 2] * 45 + hb * 55) / 100);
        }
    }
    if (!s.outdir.empty()) platform::makeDir(s.outdir);
    outFile = outputPath(shaderPath, s.outdir, L"_heat.png");
    const std::wstring cmd = L"\"" + findFfmpeg(exeDir) + L"\" -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -video_size " + std::to_wstring(w) + L"x" +
                             std::to_wstring(h) + L" -i - -frames:v 1 \"" + outFile + L"\"";
    auto proc = platform::Process::start(platform::splitCommand(cmd), true, true);
    if (!proc) {
        err = TX("error.ffmpeg_launch");
        return false;
    }
    const bool wrote = proc->writeStdin(out.data(), out.size());
    proc->closeStdin();
    char drain[1024];
    while (proc->readOutput(drain, sizeof(drain)) > 0) {}
    if (!wrote || proc->wait() != 0) {
        err = TX("error.ffmpeg_failed");
        return false;
    }
    if (H.output) H.output(outFile);
    return true;
}

double frameRichness(const uint8_t* p, int w, int h) {
    if (w < 2 || h < 2) return 0;
    const size_t n = (size_t)w * h;
    std::vector<uint32_t> hist(4096, 0);
    std::vector<float> Y(n);
    double sum = 0, sum2 = 0;
    size_t extreme = 0;
    for (size_t i = 0; i < n; i++) {
        const int r = p[i * 4], g = p[i * 4 + 1], b = p[i * 4 + 2];
        const float y = 0.299f * r + 0.587f * g + 0.114f * b;
        Y[i] = y;
        sum += y;
        sum2 += (double)y * y;
        hist[(size_t)((r >> 4) << 8 | (g >> 4) << 4 | (b >> 4))]++;
        if (y < 10.f || y > 245.f) extreme++;
    }
    const double mean = sum / n, sd = std::sqrt(std::max(0.0, sum2 / n - mean * mean));
    double grad = 0;
    for (int y = 0; y + 1 < h; y++)
        for (int x = 0; x + 1 < w; x++) {
            const size_t i = (size_t)y * w + x;
            grad += std::fabs(Y[i + 1] - Y[i]) + std::fabs(Y[i + w] - Y[i]);
        }
    grad /= 2.0 * (double)(w - 1) * (h - 1);
    double entropy = 0;
    for (uint32_t c : hist)
        if (c) {
            const double q = (double)c / n;
            entropy -= q * std::log2(q);
        }
    const double en = std::min(1.0, entropy / 8.0), contrast = std::min(1.0, sd / 60.0), detail = std::min(1.0, grad / 12.0);
    const double expo = 1.0 - std::min(1.0, 1.25 * (double)extreme / n);
    return (0.4 * en + 0.3 * detail + 0.3 * contrast) * (0.35 + 0.65 * expo);
}

bool renderSmartThumbnails(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, const Hooks& H, std::vector<std::wstring>& files,
                           std::wstring& err) {
    files.clear();
    std::vector<BestFrame> best;
    if (!findBestFrames(shaderPath, exeDir, s, 24, 3, H, best, err)) {
        if (err.empty()) err = TX("error.thumb_none");
        return false;
    }
    for (size_t i = 0; i < best.size(); i++) {
        Hooks H2 = H;
        H2.snapshot = true;
        H2.thumbTime = best[i].t;
        H2.thumbSuffix = i == 0 ? std::wstring() : std::to_wstring(i + 1);
        H2.progress = nullptr;
        H2.output = [&](const std::wstring& path) {
            files.push_back(path);
            if (H.output) H.output(path);
        };
        if (H.log) H.log(L"  " + TXF("log.thumb_best", (int)i + 1, best[i].t, 100.0 * best[i].score));
        if (convertFile(shaderPath, exeDir, s, H2, err) != Result::Ok) return false;
    }
    return true;
}

bool findBestFrames(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, int candidates, int top, const Hooks& H,
                    std::vector<BestFrame>& out, std::wstring& err) {
    out.clear();
    ShaderProbe probe;
    if (!probe.open(shaderPath, exeDir, s, 192, err)) return false;
    const Settings& st = probe.settings();
    const int N = std::max(6, std::min(200, candidates));
    std::vector<BestFrame> all;
    std::vector<uint8_t> f;
    for (int i = 0; i < N; i++) {
        if (H.cancelled()) {
            err = TX("error.loop_find_cancelled");
            return false;
        }
        BestFrame b;
        b.t = st.start + (i + 0.5) * st.duration / N;
        if (!probe.frameAt(b.t, f)) {
            err = TX("error.framebuffer");
            return false;
        }
        b.score = frameRichness(f.data(), probe.width(), probe.height());
        all.push_back(b);
        if (H.progress) H.progress(i + 1, N, 0, 0);
    }
    std::sort(all.begin(), all.end(), [](const BestFrame& a, const BestFrame& b) { return a.score > b.score; });
    const double gap = st.duration / (2.0 * std::max(1, top) + 4.0);  // les propositions ne sont pas des images voisines
    for (const BestFrame& b : all) {
        bool apart = true;
        for (const BestFrame& c : out)
            if (std::fabs(c.t - b.t) < gap) apart = false;
        if (apart) out.push_back(b);
        if ((int)out.size() >= top) break;
    }
    return !out.empty();
}
