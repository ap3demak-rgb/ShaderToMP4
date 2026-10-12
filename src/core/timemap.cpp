// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "timemap.h"
#include "i18n.h"
#include "util.h"

namespace {

bool number(const std::string& t, double& v) {
    if (t.empty()) return false;
    char* end = nullptr;
    v = strtod(t.c_str(), &end);
    return end && *end == 0 && std::isfinite(v);
}

}  // namespace

bool TimeMap::parse(const std::string& specIn, TimeMap& out, std::wstring& err) {
    out = TimeMap();
    const std::string spec = trim(specIn);
    if (spec.empty() || lower(spec) == "off") return true;
    std::vector<Pt> pts;
    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t e = spec.find_first_of(";,", pos);
        if (e == std::string::npos) e = spec.size();
        const std::string item = trim(spec.substr(pos, e - pos));
        pos = e + 1;
        if (item.empty()) continue;
        const size_t colon = item.find(':');
        Pt p;
        if (colon == std::string::npos || !number(trim(item.substr(0, colon)), p.x) || !number(trim(item.substr(colon + 1)), p.y) || p.x < 0) {
            err = TXF("error.timemap_invalid", W(item).c_str());
            return false;
        }
        if (!pts.empty() && p.x <= pts.back().x) {
            err = TXF("error.timemap_order", W(item).c_str());
            return false;
        }
        pts.push_back(p);
    }
    if (pts.empty()) return true;
    if (pts.front().x > 0) pts.insert(pts.begin(), Pt{0, 0, 0});
    if (pts.size() < 2) {
        err = TX("error.timemap_points");
        return false;
    }
    // Pentes : cubique monotone par morceaux (Fritsch-Carlson), donc une pente nulle aux extrema et pas de dépassement
    const size_t n = pts.size();
    std::vector<double> h(n - 1), d(n - 1);
    for (size_t k = 0; k + 1 < n; k++) {
        h[k] = pts[k + 1].x - pts[k].x;
        d[k] = (pts[k + 1].y - pts[k].y) / h[k];
    }
    auto edge = [&](double h0, double h1, double d0, double d1) {
        double m = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        if ((m > 0) != (d0 > 0) || m == 0) m = 0;
        else if ((d0 > 0) != (d1 > 0) && std::fabs(m) > 3 * std::fabs(d0)) m = 3 * d0;
        return m;
    };
    if (n == 2) {
        pts[0].m = pts[1].m = d[0];
    } else {
        for (size_t k = 1; k + 1 < n; k++) {
            if (d[k - 1] * d[k] <= 0) pts[k].m = 0;
            else {
                const double w1 = 2 * h[k] + h[k - 1], w2 = h[k] + 2 * h[k - 1];
                pts[k].m = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k]);
            }
        }
        pts[0].m = edge(h[0], h[1], d[0], d[1]);
        pts[n - 1].m = edge(h[n - 2], h[n - 3], d[n - 2], d[n - 3]);
    }
    out.pts_ = pts;
    return true;
}

TimeMap TimeMap::pingPong(double half) {
    TimeMap t;
    t.pingpong_ = true;
    t.half_ = std::max(1e-6, half);
    return t;
}

double TimeMap::base(double u) const {
    if (pingpong_) {
        const double period = 2 * half_;
        double p = std::fmod(u, period);
        if (p < 0) p += period;
        return p <= half_ ? p : period - p;
    }
    if (pts_.empty()) return u;
    if (u <= pts_.front().x) return pts_.front().y + pts_.front().m * (u - pts_.front().x);  // avant la courbe (chauffe) : droite
    if (u >= pts_.back().x) return pts_.back().y + pts_.back().m * (u - pts_.back().x);
    size_t k = 0;
    while (k + 2 < pts_.size() && u >= pts_[k + 1].x) k++;
    const Pt &a = pts_[k], &b = pts_[k + 1];
    const double h = b.x - a.x, t = (u - a.x) / h, t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * a.y + (t3 - 2 * t2 + t) * h * a.m + (-2 * t3 + 3 * t2) * b.y + (t3 - t2) * h * b.m;
}

double TimeMap::baseSlope(double u) const {
    if (pingpong_) {
        const double period = 2 * half_;
        double p = std::fmod(u, period);
        if (p < 0) p += period;
        return p < half_ ? 1.0 : -1.0;
    }
    if (pts_.empty()) return 1.0;
    if (u <= pts_.front().x) return pts_.front().m;
    if (u >= pts_.back().x) return pts_.back().m;
    size_t k = 0;
    while (k + 2 < pts_.size() && u >= pts_[k + 1].x) k++;
    const Pt &a = pts_[k], &b = pts_[k + 1];
    const double h = b.x - a.x, t = (u - a.x) / h, t2 = t * t;
    return ((6 * t2 - 6 * t) * a.y + (3 * t2 - 4 * t + 1) * h * a.m + (-6 * t2 + 6 * t) * b.y + (3 * t2 - 2 * t) * h * b.m) / h;
}

double TimeMap::at(double u) const { return speed_ * base(u); }

double TimeMap::speedAt(double u) const { return speed_ * baseSlope(u); }

bool TimeMap::monotone() const {
    if (pingpong_) return false;
    if (speed_ < 0) return false;
    for (size_t k = 0; k + 1 < pts_.size(); k++)
        if (pts_[k + 1].y < pts_[k].y) return false;
    return pts_.empty() || pts_.back().m >= 0;
}

void TimeMap::speedRange(double duration, double& lo, double& hi) const {
    lo = hi = speedAt(0);
    const int steps = 400;
    for (int i = 1; i <= steps; i++) {
        const double v = speedAt(duration * i / steps);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
}

std::string TimeMap::canonical() const {
    char b[96];
    snprintf(b, sizeof(b), "tm:%d|%g|%g", pingpong_ ? 1 : 0, half_, speed_);
    std::string o = b;
    for (const Pt& p : pts_) {
        snprintf(b, sizeof(b), "|%g:%g", p.x, p.y);
        o += b;
    }
    return o;
}
