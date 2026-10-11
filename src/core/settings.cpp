// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "settings.h"
#include "i18n.h"
#include "util.h"
#include "formats.h"
#include "platform/platform.h"

static bool sameChannels(const Settings& a, const Settings& b) {
    for (int i = 0; i < 4; i++)
        if (a.channel[i] != b.channel[i] || a.chFilter[i] != b.chFilter[i] || a.chWrap[i] != b.chWrap[i] || a.chVflip[i] != b.chVflip[i])
            return false;
    return true;
}

bool sameSettings(const Settings& a, const Settings& b) {
    return a.width == b.width && a.height == b.height && a.fps == b.fps && a.duration == b.duration &&
           a.start == b.start && a.crf == b.crf && a.preset == b.preset && a.codec == b.codec &&
           a.container == b.container && a.ss == b.ss && a.outdir == b.outdir &&
           a.bitrate == b.bitrate && a.color == b.color && a.blur == b.blur && a.shutter == b.shutter &&
           a.loop == b.loop && a.audio == b.audio && a.fadein == b.fadein && a.fadeout == b.fadeout &&
           sameChannels(a, b) && a.mouseSet == b.mouseSet && a.mouseX == b.mouseX && a.mouseY == b.mouseY &&
           a.mouseDown == b.mouseDown && a.date == b.date && a.depth == b.depth && a.name == b.name && a.exists == b.exists &&
           a.encoder == b.encoder && a.pbo == b.pbo && a.chunk == b.chunk && a.metadata == b.metadata;
}

static void applyKV(Settings& s, const std::string& kIn, const std::string& vIn) {
    std::string k = lower(trim(kIn)), v = trim(vIn);
    if (k == "outdir") { s.outdir = W(v); return; }  // chemin complet (peut contenir des espaces)
    if (k == "audio") { s.audio = W(v); return; }
    if (k.size() == 9 && k.compare(0, 8, "ichannel") == 0 && k[8] >= '0' && k[8] <= '3') {  // chemin complet
        s.channel[k[8] - '0'] = W(v);
        return;
    }
    if (k == "name") { s.name = W(v); return; }  // modele du nom de sortie (peut contenir des espaces)
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
        else if (k == "metadata") s.metadata = !(v == "0" || lower(v) == "off" || lower(v) == "false" || lower(v) == "no");
        else if (k.size() > 9 && k.compare(0, 8, "ichannel") == 0 && k[8] >= '0' && k[8] <= '3' && k[9] == '.') {
            const int i = k[8] - '0';
            const std::string sub = k.substr(10), val = lower(v);
            if (sub == "filter") s.chFilter[i] = (val == "nearest" || val == "linear") ? val : "mipmap";
            else if (sub == "wrap") s.chWrap[i] = val == "clamp" ? val : "repeat";
            else if (sub == "vflip") s.chVflip[i] = !(val == "0" || val == "false" || val == "off" || val == "no");
        }
        else if (k == "mouse") {  // x,y[,down] : fractions de la taille, origine en bas a gauche
            double x = 0, y = 0;
            if (sscanf(v.c_str(), "%lf,%lf", &x, &y) == 2) {
                s.mouseSet = true;
                s.mouseX = std::min(1.0, std::max(0.0, x));
                s.mouseY = std::min(1.0, std::max(0.0, y));
                s.mouseDown = lower(v).find("down") != std::string::npos;
            } else if (lower(v) == "off") {
                s.mouseSet = false;
            }
        }
        else if (k == "date") s.date = lower(v) == "off" ? "" : v;
        else if (k == "chunk") s.chunk = std::stod(v);
        else if (k == "encoder") {
            std::string c = lower(v);
            s.encoder = (c == "auto" || c == "nvenc" || c == "amf" || c == "qsv") ? c : "cpu";
        }
        else if (k == "color") {
            std::string c = lower(v);
            s.color = (c == "bt709full" || c == "srgb" || c == "bt601") ? c : "bt709";
        }
        else if (k == "container") s.container = normalizeContainer(v);
        else if (k == "codec") s.codec = normalizeCodec(v);
        else if (k == "depth") s.depth = std::stoi(v) >= 10 ? 10 : 8;
        else if (k == "exists") {
            const std::string c = lower(v);
            s.exists = (c == "number" || c == "skip") ? c : "overwrite";
        }
    } catch (...) {}
}

bool isSettingKey(const std::string& key) {
    static const char* const kKeys[] = {"width", "height", "fps", "duration", "start", "crf", "preset", "ss", "supersample", "bitrate",
                                        "blur", "shutter", "loop", "fadein", "fadeout", "pbo", "chunk", "encoder", "color", "container",
                                        "codec", "depth", "exists", "name", "outdir", "audio", "metadata", "mouse", "date"};
    for (const char* k : kKeys)
        if (key == k) return true;
    // ichannel0 a ichannel3, avec .filter .wrap .vflip
    if (key.size() >= 9 && key.compare(0, 8, "ichannel") == 0 && key[8] >= '0' && key[8] <= '3')
        return key.size() == 9 || key.substr(9) == ".filter" || key.substr(9) == ".wrap" || key.substr(9) == ".vflip";
    return false;
}

void applySetting(Settings& s, const std::string& key, const std::string& value) { applyKV(s, key, value); }

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
    put("depth", std::to_string(s.depth), "ini.depth");
    put("name", U8(s.name), "ini.name");
    put("exists", s.exists, "ini.exists");
    for (int i = 0; i < 4; i++) {
        if (s.channel[i].empty()) continue;
        const std::string key = "iChannel" + std::to_string(i);
        put(key, U8(s.channel[i]), i == 0 ? "ini.ichannel" : nullptr);
        if (!s.chFilter[i].empty()) put(key + ".filter", s.chFilter[i]);
        if (!s.chWrap[i].empty()) put(key + ".wrap", s.chWrap[i]);
        put(key + ".vflip", s.chVflip[i] ? "1" : "0");
    }
    if (s.mouseSet) put("mouse", num(s.mouseX) + "," + num(s.mouseY) + (s.mouseDown ? ",down" : ""), "ini.mouse");
    if (!s.date.empty()) put("date", s.date, "ini.date");
    put("chunk", num(s.chunk), "ini.chunk");
    put("metadata", s.metadata ? "1" : "0", "ini.metadata");
    fclose(f);
}
