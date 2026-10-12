// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "dateinput.h"
#include "util.h"

// Jours depuis 1970-01-01 pour une date du calendrier gregorien, et l'inverse (algorithmes de H. Hinnant)
static long long daysFromCivil(long long y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civilFromDays(long long z, int& y, int& m, int& d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (m <= 2));
}

DateInput dateInputFrom(const std::string& text) {
    DateInput r;
    r.base = platform::localTime();
    if (text.empty()) return r;
    std::string dt = text;
    const size_t plus = lower(dt).find("+rate=");
    if (plus != std::string::npos) {
        char* end = nullptr;
        const double v = strtod(dt.c_str() + plus + 6, &end);
        if (end != dt.c_str() + plus + 6) r.rate = v;
        dt.resize(plus);
    }
    for (auto& c : dt)
        if (c == 'T' || c == 't' || c == '_') c = ' ';
    int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
    if (sscanf(dt.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mi, &ss) >= 3 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31) {
        r.base.year = y; r.base.month = mo; r.base.day = d;
        r.base.hour = hh; r.base.minute = mi; r.base.second = ss;
    }
    return r;
}

void dateAt(const DateInput& d, double t, float out[4]) {
    const double secs = d.base.hour * 3600.0 + d.base.minute * 60.0 + d.base.second + d.rate * t;
    const double dayShift = std::floor(secs / 86400.0);
    int y = d.base.year, m = d.base.month, day = d.base.day;
    if (dayShift != 0) civilFromDays(daysFromCivil(y, m, day) + (long long)dayShift, y, m, day);
    out[0] = (float)y;
    out[1] = (float)(m - 1);
    out[2] = (float)day;
    out[3] = (float)(secs - dayShift * 86400.0);
}
