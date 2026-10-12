// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "outputopts.h"
#include "formats.h"
#include "i18n.h"
#include "util.h"

namespace {

const char* const kFilters[kScaleFilterCount] = {"lanczos", "bicubic", "bilinear", "area", "neighbor"};
const char* const kFilterKeys[kScaleFilterCount] = {"ui.scalefilter.lanczos", "ui.scalefilter.bicubic", "ui.scalefilter.bilinear", "ui.scalefilter.area",
                                                    "ui.scalefilter.neighbor"};

std::wstring trim(std::wstring t) {
    while (!t.empty() && iswspace(t.back())) t.pop_back();
    size_t i = 0;
    while (i < t.size() && iswspace(t[i])) i++;
    return t.substr(i);
}

// Nombre saisi (virgule ou point) ; vide -> `empty` ; faux si ce n'est pas un nombre
bool number(const std::wstring& raw, double& out, double empty) {
    std::wstring t = trim(raw);
    if (t.empty()) {
        out = empty;
        return true;
    }
    for (auto& c : t)
        if (c == L',') c = L'.';
    wchar_t* end = nullptr;
    out = wcstod(t.c_str(), &end);
    return end != t.c_str() && *end == 0;
}

}  // namespace

const char* scaleFilterId(int i) { return kFilters[i < 0 || i >= kScaleFilterCount ? 0 : i]; }
const char* scaleFilterLabelKey(int i) { return kFilterKeys[i < 0 || i >= kScaleFilterCount ? 0 : i]; }

int scaleFilterIndex(const std::string& id) {
    for (int i = 0; i < kScaleFilterCount; i++)
        if (id == kFilters[i]) return i;
    return 0;
}

void outputFieldsFrom(const Settings& s, OutputFields& f) {
    f.alpha = s.alpha;
    f.hdr = s.hdr == "pq" ? 1 : s.hdr == "hlg" ? 2 : 0;
    f.hdrPeak = std::to_wstring(s.hdrPeak);
    f.hdrFall = std::to_wstring(s.hdrFall);
    f.hdrWhite = std::to_wstring(s.hdrWhite);
    f.scale = W(s.scale);
    f.scaleFilter = scaleFilterIndex(s.scaleFilter);
    f.interp = s.interp > 0 ? i18n::num(s.interp) : L"";
    f.loudnorm = s.loudnorm != 0 ? i18n::num(s.loudnorm) : L"";
    f.title = s.title;
    f.credit = s.credit;
    f.titleDur = i18n::num(s.titleDur);
}

bool outputFieldsApply(const OutputFields& f, Settings& s, std::wstring& err) {
    Settings t = s;
    t.alpha = f.alpha;
    t.hdr = f.hdr == 1 ? "pq" : f.hdr == 2 ? "hlg" : "off";
    auto whole = [&](const std::wstring& txt, int lo, int hi, int& out) {
        double v = 0;
        if (!number(txt, v, -1) || v < lo || v > hi || v != std::floor(v)) return false;
        out = (int)v;
        return true;
    };
    if (!whole(f.hdrPeak, 100, 10000, t.hdrPeak) || !whole(f.hdrFall, 10, 10000, t.hdrFall) || !whole(f.hdrWhite, 20, 1000, t.hdrWhite)) {
        err = TX("error.hdr_values_invalid");
        return false;
    }
    t.scale = lower(U8(trim(f.scale)));
    if (t.scale == "off") t.scale.clear();
    int ow = 0, oh = 0;
    t.width = s.width;
    t.height = s.height;
    if (!t.scale.empty() && !outputSize(t, ow, oh)) {
        err = TXF("error.scale_invalid", f.scale.c_str());
        return false;
    }
    t.scaleFilter = scaleFilterId(f.scaleFilter);
    double v = 0;
    if (!number(f.interp, v, 0) || v < 0 || v > 480) {
        err = TX("error.interp_invalid");
        return false;
    }
    t.interp = v;
    if (!number(f.loudnorm, v, 0) || (v != 0 && (v < -70 || v > -5))) {
        err = TX("error.loudnorm_invalid");
        return false;
    }
    t.loudnorm = v;
    if (!number(f.titleDur, v, 3) || v < 0.5 || v > 60) {
        err = TX("error.titledur_invalid");
        return false;
    }
    t.titleDur = v;
    t.title = trim(f.title);
    t.credit = trim(f.credit);
    s = t;
    return true;
}
