// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "aov.h"
#include "i18n.h"
#include "util.h"

namespace {

int componentOf(char c) {
    switch (c) {
        case 'r': case 'x': return 0;
        case 'g': case 'y': return 1;
        case 'b': case 'z': return 2;
        case 'a': case 'w': return 3;
    }
    return -1;
}

// Noms de canaux : profondeur Z ; vecteurs X Y Z ; couleurs R G B A ; un seul canal inconnu : R
std::vector<std::string> channelNames(const std::string& name, size_t n) {
    const std::string base = name + ".";
    if (n == 1) return {base + (name == "depth" ? "Z" : "R")};
    const bool vec = name == "normal" || name == "position" || name == "motion" || name == "vector" || name == "uv" || name == "point";
    static const char* const xyz[4] = {"X", "Y", "Z", "W"};
    static const char* const rgb[4] = {"R", "G", "B", "A"};
    std::vector<std::string> v;
    for (size_t i = 0; i < n; i++) v.push_back(base + (vec ? xyz[i] : rgb[i]));
    return v;
}

}  // namespace

bool parseAovs(const Settings& s, std::vector<AovSpec>& out, std::wstring& err) {
    out.clear();
    for (const std::string& raw : s.aovSpecs) {
        const std::string spec = trim(raw);
        const size_t eq = spec.find('=');
        AovSpec a;
        if (eq == std::string::npos) {
            const std::string n = lower(spec);
            if (n == "alpha") {
                a.kind = AovSpec::Kind::Alpha;
                a.name = "alpha";
                a.exrNames = {"A"};
            } else if (n == "motion") {
                a.kind = AovSpec::Kind::MotionEstimate;
                a.name = "motion";
                a.exrNames = {"motion.X", "motion.Y"};
            } else {
                err = TXF("error.aov_invalid", W(spec).c_str());
                return false;
            }
        } else {
            a.name = trim(spec.substr(0, eq));
            std::string src = lower(trim(spec.substr(eq + 1)));
            bool ok = !a.name.empty() && a.name.size() <= 24;
            for (char c : a.name)
                if (!isalnum((unsigned char)c) && c != '_') ok = false;
            std::string swz = "rgb";
            const size_t dot = src.find('.');
            if (dot != std::string::npos) {
                swz = src.substr(dot + 1);
                src = src.substr(0, dot);
            }
            if (src.size() == 7 && src.compare(0, 6, "buffer") == 0 && src[6] >= 'a' && src[6] <= 'd') a.buffer = src[6] - 'a';
            else ok = false;
            if (swz.empty() || swz.size() > 4) ok = false;
            for (char c : swz) {
                const int k = componentOf(c);
                if (k < 0) ok = false;
                else a.components.push_back(k);
            }
            if (!ok) {
                err = TXF("error.aov_invalid", W(spec).c_str());
                return false;
            }
            a.swizzle = swz;
            a.exrNames = channelNames(a.name, a.components.size());
        }
        for (const AovSpec& o : out)
            if (lower(o.name) == lower(a.name)) {
                err = TXF("error.aov_duplicate", W(a.name).c_str());
                return false;
            }
        if (lower(a.name) == "r" || lower(a.name) == "g" || lower(a.name) == "b" || lower(a.name) == "beauty") {
            err = TXF("error.aov_invalid", W(spec).c_str());
            return false;
        }
        out.push_back(a);
    }
    return true;
}

void estimateMotion(const std::vector<float>& prev, const std::vector<float>& cur, int width, int height, std::vector<float>& vx, std::vector<float>& vy) {
    vx.assign((size_t)width * height, 0.0f);
    vy.assign((size_t)width * height, 0.0f);
    const int hw = width / 2, hh = height / 2;
    const int B = 8, R = 8;  // blocs de 8 pixels de l'image reduite, recherche de +-8 (16 pixels de l'image pleine)
    if (hw < B * 2 || hh < B * 2) return;
    auto half = [&](const std::vector<float>& src) {
        std::vector<float> d((size_t)hw * hh);
        for (int y = 0; y < hh; y++)
            for (int x = 0; x < hw; x++)
                d[(size_t)y * hw + x] = 0.25f * (src[(size_t)(2 * y) * width + 2 * x] + src[(size_t)(2 * y) * width + 2 * x + 1] +
                                                 src[(size_t)(2 * y + 1) * width + 2 * x] + src[(size_t)(2 * y + 1) * width + 2 * x + 1]);
        return d;
    };
    const std::vector<float> a = half(prev), b = half(cur);
    const int bx = (hw + B - 1) / B, by = (hh + B - 1) / B;
    std::vector<float> mx((size_t)bx * by, 0.0f), my((size_t)bx * by, 0.0f);
    for (int j = 0; j < by; j++) {
        for (int i = 0; i < bx; i++) {
            const int x0 = i * B, y0 = j * B, x1 = std::min(hw, x0 + B), y1 = std::min(hh, y0 + B);
            float best = 1e30f;
            int bdx = 0, bdy = 0;
            for (int dy = -R; dy <= R; dy++) {
                for (int dx = -R; dx <= R; dx++) {
                    if (x0 + dx < 0 || y0 + dy < 0 || x1 + dx > hw || y1 + dy > hh) continue;
                    float sad = 0;
                    for (int y = y0; y < y1 && sad < best; y++)
                        for (int x = x0; x < x1; x++) sad += std::fabs(a[(size_t)y * hw + x] - b[(size_t)(y + dy) * hw + x + dx]);
                    // legere preference pour un petit deplacement : un bloc uni ne doit pas voir un mouvement qui n'existe pas
                    sad += 1e-4f * (float)(std::abs(dx) + std::abs(dy));
                    if (sad < best) {
                        best = sad;
                        bdx = dx;
                        bdy = dy;
                    }
                }
            }
            mx[(size_t)j * bx + i] = (float)bdx * 2.0f;  // pixels de l'image pleine
            my[(size_t)j * bx + i] = (float)bdy * 2.0f;
        }
    }
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            const int i = std::min(bx - 1, x / (2 * B)), j = std::min(by - 1, y / (2 * B));
            vx[(size_t)y * width + x] = mx[(size_t)j * bx + i];
            vy[(size_t)y * width + x] = my[(size_t)j * bx + i];
        }
}
