// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "settings.h"
#include "i18n.h"
#include "util.h"
#include "platform/platform.h"

bool sameSettings(const Settings& a, const Settings& b) {
    return a.width == b.width && a.height == b.height && a.fps == b.fps && a.duration == b.duration &&
           a.start == b.start && a.crf == b.crf && a.preset == b.preset && a.codec == b.codec &&
           a.container == b.container && a.ss == b.ss && a.outdir == b.outdir &&
           a.bitrate == b.bitrate && a.color == b.color && a.blur == b.blur && a.shutter == b.shutter &&
           a.loop == b.loop && a.audio == b.audio && a.fadein == b.fadein && a.fadeout == b.fadeout &&
           a.encoder == b.encoder && a.pbo == b.pbo && a.chunk == b.chunk;
}

static void applyKV(Settings& s, const std::string& kIn, const std::string& vIn) {
    std::string k = lower(trim(kIn)), v = trim(vIn);
    if (k == "outdir") { s.outdir = W(v); return; }  // chemin complet (peut contenir des espaces)
    if (k == "audio") { s.audio = W(v); return; }
    // ne garder que le premier mot de la valeur (ignore un commentaire derriere)
    size_t sp = v.find_first_of(" \t");
    if (sp != std::string::npos) v = v.substr(0, sp);
    if (v.empty()) return;
    try {
        if (k == "width") s.width = std::stoi(v);
        else if (k == "height") s.height = std::stoi(v);
        else if (k == "fps") s.fps = std::stod(v);
        else if (k == "duration") s.duration = std::stod(v);
        else if (k == "start") s.start = std::stod(v);
        else if (k == "crf") s.crf = std::stoi(v);
        else if (k == "preset") s.preset = lower(v);
        else if (k == "ss" || k == "supersample") s.ss = std::stoi(v);
        else if (k == "bitrate") s.bitrate = std::stoi(v);
        else if (k == "blur") s.blur = std::stoi(v);
        else if (k == "shutter") s.shutter = std::stod(v);
        else if (k == "loop") s.loop = std::stod(v);
        else if (k == "fadein") s.fadein = std::stod(v);
        else if (k == "fadeout") s.fadeout = std::stod(v);
        else if (k == "pbo") s.pbo = std::stoi(v);
        else if (k == "chunk") s.chunk = std::stod(v);
        else if (k == "encoder") {
            std::string c = lower(v);
            s.encoder = (c == "auto" || c == "nvenc" || c == "amf" || c == "qsv") ? c : "cpu";
        }
        else if (k == "color") {
            std::string c = lower(v);
            s.color = (c == "bt709full" || c == "srgb" || c == "bt601") ? c : "bt709";
        }
        else if (k == "container") s.container = (v == "mov" || v == "mkv") ? lower(v) : "mp4";
        else if (k == "codec") {
            std::string c = lower(v);
            s.codec = (c == "h265" || c == "hevc" || c == "x265") ? "h265" : "h264";
        }
    } catch (...) {}
}

void parseLines(Settings& s, const std::string& text, bool onlySlashComments) {
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (onlySlashComments) {
            if (line.compare(0, 2, "//") != 0) continue;
            line = trim(line.substr(2));
        } else if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        applyKV(s, line.substr(0, eq), line.substr(eq + 1));
    }
}

void writeIni(const std::wstring& path, const Settings& s) {
    FILE* f = platform::openFile(path, "wb");
    if (!f) return;
    // Les valeurs gardent toujours le point décimal ; seuls les commentaires suivent la langue de l'interface.
    auto num = [](double v) {
        char b[48];
        snprintf(b, sizeof(b), "%g", v);
        return std::string(b);
    };
    auto put = [&](const std::string& key, const std::string& value, const char* commentKey = nullptr) {
        std::string l = key + "=" + value;
        if (commentKey) l += "      ; " + i18n::tr8(commentKey);
        l += "\r\n";
        fputs(l.c_str(), f);
    };
    fputs(("; " + i18n::tr8("ini.header1") + "\r\n; " + i18n::tr8("ini.header2") + "\r\n").c_str(), f);
    put("width", std::to_string(s.width));
    put("height", std::to_string(s.height));
    put("fps", num(s.fps));
    put("duration", num(s.duration), "ini.duration");
    put("start", num(s.start), "ini.start");
    put("crf", std::to_string(s.crf), "ini.crf");
    put("preset", s.preset, "ini.preset");
    put("codec", s.codec, "ini.codec");
    put("container", s.container, "ini.container");
    put("ss", std::to_string(s.ss), "ini.ss");
    put("outdir", U8(s.outdir));
    put("bitrate", std::to_string(s.bitrate), "ini.bitrate");
    put("color", s.color, "ini.color");
    put("blur", std::to_string(s.blur), "ini.blur");
    put("shutter", num(s.shutter), "ini.shutter");
    put("loop", num(s.loop), "ini.loop");
    put("audio", U8(s.audio));
    put("fadein", num(s.fadein), "ini.fadein");
    put("fadeout", num(s.fadeout), "ini.fadeout");
    put("encoder", s.encoder, "ini.encoder");
    put("pbo", std::to_string(s.pbo), "ini.pbo");
    put("chunk", num(s.chunk), "ini.chunk");
    fclose(f);
}
