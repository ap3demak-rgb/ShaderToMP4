// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "quality.h"
#include "formats.h"
#include "i18n.h"
#include "looptools.h"
#include "player.h"
#include "util.h"
#include "platform/platform.h"

namespace {

double toLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

// Nombre d'inversions de luminance (creux a crete d'au moins `delta`) le long d'une ligne de n valeurs espacees de `step`
int zigzag(const float* v, int n, int step, float delta) {
    float lo = v[0], hi = v[0], ext = v[0];
    int dir = 0, trans = 0;
    for (int i = 1; i < n; i++) {
        const float x = v[(size_t)i * step];
        if (dir == 0) {
            if (x > lo + delta) {
                dir = 1;
                ext = x;
            } else if (x < hi - delta) {
                dir = -1;
                ext = x;
            } else {
                lo = std::min(lo, x);
                hi = std::max(hi, x);
            }
        } else if (dir == 1) {
            if (x > ext) ext = x;
            else if (x < ext - delta) {
                dir = -1;
                ext = x;
                trans++;
            }
        } else {
            if (x < ext) ext = x;
            else if (x > ext + delta) {
                dir = 1;
                ext = x;
                trans++;
            }
        }
    }
    return trans;
}

// Motif regulier de bandes : au moins cinq paires claires / sombres (10 inversions) sur au moins le quart des lignes (ou des colonnes)
bool hasStripes(const std::vector<float>& L, int w, int h) {
    int rows = 0, cols = 0;
    for (int k = 0; k < 8; k++) {
        const int y = std::min(h - 1, (int)((k + 0.5) * h / 8)), x = std::min(w - 1, (int)((k + 0.5) * w / 8));
        if (zigzag(&L[(size_t)y * w], w, 1, 0.2f) >= 10) rows++;
        if (zigzag(&L[(size_t)x], h, w, 0.2f) >= 10) cols++;
    }
    return rows >= 2 || cols >= 2;
}

float clean(float v) { return std::isfinite(v) ? std::min(1.0f, std::max(0.0f, v)) : 0.0f; }

// Une image reduite a ce qui sert a l'analyse
struct Px {
    std::vector<float> lin;     // luminance relative en lumiere lineaire (flashs)
    std::vector<float> disp;    // luminance affichee (ecarts entre images)
    std::vector<uint8_t> red;   // rouge sature : R / (R + G + B) >= 0,8
};

void toPx(const float* rgba, int n, Px& p) {
    p.lin.resize((size_t)n);
    p.disp.resize((size_t)n);
    p.red.resize((size_t)n);
    for (int i = 0; i < n; i++) {
        const float r = clean(rgba[i * 4]), g = clean(rgba[i * 4 + 1]), b = clean(rgba[i * 4 + 2]);
        p.disp[(size_t)i] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const double rl = toLinear(r), gl = toLinear(g), bl = toLinear(b);
        p.lin[(size_t)i] = (float)(0.2126 * rl + 0.7152 * gl + 0.0722 * bl);
        const double sum = rl + gl + bl;
        p.red[(size_t)i] = sum > 0.02 && rl / sum >= 0.8 ? 1 : 0;
    }
}

double meanAbs(const std::vector<float>& a, const std::vector<float>& b) {
    double s = 0;
    for (size_t i = 0; i < a.size(); i++) s += std::fabs(a[i] - b[i]);
    return a.empty() ? 0 : s / (double)a.size();
}

struct Tr {
    double t;
    int sign;
};

// Suite d'inversions : les passages de meme sens qui se suivent (fondu en plusieurs images) comptent pour un ; renvoie le pire nombre de
// flashs par seconde (une paire d'inversions = un flash) et le premier instant d'une seconde qui en a plus de trois
double worstFlashRate(const std::vector<Tr>& raw, double dt, double& firstTime) {
    std::vector<Tr> m;
    for (const Tr& t : raw) {
        if (!m.empty() && m.back().sign == t.sign && t.t - m.back().t <= 1.5 * dt) m.back().t = t.t;
        else m.push_back(t);
    }
    double worst = 0;
    firstTime = -1;
    for (size_t i = 0; i < m.size(); i++) {
        size_t j = i;
        while (j < m.size() && m[j].t < m[i].t + 1.0) j++;
        const double rate = (double)(j - i) / 2.0;
        worst = std::max(worst, rate);
        if (rate > 3.0 && firstTime < 0) firstTime = m[i].t;
    }
    return worst;
}

// Lignes du shader qui peuvent produire des NaN ou des infinis : division, normalize, pow, sqrt, log, acos / asin
void scanSuspects(const std::wstring& shaderPath, std::vector<std::wstring>& out) {
    std::string code;
    if (!readFile(shaderPath, code)) return;
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= code.size()) {
        size_t e = code.find('\n', pos);
        if (e == std::string::npos) e = code.size();
        std::string l = code.substr(pos, e - pos);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        pos = e + 1;
    }
    static const struct {
        const char* needle;
        const char* key;
    } kinds[] = {{"normalize(", "log.check.cause_normalize"}, {"pow(", "log.check.cause_pow"}, {"sqrt(", "log.check.cause_sqrt"}, {"inversesqrt(", "log.check.cause_sqrt"},
                 {"log(", "log.check.cause_log"}, {"log2(", "log.check.cause_log"}, {"acos(", "log.check.cause_acos"}, {"asin(", "log.check.cause_acos"}};
    for (size_t i = 0; i < lines.size() && out.size() < 12; i++) {
        std::string l = lines[i];
        const size_t c = l.find("//");
        if (c != std::string::npos) l = l.substr(0, c);
        std::wstring why;
        for (const auto& k : kinds)
            if (l.find(k.needle) != std::string::npos) {
                why = TX(k.key);
                break;
            }
        if (why.empty()) {  // division par autre chose qu'une constante : « / x » ou « / (»
            for (size_t p = 0; p + 1 < l.size() && why.empty(); p++) {
                if (l[p] != '/' || l[p + 1] == '=' || (p > 0 && l[p - 1] == '/')) continue;
                size_t q = p + 1;
                while (q < l.size() && l[q] == ' ') q++;
                if (q < l.size() && (isalpha((unsigned char)l[q]) || l[q] == '_' || l[q] == '(')) why = TX("log.check.cause_div");
            }
        }
        if (!why.empty()) out.push_back(TXF("log.check.cause_line", (int)i + 1, why.c_str()));
    }
}

}  // namespace

double roundToHalf(double v) {
    if (v == 0 || !std::isfinite(v)) return v;
    int e = 0;
    std::frexp(v, &e);                        // v = m * 2^e, m dans [0.5, 1)
    const double quantum = std::ldexp(1.0, e - 11);  // 11 chiffres significatifs (10 stockes)
    return std::nearbyint(v / quantum) * quantum;
}

FrameStats analyseFrame(const float* rgba, int w, int h) {
    FrameStats st;
    const int n = w * h;
    double sum = 0;
    bool anyLit = false;
    for (int i = 0; i < n; i++) {
        const float r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
        bool bad = false;
        for (float v : {r, g, b}) {
            if (std::isnan(v)) {
                st.nan++;
                bad = true;
                break;
            }
            if (std::isinf(v)) {
                st.inf++;
                bad = true;
                break;
            }
        }
        if (bad) continue;
        if (r > 1.0001f || g > 1.0001f || b > 1.0001f) st.clipped++;
        sum += 0.2126 * clean(r) + 0.7152 * clean(g) + 0.0722 * clean(b);
        if (std::max(r, std::max(g, b)) >= 0.004f) anyLit = true;
    }
    st.meanLuma = n > 0 ? sum / n : 0;
    st.allBlack = !anyLit && st.nan == 0 && st.inf == 0;
    return st;
}

int CheckReport::warnings() const {
    int n = 0;
    for (const CheckIssue& i : issues)
        if (i.severity >= 1) n++;
    return n;
}

bool parseCheckList(const std::string& listIn, CheckOptions& out) {
    out = CheckOptions();
    const std::string list = lower(trim(listIn));
    if (list.empty() || list == "all" || list == "on" || list == "1") return true;
    out.flash = out.defects = out.flicker = out.alias = out.portability = false;
    size_t pos = 0;
    while (pos <= list.size()) {
        size_t e = list.find(',', pos);
        if (e == std::string::npos) e = list.size();
        const std::string w = trim(list.substr(pos, e - pos));
        pos = e + 1;
        if (w.empty()) continue;
        if (w == "flash") out.flash = true;
        else if (w == "nan" || w == "defects") out.defects = true;
        else if (w == "flicker") out.flicker = true;
        else if (w == "alias" || w == "aliasing") out.alias = true;
        else if (w == "portability" || w == "precision") out.portability = true;
        else return false;
    }
    return true;
}

bool runChecks(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, const CheckOptions& opt, const Hooks& H, CheckReport& rep,
               std::wstring& err) {
    rep = CheckReport();
    rep.shader = shaderPath;
    Settings st = s;
    applyOverrides(st, shaderOverrides(shaderPath));  // les // cle=valeur du shader priment
    if (st.fps <= 0) st.fps = 60;
    if (st.duration <= 0) st.duration = 10;
    const double aspect = (double)std::max(16, st.width) / std::max(16, st.height);
    const int wA = 128, hA = std::max(16, (int)std::lround(wA / aspect) & ~1);
    Settings sp = st;
    sp.width = wA;
    sp.height = hA;
    ShaderProbe probe;
    if (!probe.open(shaderPath, exeDir, sp, 4096, err, true)) return false;
    const Settings& ps = probe.settings();
    rep.start = ps.start;
    rep.duration = ps.duration;
    rep.fps = ps.fps;
    const int n = probe.width() * probe.height();
    const double dt = std::max(1.0 / ps.fps, ps.duration / std::max(100, opt.maxSamples));
    const int N = std::max(2, (int)std::floor(ps.duration / dt) + 1);
    std::string code;
    readFile(shaderPath, code);

    std::vector<float> frame;
    Px cur, prev, prev2;
    std::vector<Tr> lum, red;
    long long stripeFrames = 0;
    double firstStripes = -1;
    long long nanFrames = 0, infFrames = 0, blackFrames = 0, whiteFrames = 0, burntFrames = 0, flickerFrames = 0, glitchFrames = 0, pairs = 0;
    double firstNan = -1, firstInf = -1, firstBlack = -1, firstWhite = -1, firstBurnt = -1, firstFlicker = -1, firstGlitch = -1;
    double worstNan = 0, worstInf = 0, worstBurnt = 0;
    std::vector<float> nanFrame;  // image du premier defaut
    double nanTime = -1;
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i++) {
        if (H.cancelled()) {
            err = TX("error.loop_find_cancelled");
            return false;
        }
        const double t = ps.start + i * dt;
        if (!probe.frameAtF(t, frame)) {
            err = TX("error.framebuffer");
            return false;
        }
        const FrameStats fs = analyseFrame(frame.data(), probe.width(), probe.height());
        if (opt.defects) {
            if (fs.nan > 0) {
                nanFrames++;
                worstNan = std::max(worstNan, 100.0 * fs.nan / n);
                if (firstNan < 0) firstNan = t;
                if (nanTime < 0) {
                    nanTime = t;
                    nanFrame = frame;
                }
            }
            if (fs.inf > 0) {
                infFrames++;
                worstInf = std::max(worstInf, 100.0 * fs.inf / n);
                if (firstInf < 0) firstInf = t;
                if (nanTime < 0) {
                    nanTime = t;
                    nanFrame = frame;
                }
            }
            if (fs.allBlack) {
                blackFrames++;
                if (firstBlack < 0) firstBlack = t;
            }
            if (fs.meanLuma > 0.996) {
                whiteFrames++;
                if (firstWhite < 0) firstWhite = t;
            }
            if (fs.clipped >= n / 4) {
                burntFrames++;
                worstBurnt = std::max(worstBurnt, 100.0 * fs.clipped / n);
                if (firstBurnt < 0) firstBurnt = t;
            }
        }
        toPx(frame.data(), n, cur);
        if (opt.flash && hasStripes(cur.disp, probe.width(), probe.height())) {
            stripeFrames++;
            if (firstStripes < 0) firstStripes = t;
        }
        if (i >= 1 && opt.flash) {
            long long changed = 0, redChanged = 0, becameRed = 0, lostRed = 0;
            double sum = 0;
            for (int p = 0; p < n; p++) {
                const double d = (double)cur.lin[(size_t)p] - prev.lin[(size_t)p];
                if (std::fabs(d) >= 0.1 && std::min(cur.lin[(size_t)p], prev.lin[(size_t)p]) < 0.8) {  // variation de 10 % au moins, l'etat sombre sous 0,80
                    changed++;
                    sum += d;
                }
                if (cur.red[(size_t)p] != prev.red[(size_t)p]) {
                    redChanged++;
                    if (cur.red[(size_t)p]) becameRed++;
                    else lostRed++;
                }
            }
            if (changed * 4 >= n) lum.push_back({t, sum > 0 ? 1 : -1});  // au moins le quart de l'ecran
            if (redChanged * 4 >= n) red.push_back({t, becameRed >= lostRed ? 1 : -1});
        }
        if (i >= 2 && opt.flicker) {
            const double d1 = meanAbs(cur.disp, prev.disp), dPrev = meanAbs(prev.disp, prev2.disp), d2 = meanAbs(cur.disp, prev2.disp);
            pairs++;
            if (d1 > 0.12 && d2 < 1.25 * d1) {  // l'image n'a pas de rapport avec la precedente : un mouvement continu s'eloigne de plus en plus
                flickerFrames++;
                if (firstFlicker < 0) firstFlicker = t;
            }
            if (dPrev > 0.1 && d1 > 0.1 && d2 < 0.4 * std::min(dPrev, d1)) {  // l'image precedente differe des deux voisines qui se ressemblent
                glitchFrames++;
                if (firstGlitch < 0) firstGlitch = t - dt;
            }
        }
        std::swap(prev2, prev);
        std::swap(prev, cur);
        if (H.progress && (i % 32 == 0 || i == N - 1)) {
            const double el = std::max(1e-6, std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
            H.progress(i + 1, N, (i + 1) / el, (N - i - 1) * el / (i + 1));
        }
    }
    rep.samples = N;
    auto add = [&](const char* kind, int severity, double first, double value, long long count, const std::wstring& text) {
        CheckIssue c;
        c.kind = kind;
        c.severity = severity;
        c.firstTime = first;
        c.value = value;
        c.count = count;
        c.text = text;
        rep.issues.push_back(c);
    };
    // Flashs : lignes directrices de type Harding (variation de luminance relative de 10 %, au quart de l'ecran, plus de trois flashs par seconde)
    if (opt.flash) {
        double first = -1;
        rep.maxFlashes = worstFlashRate(lum, dt, first);
        if (first >= 0) add("flash", 1, first, rep.maxFlashes, (long long)lum.size(), TXF("log.check.flash", rep.maxFlashes, first));
        rep.maxRedFlashes = worstFlashRate(red, dt, first);
        if (first >= 0) add("red_flash", 1, first, rep.maxRedFlashes, (long long)red.size(), TXF("log.check.red", rep.maxRedFlashes, first));
        if (stripeFrames * dt >= 1.0 || (stripeFrames > 0 && stripeFrames * 2 >= N)) add("pattern", 1, firstStripes, stripeFrames * dt, stripeFrames, TXF("log.check.pattern", stripeFrames * dt, firstStripes));
    }
    if (opt.defects) {
        if (nanFrames) add("nan", 1, firstNan, worstNan, nanFrames, TXF("log.check.nan", nanFrames, firstNan, worstNan));
        if (infFrames) add("inf", 1, firstInf, worstInf, infFrames, TXF("log.check.inf", infFrames, firstInf, worstInf));
        const long long many = std::max(3LL, (long long)(0.05 * N));
        if (blackFrames) add("black", blackFrames >= many ? 1 : 0, firstBlack, 100.0 * blackFrames / N, blackFrames, TXF("log.check.black", blackFrames, firstBlack));
        if (whiteFrames) add("white", whiteFrames >= many ? 1 : 0, firstWhite, 100.0 * whiteFrames / N, whiteFrames, TXF("log.check.white", whiteFrames, firstWhite));
        if (burntFrames) add("burnt", 1, firstBurnt, worstBurnt, burntFrames, TXF("log.check.burnt", worstBurnt, firstBurnt));
        if (nanTime >= 0) {  // carte des pixels invalides au premier defaut, et lignes suspectes
            scanSuspects(shaderPath, rep.suspectLines);
            std::vector<uint8_t> rgb((size_t)n * 3);
            const int w = probe.width(), h = probe.height();
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const float* p = &nanFrame[((size_t)(h - 1 - y) * w + x) * 4];
                    uint8_t* o = &rgb[((size_t)y * w + x) * 3];
                    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) {
                        o[0] = 255;
                        o[1] = 0;
                        o[2] = 255;
                    } else {
                        for (int c = 0; c < 3; c++) o[c] = (uint8_t)(clean(p[c]) * 120.0f);
                    }
                }
            if (!st.outdir.empty()) platform::makeDir(st.outdir);
            const std::wstring map = outputPath(shaderPath, st.outdir, L".check.png");
            if (writePngRgb(findFfmpeg(exeDir), rgb, w, h, map)) rep.mapFile = map;
            if (H.previewFrame) {
                std::vector<uint8_t> bgra((size_t)n * 4);
                for (int i = 0; i < n; i++) {
                    bgra[(size_t)i * 4] = rgb[(size_t)i * 3 + 2];
                    bgra[(size_t)i * 4 + 1] = rgb[(size_t)i * 3 + 1];
                    bgra[(size_t)i * 4 + 2] = rgb[(size_t)i * 3];
                    bgra[(size_t)i * 4 + 3] = 255;
                }
                H.previewFrame(w, h, std::move(bgra));
            }
        }
    }
    if (opt.flicker && pairs > 0) {
        if (flickerFrames * 5 >= pairs) add("flicker", 1, firstFlicker, 100.0 * flickerFrames / pairs, flickerFrames, TXF("log.check.flicker", 100.0 * flickerFrames / pairs, firstFlicker));
        if (glitchFrames > 0) add("glitch", 1, firstGlitch, (double)glitchFrames, glitchFrames, TXF("log.check.glitch", glitchFrames, firstGlitch));
    }
    // Aliasing : l'image reduite a 128 pixels contre le rendu a 3 fois la taille ramene par moyenne de 3 x 3 pixels, a cinq instants
    if (opt.alias) {
        Settings s3 = sp;
        s3.width = wA * 3;
        s3.height = hA * 3;
        ShaderProbe big;
        std::wstring e2;
        if (big.open(shaderPath, exeDir, s3, 4096, e2, true)) {
            double worst = 0;
            std::vector<float> a, b;
            for (int k = 0; k < 5; k++) {
                const double t = ps.start + (k + 0.5) * ps.duration / 5.0;
                if (!probe.frameAtF(t, a) || !big.frameAtF(t, b)) break;
                const int w = probe.width(), h = probe.height(), W3 = big.width();
                double sum = 0;
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++)
                        for (int c = 0; c < 3; c++) {
                            double acc = 0;
                            for (int sy = 0; sy < 3; sy++)
                                for (int sx = 0; sx < 3; sx++) acc += clean(b[((size_t)(y * 3 + sy) * W3 + (size_t)(x * 3 + sx)) * 4 + c]);
                            sum += std::fabs(acc / 9.0 - clean(a[((size_t)y * w + x) * 4 + c]));
                        }
                worst = std::max(worst, 255.0 * sum / ((double)w * h * 3));
            }
            if (worst > 4.0) add("aliasing", 1, -1, worst, 0, TXF("log.check.alias", worst));
        }
    }
    // Portabilite : le temps est un flottant de 32 bits ; la meme image bouge-t-elle autant aux grands temps ?
    if (opt.portability && !probe.hasHistory() && code.find("iTime") != std::string::npos) {
        auto motion = [&](double base) {  // mouvement moyen entre images voisines, sur six phases
            double sum = 0;
            int cnt = 0;
            std::vector<float> a, b;
            for (int g = 0; g < 6; g++)
                for (int k = 0; k < 4; k++) {
                    const double t0 = base + g * ps.duration / 6.0 + k / ps.fps;
                    if (!probe.frameAtF(t0, a) || !probe.frameAtF(t0 + 1.0 / ps.fps, b)) return -1.0;
                    double d = 0;
                    for (int p = 0; p < n * 4; p += 4) d += std::fabs(clean(a[(size_t)p]) - clean(b[(size_t)p])) + std::fabs(clean(a[(size_t)p + 1]) - clean(b[(size_t)p + 1])) + std::fabs(clean(a[(size_t)p + 2]) - clean(b[(size_t)p + 2]));
                    sum += d / (3.0 * n);
                    cnt++;
                }
            return cnt ? sum / cnt : -1.0;
        };
        const double m0 = motion(ps.start);
        if (m0 > 0.002)
            for (double T : {10000.0, 100000.0}) {
                const double m = motion(T);
                if (m >= 0 && m < 0.4 * m0) {
                    add("time_large", 1, -1, 100.0 * m / m0, 0, TXF("log.check.time_large", T, 100.0 * m / m0));
                    break;
                }
            }
        const double tHalf = 512.0 / ps.fps;  // la demi-precision (10 bits de mantisse) pas plus fin que la moitie d'une image
        if (tHalf < ps.start + ps.duration) {
            add("mediump", 0, tHalf, tHalf, 0, TXF("log.check.mediump", tHalf, ps.fps));
            // Simulation : iTime et iResolution arrondis en demi-precision, l'image est comparee a l'exacte, puis a l'image suivante (le pas normal)
            const double lo = std::max(ps.start, tHalf), hi = ps.start + ps.duration;
            double dq = 0, ds = 0, worstErr = 0, worstT = lo;
            std::vector<float> a, b, c;
            auto rgbDiff = [&](const std::vector<float>& x, const std::vector<float>& y) {
                double d = 0;
                for (int p = 0; p < n * 4; p += 4)
                    for (int k = 0; k < 3; k++) d += std::fabs(clean(x[(size_t)p + k]) - clean(y[(size_t)p + k]));
                return d / (3.0 * n);
            };
            int cnt = 0;
            for (int k = 0; k < 5; k++) {
                const double t = lo + (k + 0.5) * (hi - lo) / 5.0;
                probe.setHalfInputs(false);
                if (!probe.frameAtF(t, a) || !probe.frameAtF(t + 1.0 / ps.fps, c)) break;
                probe.setHalfInputs(true);
                const bool got = probe.frameAtF(t, b);
                probe.setHalfInputs(false);
                if (!got) break;
                dq += rgbDiff(a, b);
                ds += rgbDiff(a, c);
                cnt++;
                if (std::fabs(roundToHalf(t) - t) > worstErr) {
                    worstErr = std::fabs(roundToHalf(t) - t);
                    worstT = t;
                }
            }
            if (cnt > 0 && ds / cnt > 0.002 && dq > ds) add("mediump_sim", 1, worstT, 255.0 * dq / cnt, 0, TXF("log.check.mediump_sim", worstT, worstErr, 255.0 * dq / cnt, 255.0 * ds / cnt));
        }
    }
    std::stable_sort(rep.issues.begin(), rep.issues.end(), [](const CheckIssue& a, const CheckIssue& b) { return a.severity > b.severity; });
    return true;
}

namespace {

std::string jstr(const std::string& s) {
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

std::string jnum(double v) {
    char b[48];
    snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

}  // namespace

std::string checkReportJson(const CheckReport& r) {
    std::string o = "{\n";
    auto field = [&](const std::string& k, const std::string& v) { o += "  \"" + k + "\": " + v + ",\n"; };
    field("shader", jstr(U8(r.shader)));
    field("start", jnum(r.start));
    field("duration", jnum(r.duration));
    field("fps", jnum(r.fps));
    field("samples", std::to_string(r.samples));
    field("max_flashes_per_second", jnum(r.maxFlashes));
    field("max_red_flashes_per_second", jnum(r.maxRedFlashes));
    field("warnings", std::to_string(r.warnings()));
    o += "  \"issues\": [";
    for (size_t i = 0; i < r.issues.size(); i++) {
        const CheckIssue& c = r.issues[i];
        o += std::string(i ? ",\n    " : "\n    ") + "{\"kind\": " + jstr(c.kind) + ", \"severity\": " + std::to_string(c.severity) + ", \"first_time\": " +
             (c.firstTime >= 0 ? jnum(c.firstTime) : "null") + ", \"value\": " + jnum(c.value) + ", \"frames\": " + std::to_string(c.count) + ", \"text\": " + jstr(U8(c.text)) + "}";
    }
    o += r.issues.empty() ? "],\n" : "\n  ],\n";
    o += "  \"suspect_lines\": [";
    for (size_t i = 0; i < r.suspectLines.size(); i++) o += (i ? ", " : "") + jstr(U8(r.suspectLines[i]));
    o += "],\n  \"map\": " + (r.mapFile.empty() ? std::string("null") : jstr(U8(r.mapFile))) + "\n}\n";
    return o;
}

std::string checkReportText(const CheckReport& r) {
    std::wstring o = TX("log.check.title") + L"\n" + TX("log.check.shader") + L": " + r.shader + L"\n" + TXF("log.check.analysed", r.samples, r.duration) + L"\n";
    if (r.issues.empty()) o += TX("log.check.none") + L"\n";
    for (const CheckIssue& c : r.issues) o += (c.severity >= 1 ? L"! " : L"- ") + c.text + L"\n";
    if (!r.suspectLines.empty()) {
        o += TX("log.check.suspects_title") + L"\n";
        for (const std::wstring& l : r.suspectLines) o += l + L"\n";
    }
    if (!r.mapFile.empty()) o += TX("log.check.map") + L": " + r.mapFile + L"\n";
    o += TXF("log.check.summary", r.samples, r.duration, r.warnings(), (int)r.issues.size() - r.warnings()) + L"\n";
    return U8(o);
}

bool writeCheckReport(const CheckReport& rep, const std::wstring& stem) {
    bool ok = true;
    for (int k = 0; k < 2; k++) {
        const std::string text = k == 0 ? checkReportJson(rep) : checkReportText(rep);
        FILE* f = platform::openFile(stem + (k == 0 ? L".check.json" : L".check.txt"), "wb");
        if (!f) {
            ok = false;
            continue;
        }
        ok = fwrite(text.data(), 1, text.size(), f) == text.size() && ok;
        fclose(f);
    }
    return ok;
}
