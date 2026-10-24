// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "scopes.h"

namespace {

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void put(std::vector<uint8_t>& img, int w, int x, int y, int r, int g, int b) {
    uint8_t* p = &img[((size_t)y * w + x) * 4];
    p[0] = (uint8_t)clampi(b, 0, 255);
    p[1] = (uint8_t)clampi(g, 0, 255);
    p[2] = (uint8_t)clampi(r, 0, 255);
    p[3] = 255;
}

// Ajoute de la lumiere a un pixel (les points se cumulent sans depasser 255)
void add(std::vector<uint8_t>& img, int w, int x, int y, int r, int g, int b) {
    uint8_t* p = &img[((size_t)y * w + x) * 4];
    p[0] = (uint8_t)std::min(255, p[0] + b);
    p[1] = (uint8_t)std::min(255, p[1] + g);
    p[2] = (uint8_t)std::min(255, p[2] + r);
}

void fillBackground(std::vector<uint8_t>& img, int w, int h) {
    img.assign((size_t)w * h * 4, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) put(img, w, x, y, 14, 16, 22);
}

uint32_t maxOf(const uint32_t* v, size_t n) {
    uint32_t m = 0;
    for (size_t i = 0; i < n; i++) m = std::max(m, v[i]);
    return m;
}

// Intensite 0..255 d'un compte, echelle racine
int level(uint32_t count, uint32_t peak) { return peak ? (int)(255.0 * std::sqrt((double)count / (double)peak) + 0.5) : 0; }

}  // namespace

int scopeLuma(int r, int g, int b) { return clampi((int)std::lround(0.2126 * r + 0.7152 * g + 0.0722 * b), 0, 255); }

void scopeChroma(int r, int g, int b, int& cb, int& cr) {
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    cb = clampi((int)std::lround(128.0 + (b - y) / 1.8556 * (255.0 / 255.0)), 0, 255);
    cr = clampi((int)std::lround(128.0 + (r - y) / 1.5748 * (255.0 / 255.0)), 0, 255);
}

void computeScopes(const uint8_t* bgra, int w, int h, Scopes& out) {
    out = Scopes();
    out.width = std::max(0, w);
    out.height = std::max(0, h);
    out.wave.assign((size_t)kScopeWaveCols * 256, 0);
    out.vec.assign(256 * 256, 0);
    if (!bgra || w <= 0 || h <= 0) return;
    for (int y = 0; y < h; y++) {
        const uint8_t* row = bgra + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            const int b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2];
            const int l = scopeLuma(r, g, b);
            out.hist[0][r]++;
            out.hist[1][g]++;
            out.hist[2][b]++;
            out.hist[3][l]++;
            const int col = (int)((int64_t)x * kScopeWaveCols / w);
            out.wave[(size_t)l * kScopeWaveCols + (size_t)col]++;
            int cb, cr;
            scopeChroma(r, g, b, cb, cr);
            out.vec[(size_t)cr * 256 + (size_t)cb]++;
        }
    }
    out.pixels = (uint64_t)w * h;
}

void renderHistogram(const Scopes& s, std::vector<uint8_t>& bgra, int& w, int& h) {
    w = 256;
    h = 128;
    fillBackground(bgra, w, h);
    uint32_t peak = 0;
    for (int c = 0; c < 4; c++) peak = std::max(peak, maxOf(s.hist[c], 256));
    for (int x = 0; x < 256; x++) {
        for (int c = 0; c < 4; c++) {
            const int top = clampi((int)std::lround((double)h * std::sqrt(peak ? (double)s.hist[c][x] / peak : 0.0)), 0, h);
            for (int k = 0; k < top; k++) {
                const int y = h - 1 - k;
                if (c == 0) add(bgra, w, x, y, 150, 0, 0);
                else if (c == 1) add(bgra, w, x, y, 0, 150, 0);
                else if (c == 2) add(bgra, w, x, y, 0, 0, 150);
                else add(bgra, w, x, y, 30, 30, 30);
            }
        }
    }
}

void renderWaveform(const Scopes& s, std::vector<uint8_t>& bgra, int& w, int& h) {
    w = kScopeWaveCols;
    h = 256;
    fillBackground(bgra, w, h);
    for (int g = 0; g <= 4; g++)  // repères a 0, 25, 50, 75 et 100 %
        for (int x = 0; x < w; x += 2) put(bgra, w, x, 255 - g * 255 / 4, 40, 44, 56);
    const uint32_t peak = maxOf(s.wave.data(), s.wave.size());
    for (int l = 0; l < 256; l++)
        for (int x = 0; x < w; x++) {
            const int v = level(s.wave[(size_t)l * kScopeWaveCols + (size_t)x], peak);
            if (v > 0) add(bgra, w, x, 255 - l, v * 3 / 10 + (v > 0 ? 40 : 0), v, v * 3 / 10 + (v > 0 ? 40 : 0));
        }
}

void renderVectorscope(const Scopes& s, std::vector<uint8_t>& bgra, int& w, int& h) {
    w = 256;
    h = 256;
    fillBackground(bgra, w, h);
    for (int a = 0; a < 720; a++) {  // cercle de saturation 75 %
        const double t = a * 3.14159265358979 / 360.0;
        const int x = (int)std::lround(128 + 96 * std::cos(t)), y = (int)std::lround(128 - 96 * std::sin(t));
        if (x >= 0 && x < w && y >= 0 && y < h) put(bgra, w, x, y, 44, 48, 62);
    }
    for (int i = 0; i < 256; i++) {  // axes
        put(bgra, w, i, 128, 36, 40, 52);
        put(bgra, w, 128, i, 36, 40, 52);
    }
    static const int kMarks[6][3] = {{255, 0, 0}, {255, 255, 0}, {0, 255, 0}, {0, 255, 255}, {0, 0, 255}, {255, 0, 255}};  // R J V C B M
    for (const auto& m : kMarks) {  // petits carres aux positions des couleurs primaires et secondaires
        int cb, cr;
        scopeChroma(m[0], m[1], m[2], cb, cr);
        const int x = clampi(cb, 3, 252), y = clampi(255 - cr, 3, 252);
        for (int d = -3; d <= 3; d++) {
            put(bgra, w, x + d, y - 3, m[0] / 2 + 40, m[1] / 2 + 40, m[2] / 2 + 40);
            put(bgra, w, x + d, y + 3, m[0] / 2 + 40, m[1] / 2 + 40, m[2] / 2 + 40);
            put(bgra, w, x - 3, y + d, m[0] / 2 + 40, m[1] / 2 + 40, m[2] / 2 + 40);
            put(bgra, w, x + 3, y + d, m[0] / 2 + 40, m[1] / 2 + 40, m[2] / 2 + 40);
        }
    }
    const uint32_t peak = maxOf(s.vec.data(), s.vec.size());
    for (int cr = 0; cr < 256; cr++)
        for (int cb = 0; cb < 256; cb++) {
            const int v = level(s.vec[(size_t)cr * 256 + (size_t)cb], peak);
            if (v > 0) add(bgra, w, cb, 255 - cr, v / 2 + 60, v, v / 2 + 60);
        }
}
