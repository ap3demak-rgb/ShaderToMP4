// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "presets.h"
#include "util.h"
#include "platform/platform.h"
#include <cwctype>

static const char* const kKeys[kBuiltinPresetCount] = {"ui.chip.draft",  "ui.chip.youtube", "ui.chip.4k60", "ui.chip.vertical",
                                                       "ui.preset.x",    "ui.preset.gif",   "ui.preset.prores", "ui.preset.webm"};

const char* builtinPresetKey(int idx) { return kKeys[std::min(kBuiltinPresetCount - 1, std::max(0, idx))]; }

void applyBuiltinPreset(int idx, Settings& s) {
    s.blur = 1;
    s.ss = 1;
    s.bitrate = 0;
    s.depth = 8;
    s.container = "mp4";
    switch (idx) {
        case 0:  // brouillon : rapide pour tester
            s.width = 1280; s.height = 720; s.fps = 30; s.crf = 23; s.preset = "veryfast"; s.codec = "h264";
            break;
        case 1:  // YouTube 1080p60
            s.width = 1920; s.height = 1080; s.fps = 60; s.crf = 16; s.preset = "slow"; s.codec = "h264";
            break;
        case 2:  // 4K 60 images/s
            s.width = 3840; s.height = 2160; s.fps = 60; s.crf = 20; s.preset = "medium"; s.codec = "h265";
            break;
        case 3:  // vertical 9:16 (Instagram, TikTok)
            s.width = 1080; s.height = 1920; s.fps = 30; s.crf = 16; s.preset = "slow"; s.codec = "h264";
            break;
        case 4:  // X : 720p30, fichier leger
            s.width = 1280; s.height = 720; s.fps = 30; s.crf = 20; s.preset = "medium"; s.codec = "h264";
            break;
        case 5:  // GIF leger : petite taille, 15 images/s, 8 s au plus
            s.width = 480; s.height = 270; s.fps = 15; s.codec = "h264"; s.container = "gif";
            s.duration = std::min(s.duration, 8.0);
            break;
        case 6:  // ProRes 422 HQ pour le montage, 10 bits
            s.width = 1920; s.height = 1080; s.fps = 30; s.codec = "prores_hq"; s.container = "mov"; s.depth = 10;
            break;
        default:  // WebM VP9 pour le web
            s.width = 1280; s.height = 720; s.fps = 30; s.crf = 24; s.preset = "medium"; s.codec = "vp9"; s.container = "webm";
            break;
    }
}

std::vector<UserPreset> userPresets(const std::wstring& exeDir) {
    std::vector<UserPreset> out;
    const std::wstring dir = exeDir + L"presets";
    std::vector<platform::DirEntry> entries;
    if (!platform::listDir(dir, entries)) return out;
    for (const auto& e : entries) {
        if (e.isDir || e.name.size() <= 4) continue;
        std::wstring ext = e.name.substr(e.name.size() - 4);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        if (ext != L".ini") continue;
        out.push_back({e.name.substr(0, e.name.size() - 4), dir + platform::pathSeparator() + e.name});
    }
    std::sort(out.begin(), out.end(), [](const UserPreset& a, const UserPreset& b) { return a.name < b.name; });
    return out;
}

Settings loadUserPreset(const UserPreset& p, bool* ok) {
    Settings s;
    std::string text;
    const bool good = readFile(p.path, text);
    if (good) parseLines(s, text, false);
    if (ok) *ok = good;
    return s;
}
