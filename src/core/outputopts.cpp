// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "outputopts.h"
#include "formats.h"
#include "i18n.h"
#include "publish.h"
#include "timemap.h"
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
    f.name = s.name;
    f.exists = s.exists == "number" ? 1 : s.exists == "skip" ? 2 : 0;
    f.chunk = i18n::num(s.chunk);
    f.pbo = std::to_wstring(s.pbo);
    f.shutter = i18n::num(s.shutter);
    f.endAction = s.endaction == "folder" ? 1 : s.endaction == "sleep" ? 2 : 0;
    f.metadata = s.metadata;
    f.loopMode = s.loopMode == "crossfade" ? 1 : s.loopMode == "pingpong" ? 2 : 0;
    f.loopFade = i18n::num(s.loopFade);
    f.timemap = W(s.timemap);
    f.speed = s.speed != 1.0 ? i18n::num(s.speed) : L"";
    f.samples = s.samples > 1 ? std::to_wstring(s.samples) : L"";
    f.tile = s.tile > 0 ? std::to_wstring(s.tile) : L"";
    f.audioFollow = s.audioFollow;
    f.precheck = s.precheck;
    f.target = W(s.target);
    f.targetSize = s.targetSizeMB > 0 ? i18n::num(s.targetSizeMB) : L"";
    f.crop = W(s.crop);
    f.cacheMax = i18n::num(s.cacheMaxGB);
    f.masterCache = s.masterCache;
    f.embed = s.embed;
    f.autoCredit = s.autoCredit;
    f.animPreview = s.animPreview == "gif" ? 1 : s.animPreview == "webp" ? 2 : 0;
    f.stillFormat = s.stillFormat == "tiff" ? 1 : 0;
    f.dpi = s.dpi > 0 ? std::to_wstring(s.dpi) : L"";
    f.bpm = s.bpm > 0 ? i18n::num(s.bpm) : L"";
    f.parallel = std::to_wstring(s.parallel);
    f.farm = s.farm;
    f.beatsPerBar = std::to_wstring(s.beatsPerBar);
    f.markers = s.markers == "chapters" ? 1 : s.markers == "file" ? 2 : s.markers == "all" ? 3 : 0;
    f.beatSync = s.beatSync == "beat" ? 1 : s.beatSync == "bar" ? 2 : s.beatSync == "section" ? 3 : 0;
    f.source = s.source;
    f.source2 = s.source2;
    f.sourceMatch = s.sourceMatch == "none" ? 3 : s.sourceMatch == "size" ? 2 : s.sourceMatch == "size,fps" ? 1 : 0;
    f.sourceAudio = s.sourceAudio;
    f.reverse = s.reverse;
    f.sourceFps = s.sourceFps > 0 ? i18n::num(s.sourceFps) : L"";
    f.transDur = i18n::num(s.transitionDur);
    f.transOffset = s.transitionOffset != 0 ? i18n::num(s.transitionOffset) : L"";
    f.variants.clear();
    for (const std::string& v : s.variants) f.variants += (f.variants.empty() ? L"" : L" ; ") + W(v);
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
    t.name = trim(f.name);
    t.exists = f.exists == 1 ? "number" : f.exists == 2 ? "skip" : "overwrite";
    t.endaction = f.endAction == 1 ? "folder" : f.endAction == 2 ? "sleep" : "none";
    t.metadata = f.metadata;
    t.variants.clear();
    {
        const std::string all = U8(f.variants);
        size_t pos = 0;
        while (pos <= all.size()) {
            size_t e = all.find(';', pos);
            if (e == std::string::npos) e = all.size();
            const std::string v = trim(all.substr(pos, e - pos));
            pos = e + 1;
            if (v.empty()) continue;
            const size_t colon = v.find(':');
            const std::string name = trim(colon == std::string::npos ? v : v.substr(0, colon));
            if (name.empty() || (colon != std::string::npos && v.find('=', colon) == std::string::npos)) {
                err = TXF("error.variant_invalid", W(v).c_str());
                return false;
            }
            t.variants.push_back(v);
        }
    }
    // temps et boucle
    {
        t.loopMode = f.loopMode == 1 ? "crossfade" : f.loopMode == 2 ? "pingpong" : "none";
        double fade = 0, speed = 0, samples = 0;
        if (!number(f.loopFade, fade, 1) || fade < 0.05 || fade > 600 || !number(f.speed, speed, 1) || speed == 0 || std::fabs(speed) > 100 ||
            !number(f.samples, samples, 1) || samples < 1 || samples > 100000 || samples != std::floor(samples)) {
            err = TX("error.time_invalid");
            return false;
        }
        std::string curve;
        for (char c : U8(f.timemap))
            if (!isspace((unsigned char)c)) curve += c;
        TimeMap check;
        if (!TimeMap::parse(curve, check, err)) return false;
        double tile = 0, dpi = 0;
        if (!number(f.tile, tile, 0) || (tile != 0 && (tile < 64 || tile > 16384)) || tile != std::floor(tile) || !number(f.dpi, dpi, 0) || dpi < 0 || dpi > 10000 || dpi != std::floor(dpi)) {
            err = TX("error.still_invalid");
            return false;
        }
        t.audioFollow = f.audioFollow;
        t.precheck = f.precheck;
        double sizeMB = 0, cacheGB = 10, cw = 0, ch = 0;
        const std::string sizeText = U8(trim(f.targetSize)), cropText = U8(trim(f.crop));
        if ((!sizeText.empty() && !parseSizeMB(sizeText, sizeMB)) || !number(f.cacheMax, cacheGB, 10) || cacheGB < 0.1 || cacheGB > 10000 ||
            (!cropText.empty() && lower(cropText) != "off" && (sscanf(cropText.c_str(), "%lf:%lf", &cw, &ch) != 2 || cw <= 0 || ch <= 0))) {
            err = TX("error.publish_invalid");
            return false;
        }
        t.target = lower(U8(trim(f.target)));
        t.targetSizeMB = sizeMB;
        t.crop = lower(cropText) == "off" ? "" : cropText;
        t.cacheMaxGB = cacheGB;
        t.masterCache = f.masterCache;
        t.embed = f.embed;
        t.autoCredit = f.autoCredit;
        t.animPreview = f.animPreview == 1 ? "gif" : f.animPreview == 2 ? "webp" : "off";
        t.stillFormat = f.stillFormat == 1 ? "tiff" : "png";
        t.tile = (int)tile;
        t.dpi = (int)dpi;
        t.loopFade = fade;
        t.speed = speed;
        t.samples = (int)samples;
        t.timemap = curve;
    }
    double bpm = 0, bars = 4;
    if (!number(f.bpm, bpm, 0) || bpm < 0 || bpm > 400 || (bpm > 0 && bpm < 20) || !number(f.beatsPerBar, bars, 4) || bars < 2 || bars > 12 || bars != std::floor(bars)) {
        err = TX("error.rhythm_invalid");
        return false;
    }
    t.bpm = bpm;
    t.beatsPerBar = (int)bars;
    t.markers = f.markers == 1 ? "chapters" : f.markers == 2 ? "file" : f.markers == 3 ? "all" : "off";
    t.beatSync = f.beatSync == 1 ? "beat" : f.beatSync == 2 ? "bar" : f.beatSync == 3 ? "section" : "off";
    {
        double sfps = 0, tdur = 1, toff = 0;
        if (!number(f.sourceFps, sfps, 0) || sfps < 0 || sfps > 1000 || !number(f.transDur, tdur, 1) || tdur < 0 || tdur > 600 || !number(f.transOffset, toff, 0) ||
            toff < -600 || toff > 600) {
            err = TX("error.source_invalid");
            return false;
        }
        std::wstring a = trim(f.source), b = trim(f.source2);
        if (lower(U8(a)) == "off") a.clear();
        if (lower(U8(b)) == "off") b.clear();
        t.source = a;
        t.source2 = b;
        t.sourceMatch = f.sourceMatch == 3 ? "none" : f.sourceMatch == 2 ? "size" : f.sourceMatch == 1 ? "size,fps" : "size,fps,duration";
        t.sourceAudio = f.sourceAudio;
        t.reverse = f.reverse;
        t.sourceFps = sfps;
        t.transitionDur = tdur;
        t.transitionOffset = toff;
    }
    double par = 1;
    if (!number(f.parallel, par, 1) || par < 1 || par > 16 || par != std::floor(par)) {
        err = TX("error.parallel_invalid");
        return false;
    }
    t.parallel = (int)par;
    t.farm = trim(U8(f.farm)) == "off" ? L"" : W(trim(U8(f.farm)));
    double chunk = 0, shutter = 0, pbo = 0;
    if (!number(f.chunk, chunk, 0) || chunk < 0 || chunk > 86400 || !number(f.shutter, shutter, 0.5) || shutter < 0.01 || shutter > 1 || !number(f.pbo, pbo, 3) ||
        pbo < 2 || pbo > 8 || pbo != std::floor(pbo)) {
        err = TX("error.adv_invalid");
        return false;
    }
    t.chunk = chunk;
    t.shutter = shutter;
    t.pbo = (int)pbo;
    s = t;
    return true;
}
