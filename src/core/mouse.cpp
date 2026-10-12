// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "mouse.h"
#include "util.h"

static double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

bool parseMouseKeys(const std::string& text, std::vector<MouseKey>& out) {
    std::vector<MouseKey> keys;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find(';', pos);
        if (e == std::string::npos) e = text.size();
        const std::string part = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (part.empty()) continue;
        MouseKey k;
        std::string body = part;
        const size_t colon = part.find(':');
        if (colon != std::string::npos) {
            char* end = nullptr;
            const std::string ts = trim(part.substr(0, colon));
            k.t = strtod(ts.c_str(), &end);
            if (ts.empty() || *end != 0 || k.t < 0) return false;
            body = trim(part.substr(colon + 1));
        } else if (!keys.empty() || e < text.size()) {
            return false;  // plusieurs cles : chacune doit porter son temps
        }
        double x = 0, y = 0;
        if (sscanf(body.c_str(), "%lf , %lf", &x, &y) != 2) return false;
        k.x = clamp01(x);
        k.y = clamp01(y);
        k.down = lower(body).find("down") != std::string::npos;
        if (!keys.empty() && k.t < keys.back().t) return false;  // temps croissants
        keys.push_back(k);
    }
    if (keys.empty()) return false;
    out = keys;
    return true;
}

std::string formatMouseKeys(const std::vector<MouseKey>& keys) {
    std::string r;
    char b[96];
    for (size_t i = 0; i < keys.size(); i++) {
        const MouseKey& k = keys[i];
        if (keys.size() == 1 && k.t == 0) snprintf(b, sizeof(b), "%g,%g%s", k.x, k.y, k.down ? ",down" : "");
        else snprintf(b, sizeof(b), "%s%g:%g,%g%s", i ? " ; " : "", k.t, k.x, k.y, k.down ? ",down" : "");
        r += b;
    }
    return r;
}

void mouseAt(const std::vector<MouseKey>& keys, double t, double dt, int w, int h, float out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    if (keys.empty()) return;
    // position : cle precedente / suivante, interpolation lineaire ; avant la premiere cle ou apres la derniere : constante
    size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].t <= t) i++;
    double x = keys[i].x, y = keys[i].y;
    if (t < keys[0].t) {
        x = keys[0].x;
        y = keys[0].y;
    } else if (i + 1 < keys.size() && keys[i + 1].t > keys[i].t) {
        const double a = (t - keys[i].t) / (keys[i + 1].t - keys[i].t);
        x += (keys[i + 1].x - x) * a;
        y += (keys[i + 1].y - y) * a;
    }
    // bouton : etat de la derniere cle atteinte (la premiere avant t0) ; un clic commence a une cle « down » precedee d'une cle relachee
    const size_t cur = t < keys[0].t ? 0 : i;
    bool down = keys[cur].down;
    if (t < keys[0].t) down = false;
    // dernier clic : cle « down » la plus recente dont la precedente est relachee (ou la premiere cle)
    long click = -1;
    for (size_t j = 0; j <= cur; j++)
        if (keys[j].down && (j == 0 || !keys[j - 1].down)) click = (long)j;
    out[0] = (float)(x * w);
    out[1] = (float)(y * h);
    if (click < 0) return;  // aucun clic encore : zw = 0
    const MouseKey& c = keys[(size_t)click];
    const float cx = (float)(c.x * w), cy = (float)(c.y * h);
    out[2] = down ? cx : -cx;
    const bool first = t >= c.t && t - c.t < dt * 0.999 && down;  // image sur laquelle le clic commence
    out[3] = first ? cy : -cy;
}
