// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "rhythm.h"
#include "audio.h"
#include "i18n.h"
#include "json.h"
#include "process.h"
#include "publish.h"
#include "util.h"
#include "platform/platform.h"
#include <cmath>
#include <complex>

namespace {

constexpr int kFftSize = 1024;
constexpr int kAlgoVersion = 1;       // change quand l'analyse change : les fiches anciennes sont refaites
constexpr double kBeatLag = 0.0;      // correction constante du temps d'une attaque (s), mesuree avec le son synthetique de l'auto-test

double percentile(std::vector<float> v, double p) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, (size_t)std::floor(p * (double)(v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + (long)k, v.end());
    return v[k];
}

// Valeurs de 0 a `ref` ramenees a 0..255 ; ref <= 0 : tout a 0
std::vector<uint8_t> quantize(const std::vector<float>& v, double ref) {
    std::vector<uint8_t> q(v.size(), 0);
    if (ref <= 1e-12) return q;
    for (size_t i = 0; i < v.size(); i++) q[i] = (uint8_t)std::lround(std::min(1.0, std::max(0.0, v[i] / ref)) * 255.0);
    return q;
}

std::vector<float> dequant(const std::vector<uint8_t>& q) {
    std::vector<float> v(q.size());
    for (size_t i = 0; i < q.size(); i++) v[i] = (float)q[i] / 255.0f;
    return v;
}

// Moyenne mobile centree de demi-largeur `r` colonnes
std::vector<float> boxSmooth(const std::vector<float>& v, int r) {
    std::vector<float> o(v.size(), 0.0f);
    const long long n = (long long)v.size();
    std::vector<double> cum((size_t)n + 1, 0.0);
    for (long long i = 0; i < n; i++) cum[(size_t)i + 1] = cum[(size_t)i] + v[(size_t)i];
    for (long long i = 0; i < n; i++) {
        const long long a = std::max(0LL, i - r), b = std::min(n - 1, i + r);
        o[(size_t)i] = (float)((cum[(size_t)b + 1] - cum[(size_t)a]) / (double)(b - a + 1));
    }
    return o;
}

double parabolicOffset(double y0, double y1, double y2) {
    const double d = y0 - 2 * y1 + y2;
    if (d >= -1e-12) return 0;
    return std::min(0.5, std::max(-0.5, 0.5 * (y0 - y2) / d));
}

std::string num(double v) {
    char b[48];
    snprintf(b, sizeof(b), "%.6f", v);
    return b;
}

}  // namespace

double nearestOf(const std::vector<double>& grid, double t) {
    if (grid.empty()) return -1;
    auto it = std::lower_bound(grid.begin(), grid.end(), t);
    if (it == grid.begin()) return *it;
    if (it == grid.end()) return grid.back();
    return std::fabs(*it - t) < std::fabs(*(it - 1) - t) ? *it : *(it - 1);
}

// --- Caracteristiques ---------------------------------------------------------------------------------------------------------------------------

void analyzeRhythmSamples(const std::vector<int16_t>& pcm, RhythmFeatures& out) {
    out = RhythmFeatures();
    const size_t n = pcm.size();
    out.duration = (double)n / kRhythmRate;
    if (n < (size_t)kFftSize * 2) return;
    const size_t frames = n / kRhythmHop + 1;
    std::vector<float> win(kFftSize);
    double winSq = 0;
    for (int i = 0; i < kFftSize; i++) {
        win[(size_t)i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * (float)i / (float)(kFftSize - 1));
        winSq += (double)win[(size_t)i] * win[(size_t)i];
    }
    auto sample = [&](long long i) -> float { return i < 0 || i >= (long long)n ? 0.0f : (float)pcm[(size_t)i] / 32768.0f; };
    std::vector<std::complex<float>> x((size_t)kFftSize);
    std::vector<float> prev(kFftSize / 2, 0.0f);
    std::vector<float> lvE(frames), lvB(frames), lvM(frames), lvT(frames), fxB(frames), fxM(frames), fxT(frames), pk(frames);
    for (size_t f = 0; f < frames; f++) {
        const long long start = (long long)f * kRhythmHop - kRhythmHop;
        double sumSq = 0;
        for (int i = 0; i < kFftSize; i++) {
            const float s = sample(start + i) * win[(size_t)i];
            x[(size_t)i] = s;
            sumSq += (double)s * s;
        }
        float peak = 0;
        for (long long i = (long long)f * kRhythmHop - kRhythmHop / 2; i < (long long)f * kRhythmHop + kRhythmHop / 2; i++) peak = std::max(peak, std::fabs(sample(i)));
        fftInPlace(x);
        double lev[3] = {0, 0, 0}, flux[3] = {0, 0, 0};
        int cnt[3] = {0, 0, 0};
        for (int k = 1; k < kFftSize / 2; k++) {
            const int band = k < 6 ? 0 : k < 47 ? 1 : k < 233 ? 2 : -1;  // 0-250 Hz, 250 Hz-2 kHz, 2-10 kHz (une raie = 43 Hz)
            const float m = std::abs(x[(size_t)k]) / (kFftSize * 0.25f);  // un sinus d'amplitude A donne A
            const float c = std::log1p(200.0f * m);
            if (band >= 0) {
                lev[band] += (double)m * m;
                cnt[band]++;
                if (f > 0) flux[band] += std::max(0.0f, c - prev[(size_t)k]);
            }
            prev[(size_t)k] = c;
        }
        lvE[f] = (float)std::sqrt(sumSq / winSq);
        lvB[f] = (float)std::sqrt(lev[0] / cnt[0]);
        lvM[f] = (float)std::sqrt(lev[1] / cnt[1]);
        lvT[f] = (float)std::sqrt(lev[2] / cnt[2]);
        fxB[f] = (float)flux[0];
        fxM[f] = (float)flux[1];
        fxT[f] = (float)flux[2];
        pk[f] = peak;
    }
    out.peak = quantize(pk, *std::max_element(pk.begin(), pk.end()));
    out.energy = quantize(lvE, percentile(lvE, 0.99));
    out.bass = quantize(lvB, percentile(lvB, 0.99));
    out.mid = quantize(lvM, percentile(lvM, 0.99));
    out.treble = quantize(lvT, percentile(lvT, 0.99));
    out.fluxBass = quantize(fxB, percentile(fxB, 0.99));
    out.fluxMid = quantize(fxM, percentile(fxM, 0.99));
    out.fluxTreble = quantize(fxT, percentile(fxT, 0.99));
}

// --- Grille -----------------------------------------------------------------------------------------------------------------------------------------

void deriveRhythmGrid(const RhythmFeatures& f, double forcedBpm, int beatsPerBar, const RhythmEdits& edits, RhythmGrid& g) {
    g = RhythmGrid();
    const long long N = (long long)f.frames();
    if (N == 0) return;
    beatsPerBar = std::min(12, std::max(2, beatsPerBar));
    const std::vector<float> fb = dequant(f.fluxBass), fm = dequant(f.fluxMid), ft = dequant(f.fluxTreble);
    g.bass = boxSmooth(dequant(f.bass), 1);
    g.mid = boxSmooth(dequant(f.mid), 1);
    g.treble = boxSmooth(dequant(f.treble), 1);
    g.energy = boxSmooth(dequant(f.energy), 10);

    // force des attaques : somme des trois bandes moins sa moyenne locale (une seconde), partie positive
    std::vector<float> o((size_t)N), mean;
    for (long long i = 0; i < N; i++) o[(size_t)i] = fb[(size_t)i] + fm[(size_t)i] + ft[(size_t)i];
    mean = boxSmooth(o, 43);
    std::vector<float> op((size_t)N);
    double sq = 0;
    for (long long i = 0; i < N; i++) {
        op[(size_t)i] = std::max(0.0f, o[(size_t)i] - mean[(size_t)i]);
        sq += (double)op[(size_t)i] * op[(size_t)i];
    }
    const double rms = std::sqrt(sq / (double)N);
    const double p98 = percentile(op, 0.98);
    if (rms < 1e-4 || N < 64) return;  // aucune attaque : ni tempo, ni temps forts
    std::vector<double> on((size_t)N);
    for (long long i = 0; i < N; i++) on[(size_t)i] = op[(size_t)i] / rms;

    // attaques isolees (maxima locaux de la force des attaques), temps affine par parabole
    const double ref = std::max(p98, 1e-6);
    for (long long i = 3; i + 3 < N; i++) {
        const float v = op[(size_t)i];
        if (v < 0.25 * ref) continue;
        bool peak = true;
        for (int d = -3; d <= 3 && peak; d++)
            if (d != 0 && op[(size_t)(i + d)] > v) peak = false;
        if (!peak) continue;
        const double t = ((double)i + parabolicOffset(op[(size_t)i - 1], v, op[(size_t)i + 1])) / kRhythmFrameRate + kBeatLag;
        if (!g.onsets.empty() && t - g.onsets.back() < 5.0 / kRhythmFrameRate) continue;
        g.onsets.push_back(t);
        g.onsetStrength.push_back((float)std::min(1.0, v / ref));
    }

    // tempo : autocorrelation de la force des attaques, entre 50 et 220 temps par minute, avec un a priori vers 120
    double periodFrames = 0;
    if (forcedBpm > 0) {
        periodFrames = kRhythmFrameRate * 60.0 / forcedBpm;
    } else {
        const long long lagMin = (long long)std::ceil(kRhythmFrameRate * 60.0 / 220.0), lagMax = (long long)std::floor(kRhythmFrameRate * 60.0 / 50.0);
        const long long top = std::min(N - 1, 4 * lagMax + 2);
        if (top <= lagMin + 2) return;
        std::vector<double> acf((size_t)top + 1, 0.0);
        for (long long lag = 1; lag <= top; lag++) {
            double s = 0;
            for (long long i = 0; i + lag < N; i++) s += on[(size_t)i] * on[(size_t)(i + lag)];
            acf[(size_t)lag] = s / (double)(N - lag);
        }
        auto at = [&](long long lag) { return lag <= top ? acf[(size_t)lag] : 0.0; };
        double bestScore = -1;
        long long bestLag = -1;
        for (long long lag = lagMin; lag <= std::min(lagMax, top); lag++) {
            const double bpm = 60.0 * kRhythmFrameRate / (double)lag;
            const double z = std::log2(bpm / 120.0) / 0.9;
            const double score = (at(lag) + 0.5 * at(2 * lag) + 0.25 * at(4 * lag)) * std::exp(-0.5 * z * z);
            if (score > bestScore) {
                bestScore = score;
                bestLag = lag;
            }
        }
        if (bestLag < 0 || bestScore <= 0) return;
        periodFrames = (double)bestLag;
        if (bestLag > lagMin && bestLag < std::min(lagMax, top)) periodFrames += parabolicOffset(at(bestLag - 1), at(bestLag), at(bestLag + 1));
    }
    if (periodFrames < 4 || (double)N < 4 * periodFrames) return;

    // temps forts : programmation dynamique (chaque temps suit le precedent a une periode pres, les attaques font le score)
    const double alpha = forcedBpm > 0 ? 400.0 : 100.0;
    std::vector<double> C((size_t)N);
    std::vector<long long> back((size_t)N, -1);
    const long long wLo = (long long)std::floor(periodFrames * 0.5), wHi = (long long)std::ceil(periodFrames * 2.0);
    for (long long i = 0; i < N; i++) {
        double best = -1e18;
        long long bj = -1;
        for (long long j = i - wHi; j <= i - wLo; j++) {
            if (j < 0) continue;
            const double r = std::log((double)(i - j) / periodFrames);
            const double v = C[(size_t)j] - alpha * r * r;
            if (v > best) {
                best = v;
                bj = j;
            }
        }
        if (bj >= 0) {
            C[(size_t)i] = on[(size_t)i] + best;
            back[(size_t)i] = bj;
        } else {
            C[(size_t)i] = on[(size_t)i];
        }
    }
    long long last = N - 1;
    {
        double best = -1e18;
        for (long long i = std::max(0LL, N - (long long)std::ceil(periodFrames)); i < N; i++)
            if (C[(size_t)i] > best) {
                best = C[(size_t)i];
                last = i;
            }
    }
    std::vector<long long> chain;
    for (long long i = last; i >= 0; i = back[(size_t)i]) {
        chain.push_back(i);
        if (back[(size_t)i] < 0) break;
    }
    std::reverse(chain.begin(), chain.end());
    if (chain.size() < 3) return;
    std::vector<double> beats;
    for (long long i : chain) {
        const double d = (i > 0 && i + 1 < N) ? parabolicOffset(op[(size_t)i - 1], op[(size_t)i], op[(size_t)i + 1]) : 0.0;
        beats.push_back(((double)i + d) / kRhythmFrameRate + kBeatLag);
    }
    // tempo moyen : regression lineaire des temps sur leur rang
    {
        const double m = (double)beats.size();
        double sk = 0, st = 0, skk = 0, skt = 0;
        for (size_t k = 0; k < beats.size(); k++) {
            sk += (double)k;
            st += beats[k];
            skk += (double)k * (double)k;
            skt += (double)k * beats[k];
        }
        const double den = m * skk - sk * sk;
        const double slope = den > 0 ? (m * skt - sk * st) / den : 60.0 / (periodFrames / kRhythmFrameRate);
        g.bpm = slope > 0 ? 60.0 / slope : 0.0;
    }
    // la grille se prolonge de part et d'autre jusqu'aux bords du fichier
    {
        const size_t m = beats.size();
        const size_t span = std::min<size_t>(4, m - 1);
        const double headP = (beats[span] - beats[0]) / (double)span, tailP = (beats[m - 1] - beats[m - 1 - span]) / (double)span;
        std::vector<double> head;
        for (double t = beats[0] - headP; t >= -1e-9 && headP > 0.05; t -= headP) head.push_back(t);
        std::reverse(head.begin(), head.end());
        beats.insert(beats.begin(), head.begin(), head.end());
        for (double t = beats.back() + tailP; t <= f.duration + 1e-9 && tailP > 0.05; t += tailP) beats.push_back(t);
    }
    // corrections manuelles
    for (double r : edits.removed) {
        size_t bi = 0;
        double bd = 1e18;
        for (size_t k = 0; k < beats.size(); k++)
            if (std::fabs(beats[k] - r) < bd) {
                bd = std::fabs(beats[k] - r);
                bi = k;
            }
        if (!beats.empty() && bd <= 0.02) beats.erase(beats.begin() + (long)bi);
    }
    for (double a : edits.added) {
        bool dup = false;
        for (double b : beats)
            if (std::fabs(b - a) < 0.03) dup = true;
        if (!dup) beats.push_back(a);
    }
    std::sort(beats.begin(), beats.end());
    g.beats = beats;
    if (beats.size() < 2) {
        g.bpm = 0;
        g.beats.clear();
        return;
    }

    // mesures : le premier temps est celui qui porte le plus de graves sur l'ensemble du morceau
    const size_t B = (size_t)beatsPerBar;
    size_t phase = 0;
    if (beats.size() >= 2 * B) {
        std::vector<double> score(B, 0.0);
        for (size_t k = 0; k < beats.size(); k++) {
            const long long fi = std::min(N - 1, std::max(0LL, (long long)std::llround(beats[k] * kRhythmFrameRate)));
            double s = 0;
            for (long long d = -1; d <= 1; d++) {
                const long long q = std::min(N - 1, std::max(0LL, fi + d));
                s += fb[(size_t)q] + 0.3 * (double)f.energy[(size_t)q] / 255.0;
            }
            score[k % B] += s;
        }
        for (size_t p = 1; p < B; p++)
            if (score[p] > score[phase] * 1.0000001) phase = p;
    }
    for (size_t k = phase; k < beats.size(); k += B) g.bars.push_back(beats[k]);

    // sections : changement du caractere (niveau et bandes) d'un groupe de mesures au suivant
    g.sections.push_back(0.0);
    const size_t nb = g.bars.size();
    if (nb >= 8) {
        struct V {
            double v[4];
        };
        std::vector<V> feat(nb);
        const std::vector<float>* src[4] = {&g.energy, &g.bass, &g.mid, &g.treble};
        for (size_t b = 0; b < nb; b++) {
            const double t0 = g.bars[b], t1 = b + 1 < nb ? g.bars[b + 1] : f.duration;
            const long long a = std::min(N - 1, (long long)std::floor(t0 * kRhythmFrameRate)), e = std::min(N, std::max(a + 1, (long long)std::ceil(t1 * kRhythmFrameRate)));
            for (int c = 0; c < 4; c++) {
                double s = 0;
                for (long long q = a; q < e; q++) s += (*src[c])[(size_t)q];
                feat[b].v[c] = s / (double)(e - a);
            }
        }
        const size_t W = 2;
        std::vector<double> nov(nb, 0.0);
        for (size_t k = W; k + W <= nb; k++) {
            double d2 = 0;
            for (int c = 0; c < 4; c++) {
                double l = 0, r = 0;
                for (size_t q = 0; q < W; q++) {
                    l += feat[k - 1 - q].v[c];
                    r += feat[k + q].v[c];
                }
                d2 += (r - l) * (r - l) / (double)(W * W);
            }
            nov[k] = std::sqrt(d2);
        }
        long long lastCut = -100;
        for (size_t k = W; k + W <= nb; k++) {
            if (nov[k] < 0.25 || (long long)k - lastCut < 4) continue;
            bool top = true;
            for (long long d = -3; d <= 3 && top; d++) {
                const long long q = (long long)k + d;
                if (d != 0 && q >= 0 && q < (long long)nb && nov[(size_t)q] > nov[k]) top = false;
            }
            if (!top) continue;
            g.sections.push_back(g.bars[k]);
            lastCut = (long long)k;
        }
    }
}

// --- Fiche <son>.beats.json ----------------------------------------------------------------------------------------------------------------------

namespace {

std::string fileKey(const std::wstring& path) {
    uint64_t size = 0;
    if (!platform::fileSize(path, size)) return "";
    FILE* fp = platform::openFile(path, "rb");
    if (!fp) return "";
    std::string buf;
    const size_t chunk = 1 << 20;
    buf.resize(std::min<uint64_t>(size, chunk));
    buf.resize(fread(&buf[0], 1, buf.size(), fp));
    if (size > 2 * chunk) {
        std::string tail(chunk, '\0');
        if (fseek(fp, -(long)chunk, SEEK_END) == 0) {
            tail.resize(fread(&tail[0], 1, tail.size(), fp));
            buf += tail;
        }
    }
    fclose(fp);
    return std::to_string(size) + "-" + fnvHex(buf);
}

void putArray(std::string& s, const char* name, const std::vector<double>& v, bool comma = true) {
    s += std::string("    \"") + name + "\": [";
    for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + num(v[i]);
    s += comma ? "],\n" : "]\n";
}

}  // namespace

bool Rhythm::save() {
    std::string s = "{\n  \"shadertomp4_beats\": " + std::to_string(kAlgoVersion) + ",\n  \"key\": \"" + key_ + "\",\n  \"duration\": " + num(feat_.duration) + ",\n";
    s += "  \"rate\": " + std::to_string(kRhythmRate) + ",\n  \"hop\": " + std::to_string(kRhythmHop) + ",\n";
    s += "  \"result\": {\n    \"bpm\": " + num(grid_.bpm) + ",\n";
    putArray(s, "beats", grid_.beats);
    putArray(s, "bars", grid_.bars);
    putArray(s, "sections", grid_.sections);
    putArray(s, "onsets", grid_.onsets, false);
    s += "  },\n  \"user\": {\n";
    putArray(s, "added", edits_.added);
    putArray(s, "removed", edits_.removed, false);
    s += "  },\n  \"features\": {\n";
    const struct {
        const char* name;
        const std::vector<uint8_t>* v;
    } cols[] = {{"peak", &feat_.peak},       {"energy", &feat_.energy},       {"bass", &feat_.bass},           {"mid", &feat_.mid},
                {"treble", &feat_.treble}, {"flux_bass", &feat_.fluxBass}, {"flux_mid", &feat_.fluxMid}, {"flux_treble", &feat_.fluxTreble}};
    for (size_t i = 0; i < 8; i++) {
        const std::string raw((const char*)cols[i].v->data(), cols[i].v->size());
        s += std::string("    \"") + cols[i].name + "\": \"" + base64Encode(raw) + (i + 1 < 8 ? "\",\n" : "\"\n");
    }
    s += "  }\n}\n";
    return writeFileAtomic(file_, s);
}

bool Rhythm::load(const std::wstring& ffmpeg, const std::wstring& audioPath, double forcedBpm, int beatsPerBar, std::wstring& err) {
    loaded_ = false;
    fromCache_ = false;
    feat_ = RhythmFeatures();
    edits_ = RhythmEdits();
    forcedBpm_ = forcedBpm > 0 ? std::min(400.0, std::max(20.0, forcedBpm)) : 0;
    beatsPerBar_ = std::min(12, std::max(2, beatsPerBar));
    if (!fileExists(audioPath)) {
        err = TXF("error.audio_missing", audioPath.c_str());
        return false;
    }
    file_ = audioPath + L".beats.json";
    key_ = fileKey(audioPath);
    std::string text;
    JsonValue j;
    std::string jerr;
    if (readFile(file_, text) && parseJson(text, j, jerr) && (int)j.num("shadertomp4_beats") == kAlgoVersion && j.str("key") == key_ && !key_.empty()) {
        const JsonValue* ft = j.get("features");
        RhythmFeatures f;
        bool ok = ft != nullptr;
        const struct {
            const char* name;
            std::vector<uint8_t>* v;
        } cols[] = {{"peak", &f.peak},       {"energy", &f.energy},       {"bass", &f.bass},           {"mid", &f.mid},
                    {"treble", &f.treble}, {"flux_bass", &f.fluxBass}, {"flux_mid", &f.fluxMid}, {"flux_treble", &f.fluxTreble}};
        for (size_t i = 0; ok && i < 8; i++) {
            std::string raw;
            if (!base64Decode(ft->str(cols[i].name), raw)) ok = false;
            cols[i].v->assign(raw.begin(), raw.end());
        }
        f.duration = j.num("duration");
        if (ok && f.frames() > 0 && f.peak.size() == f.frames() && f.mid.size() == f.frames() && f.fluxTreble.size() == f.frames() && f.bass.size() == f.frames() &&
            f.treble.size() == f.frames() && f.fluxBass.size() == f.frames() && f.fluxMid.size() == f.frames()) {
            feat_ = f;
            fromCache_ = true;
            if (const JsonValue* u = j.get("user")) {
                for (const char* nm : {"added", "removed"})
                    if (const JsonValue* a = u->get(nm))
                        for (const JsonValue& v : a->items)
                            if (v.kind == JsonValue::Number) (std::string(nm) == "added" ? edits_.added : edits_.removed).push_back(v.number);
            }
        }
    }
    if (!fromCache_) {
        AudioAnalyzer a;
        if (!a.load(ffmpeg, audioPath, err)) return false;
        analyzeRhythmSamples(a.samples(), feat_);
        if (feat_.frames() == 0) {
            err = TXF("error.rhythm_short", audioPath.c_str());
            return false;
        }
    }
    deriveRhythmGrid(feat_, forcedBpm_, beatsPerBar_, edits_, grid_);
    if (!fromCache_) save();  // si le dossier ne se laisse pas ecrire, l'analyse reste valable pour cette fois
    loaded_ = true;
    return true;
}

void Rhythm::retune(double forcedBpm, int beatsPerBar) {
    forcedBpm_ = forcedBpm > 0 ? std::min(400.0, std::max(20.0, forcedBpm)) : 0;
    beatsPerBar_ = std::min(12, std::max(2, beatsPerBar));
    if (loaded_) deriveRhythmGrid(feat_, forcedBpm_, beatsPerBar_, edits_, grid_);
}

bool Rhythm::clearEdits() {
    if (!loaded_ || edits_.empty()) return false;
    edits_ = RhythmEdits();
    deriveRhythmGrid(feat_, forcedBpm_, beatsPerBar_, edits_, grid_);
    save();
    return true;
}

bool Rhythm::toggleBeat(double t, double tolerance) {
    if (!loaded_) return false;
    double best = -1, bd = 1e18;
    for (double x : grid_.beats)
        if (std::fabs(x - t) < bd) {
            bd = std::fabs(x - t);
            best = x;
        }
    if (best >= 0 && bd <= tolerance) {
        auto it = std::find_if(edits_.added.begin(), edits_.added.end(), [&](double a) { return std::fabs(a - best) < 0.006; });
        if (it != edits_.added.end()) edits_.added.erase(it);
        else edits_.removed.push_back(best);
    } else {
        auto it = std::find_if(edits_.removed.begin(), edits_.removed.end(), [&](double r) { return std::fabs(r - t) <= tolerance; });
        if (it != edits_.removed.end()) {
            edits_.removed.erase(it);
        } else {
            const double o = nearestOf(grid_.onsets, t);
            edits_.added.push_back(o >= 0 && std::fabs(o - t) <= 0.05 ? o : std::max(0.0, t));
        }
    }
    deriveRhythmGrid(feat_, forcedBpm_, beatsPerBar_, edits_, grid_);
    save();
    return true;
}

std::string Rhythm::signature() const {
    std::string s = "|rhythm:" + key_ + ":" + std::to_string(kAlgoVersion) + ":" + num(forcedBpm_) + ":" + std::to_string(beatsPerBar_);
    for (double a : edits_.added) s += "+" + num(a);
    for (double r : edits_.removed) s += "-" + num(r);
    return s;
}

void Rhythm::waveform(double from, double to, int buckets, std::vector<float>& peaks) const {
    peaks.assign((size_t)std::max(0, buckets), 0.0f);
    if (!loaded_ || buckets <= 0 || to <= from) return;
    const long long N = (long long)feat_.peak.size();
    for (int b = 0; b < buckets; b++) {
        const long long a = (long long)std::floor((from + (to - from) * b / buckets) * kRhythmFrameRate);
        const long long e = (long long)std::ceil((from + (to - from) * (b + 1) / buckets) * kRhythmFrameRate);
        uint8_t m = 0;
        for (long long q = std::max(0LL, a); q <= std::min(N - 1, e); q++) m = std::max(m, feat_.peak[(size_t)q]);
        peaks[(size_t)b] = (float)m / 255.0f;
    }
}

RhythmValues Rhythm::at(double t) const {
    RhythmValues r;
    if (!loaded_) return r;
    const long long N = (long long)grid_.energy.size();
    if (N > 0) {
        const double fp = std::min((double)(N - 1), std::max(0.0, t * kRhythmFrameRate));
        const long long i0 = (long long)std::floor(fp), i1 = std::min(N - 1, i0 + 1);
        const float fr = (float)(fp - (double)i0);
        auto lerp = [&](const std::vector<float>& v) { return v[(size_t)i0] + (v[(size_t)i1] - v[(size_t)i0]) * fr; };
        r.bass = lerp(grid_.bass);
        r.mid = lerp(grid_.mid);
        r.treble = lerp(grid_.treble);
        r.energy = lerp(grid_.energy);
    }
    const std::vector<double>& b = grid_.beats;
    if (b.size() >= 2) {
        const long long k = (long long)(std::upper_bound(b.begin(), b.end(), t) - b.begin()) - 1;
        r.beatCount = (int)(k + 1);
        double interval, since;
        if (k < 0) {
            interval = b[1] - b[0];
            since = interval - (b[0] - t);
        } else if (k + 1 < (long long)b.size()) {
            interval = b[(size_t)k + 1] - b[(size_t)k];
            since = t - b[(size_t)k];
        } else {
            interval = b[(size_t)k] - b[(size_t)k - 1];
            since = t - b[(size_t)k];
        }
        r.beat = (float)std::min(0.999999, std::max(0.0, interval > 0 ? since / interval : 0.0));
        const long long lo = std::max(0LL, k - 4), hi = std::min((long long)b.size() - 1, std::max(k, 0LL) + 4);
        r.bpm = hi > lo ? (float)(60.0 * (double)(hi - lo) / (b[(size_t)hi] - b[(size_t)lo])) : (float)grid_.bpm;
    }
    if (!grid_.onsets.empty()) {
        long long k = (long long)(std::upper_bound(grid_.onsets.begin(), grid_.onsets.end(), t) - grid_.onsets.begin()) - 1;
        float v = 0;
        for (; k >= 0 && t - grid_.onsets[(size_t)k] < 0.8; k--) v = std::max(v, grid_.onsetStrength[(size_t)k] * (float)std::exp(-(t - grid_.onsets[(size_t)k]) / 0.12));
        r.onset = v;
    }
    return r;
}

// --- Marqueurs --------------------------------------------------------------------------------------------------------------------------------

std::vector<RhythmMarker> rhythmMarkers(const Rhythm& r, double audioStart, double videoSeconds, double speed, bool beats, bool sections) {
    std::vector<RhythmMarker> out;
    if (!r.loaded() || speed <= 0) return out;
    const RhythmGrid& g = r.grid();
    auto video = [&](double audioT) { return (audioT - audioStart) / speed; };
    if (sections) {
        size_t first = 0;
        for (size_t k = 0; k < g.sections.size(); k++)
            if (g.sections[k] <= audioStart + 1e-9) first = k;
        out.push_back({0.0, "section", TXF("rhythm.section", (int)first + 1)});
        for (size_t k = first + 1; k < g.sections.size(); k++) {
            const double v = video(g.sections[k]);
            if (v > 1e-6 && v < videoSeconds) out.push_back({v, "section", TXF("rhythm.section", (int)k + 1)});
        }
    }
    if (beats) {
        for (size_t k = 0; k < g.beats.size(); k++) {
            const double v = video(g.beats[k]);
            if (v >= -1e-9 && v < videoSeconds) out.push_back({std::max(0.0, v), "beat", TXF("rhythm.beat", (int)k + 1)});
        }
        for (size_t k = 0; k < g.bars.size(); k++) {
            const double v = video(g.bars[k]);
            if (v >= -1e-9 && v < videoSeconds) out.push_back({std::max(0.0, v), "bar", TXF("rhythm.bar", (int)k + 1)});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const RhythmMarker& a, const RhythmMarker& b) { return a.time < b.time; });
    return out;
}

static std::string ffmetaEscapeText(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '=' || c == ';' || c == '#' || c == '\\') o += '\\';
        if (c == '\n') {
            o += "\\\n";
            continue;
        }
        o += c;
    }
    return o;
}

std::string chaptersMetadata(const std::vector<RhythmMarker>& markers, double videoSeconds) {
    std::vector<const RhythmMarker*> sec;
    for (const auto& m : markers)
        if (m.kind == "section") sec.push_back(&m);
    if (sec.empty()) return "";
    std::string s = ";FFMETADATA1\n";
    for (size_t i = 0; i < sec.size(); i++) {
        const long long a = std::llround(sec[i]->time * 1000.0);
        long long b = i + 1 < sec.size() ? std::llround(sec[i + 1]->time * 1000.0) : std::llround(videoSeconds * 1000.0);
        if (b <= a) b = a + 1;
        s += "[CHAPTER]\nTIMEBASE=1/1000\nSTART=" + std::to_string(a) + "\nEND=" + std::to_string(b) + "\ntitle=" + ffmetaEscapeText(U8(sec[i]->label)) + "\n";
    }
    return s;
}

static std::string timecode(double t, double fps) {
    const long long rate = std::max(1LL, std::llround(fps));
    const long long total = std::llround(std::max(0.0, t) * (double)rate);
    char b[48];
    snprintf(b, sizeof(b), "%02lld:%02lld:%02lld:%02lld", total / rate / 3600, total / rate / 60 % 60, total / rate % 60, total % rate);
    return b;
}

bool writeMarkerFiles(const std::wstring& video, const std::vector<RhythmMarker>& markers, double fps, double videoSeconds) {
    const std::wstring stem = video.substr(0, video.find_last_of(L'.'));
    std::string txt, csv = "type,number,seconds,timecode,label\n";
    std::map<std::string, int> counter;
    for (size_t i = 0; i < markers.size(); i++) {
        const RhythmMarker& m = markers[i];
        double end = m.time;
        if (m.kind == "section") {
            end = videoSeconds;
            for (size_t j = i + 1; j < markers.size(); j++)
                if (markers[j].kind == "section") {
                    end = markers[j].time;
                    break;
                }
        }
        txt += num(m.time) + "\t" + num(end) + "\t" + U8(m.label) + "\n";
        csv += m.kind + "," + std::to_string(++counter[m.kind]) + "," + num(m.time) + "," + timecode(m.time, fps) + ",\"" + U8(m.label) + "\"\n";
    }
    return writeFileAtomic(stem + L".markers.txt", txt) && writeFileAtomic(stem + L".markers.csv", csv);
}

bool addChapters(const std::wstring& ffmpeg, const std::wstring& video, const std::vector<RhythmMarker>& markers, double videoSeconds, std::wstring& err) {
    const std::string meta = chaptersMetadata(markers, videoSeconds);
    if (meta.empty()) {
        err = TX("log.chapters_none");
        return false;
    }
    const size_t dot = video.find_last_of(L'.');
    const std::wstring metaFile = video.substr(0, dot) + L".chapters.ffmetadata", tmp = video.substr(0, dot) + L".chap" + video.substr(dot);
    if (!writeFileAtomic(metaFile, meta)) {
        err = TX("log.chapters_failed");
        return false;
    }
    const std::string e = lower(U8(video.substr(dot)));
    const std::wstring ext = e == ".mp4" || e == ".mov" ? L" -movflags +faststart+use_metadata_tags" : L"";
    const int rc = runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + video + L"\" -i \"" + metaFile +
                                  L"\" -map 0 -c copy -map_metadata 0 -map_chapters 1" + ext + L" \"" + tmp + L"\"",
                              nullptr);
    platform::removeFile(metaFile);
    if (rc != 0 || !platform::moveFile(tmp, video, true)) {
        platform::removeFile(tmp);
        err = TX("log.chapters_failed");
        return false;
    }
    return true;
}

std::vector<std::wstring> rhythmSummary(const Rhythm& r) {
    std::vector<std::wstring> l;
    const RhythmGrid& g = r.grid();
    l.push_back(TXF(r.fromCache() ? "rhythm.sum_cache" : "rhythm.sum_new", r.cacheFile().c_str()));
    if (g.beats.empty()) {
        l.push_back(TX("rhythm.sum_notempo"));
        return l;
    }
    l.push_back(TXF("rhythm.sum_tempo", g.bpm, (int)g.beats.size(), (int)g.bars.size(), r.beatsPerBar()));
    if (r.forcedBpm() > 0) l.push_back(TXF("rhythm.sum_forced", r.forcedBpm()));
    std::wstring secs;
    for (size_t i = 0; i < g.sections.size(); i++) secs += (i ? L", " : L"") + wfmt(L"%.1f s", g.sections[i]);
    l.push_back(TXF("rhythm.sum_sections", (int)g.sections.size(), secs.c_str()));
    l.push_back(TXF("rhythm.sum_onsets", (int)g.onsets.size()));
    if (!r.edits().empty()) l.push_back(TXF("rhythm.sum_edits", (int)r.edits().added.size(), (int)r.edits().removed.size()));
    return l;
}

void applyMarkerSetting(const std::string& mode, const Rhythm& r, const std::wstring& ffmpeg, const std::wstring& video, double audioStart, double seconds,
                        double speed, double fps, const std::function<void(const std::wstring&)>& log) {
    if (!r.loaded() || mode == "off") return;
    const bool chapters = mode == "chapters" || mode == "all", files = mode == "file" || mode == "all";
    if (r.grid().beats.empty()) log(TX("log.markers_notempo"));
    const std::vector<RhythmMarker> m = rhythmMarkers(r, audioStart, seconds, speed, files, true);
    if (chapters) {
        std::wstring e;
        if (addChapters(ffmpeg, video, m, seconds, e)) {
            int n = 0;
            for (const auto& k : m) n += k.kind == "section" ? 1 : 0;
            log(TXF("log.chapters_done", n));
        } else {
            log(e);
        }
    }
    if (files) {
        const std::wstring stem = video.substr(0, video.find_last_of(L'.'));
        if (writeMarkerFiles(video, m, fps, seconds)) log(TXF("log.markers_file", (stem + L".markers.csv").c_str(), (int)m.size()));
        else log(TX("log.markers_failed"));
    }
}
