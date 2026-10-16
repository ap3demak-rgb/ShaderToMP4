// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "settings.h"
#include "publish.h"
#include "i18n.h"
#include "util.h"
#include "formats.h"
#include "platform/platform.h"

static bool sameChannels(const Settings& a, const Settings& b) {
    for (int i = 0; i < 4; i++)
        if (a.bufScale[i] != b.bufScale[i] || a.bufW[i] != b.bufW[i] || a.bufH[i] != b.bufH[i] ||
a.channel[i] != b.channel[i] || a.chFilter[i] != b.chFilter[i] || a.chWrap[i] != b.chWrap[i] || a.chVflip[i] != b.chVflip[i])
            return false;
    return true;
}

bool sameSettings(const Settings& a, const Settings& b) {
    return a.width == b.width && a.height == b.height && a.fps == b.fps && a.duration == b.duration &&
           a.start == b.start && a.crf == b.crf && a.preset == b.preset && a.codec == b.codec &&
           a.container == b.container && a.ss == b.ss && a.outdir == b.outdir &&
           a.bitrate == b.bitrate && a.color == b.color && a.blur == b.blur && a.shutter == b.shutter &&
           a.loop == b.loop && a.loopMode == b.loopMode && a.loopFade == b.loopFade && a.timemap == b.timemap && a.speed == b.speed && a.audioFollow == b.audioFollow && a.audio == b.audio && a.fadein == b.fadein && a.fadeout == b.fadeout &&
           sameChannels(a, b) && formatMouseKeys(a.mouseKeys) == formatMouseKeys(b.mouseKeys) && formatKeyPresses(a.keys) == formatKeyPresses(b.keys) && a.date == b.date && a.variants == b.variants && a.gpu == b.gpu && a.endaction == b.endaction && a.depth == b.depth && a.name == b.name && a.exists == b.exists &&
           a.encoder == b.encoder && a.pbo == b.pbo && a.chunk == b.chunk && a.warmup == b.warmup && a.vr == b.vr && a.vrIpd == b.vrIpd && a.paramSpecs == b.paramSpecs && a.post == b.post && a.lut == b.lut && a.alpha == b.alpha && a.hdr == b.hdr && a.hdrPeak == b.hdrPeak &&
           a.hdrFall == b.hdrFall && a.hdrWhite == b.hdrWhite && a.interp == b.interp && a.scale == b.scale && a.scaleFilter == b.scaleFilter &&
           a.loudnorm == b.loudnorm && a.title == b.title && a.credit == b.credit && a.titleDur == b.titleDur && a.metadata == b.metadata && a.report == b.report && a.samples == b.samples && a.precheck == b.precheck && a.target == b.target && a.targetSizeMB == b.targetSizeMB && a.crop == b.crop && a.masterCache == b.masterCache && a.cacheMaxGB == b.cacheMaxGB && a.embed == b.embed && a.animPreview == b.animPreview && a.autoCredit == b.autoCredit && a.stillFormat == b.stillFormat && a.tile == b.tile && a.dpi == b.dpi;
}

// Valeur complete d'une cle qui garde ses espaces (chemin, modele de nom) : le commentaire `      ; texte` que writeIni peut
// ajouter derriere est retire (un « ; » en tete ou precede de deux espaces).
static std::string withoutIniComment(const std::string& raw) {
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] != ';') continue;
        const bool lead = raw.find_first_not_of(" \t") == i;
        const bool spaced = i >= 2 && (raw[i - 1] == ' ' || raw[i - 1] == '\t') && (raw[i - 2] == ' ' || raw[i - 2] == '\t');
        if (lead || spaced) return trim(raw.substr(0, i));
    }
    return trim(raw);
}

static void applyKV(Settings& s, const std::string& kIn, const std::string& vIn) {
    {  // parametres animes : « // key vitesse=0:1.0 ; 4:3.0 » (nom sensible a la casse) et --param=vitesse=2.5
        const std::string rk = trim(kIn);
        if (rk.size() > 4 && lower(rk.substr(0, 4)) == "key ") {
            s.paramSpecs.push_back(trim(rk.substr(4)) + "=" + trim(vIn));
            return;
        }
        if (lower(rk) == "param") {
            const std::string t = trim(vIn);
            if (t.find('=') != std::string::npos) s.paramSpecs.push_back(t);
            return;
        }
    }
    std::string k = lower(trim(kIn)), v = trim(vIn);
    if (k == "outdir" || k == "audio" || k == "name" || k == "variant" || k == "mouse" || k == "key" || k == "lut" || k == "title" || k == "credit" || (k.size() == 9 && k.compare(0, 8, "ichannel") == 0)) v = withoutIniComment(vIn);
    if (k == "variant") {  // « nom:cle=valeur|cle=valeur » : une ligne par variante
        if (!v.empty()) s.variants.push_back(v);
        return;
    }
    if (k == "mouse") {  // « x,y[,down] » ou « t:x,y[,down] ; t:x,y[,down] » : fractions de la taille, origine en bas a gauche
        if (lower(v) == "off") s.mouseKeys.clear();
        else {
            std::vector<MouseKey> keys;
            if (parseMouseKeys(v, keys)) s.mouseKeys = keys;
        }
        return;
    }
    if (k == "key") {  // « touche:debut-fin ; ... » : plusieurs lignes s'ajoutent, `off` efface
        if (lower(v) == "off") s.keys.clear();
        else parseKeyPresses(v, s.keys);
        return;
    }
    if (k == "timemap") { s.timemap = lower(v) == "off" ? "" : v; return; }  // « 0:0;4:2 » : sans espace dans un commentaire de shader
    if (k == "title") { s.title = W(v); return; }    // texte incruste (peut contenir des espaces)
    if (k == "credit") { s.credit = W(v); return; }
    if (k == "lut") { s.lut = lower(v) == "off" ? L"" : W(v); return; }  // chemin du fichier .cube (peut contenir des espaces)
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
        else if (k == "audiofollow") s.audioFollow = !(v == "0" || lower(v) == "off" || lower(v) == "false" || lower(v) == "no");
        else if (k == "loopmode") s.loopMode = (lower(v) == "crossfade" || lower(v) == "pingpong") ? lower(v) : "none";
        else if (k == "loopfade") s.loopFade = std::max(0.05, std::min(600.0, std::stod(v)));
        else if (k == "speed") s.speed = std::stod(v) == 0 ? 1.0 : std::max(-100.0, std::min(100.0, std::stod(v)));
        else if (k == "fadein") s.fadein = std::stod(v);
        else if (k == "fadeout") s.fadeout = std::stod(v);
        else if (k == "pbo") s.pbo = std::stoi(v);
        else if (k == "alpha") s.alpha = !(v == "0" || lower(v) == "off" || lower(v) == "false" || lower(v) == "no");
        else if (k == "hdr") {
            const std::string c = lower(v);
            s.hdr = (c == "pq" || c == "hdr10") ? "pq" : c == "hlg" ? "hlg" : "off";
        }
        else if (k == "hdrpeak") s.hdrPeak = std::max(100, std::min(10000, std::stoi(v)));
        else if (k == "hdrfall") s.hdrFall = std::max(10, std::min(10000, std::stoi(v)));
        else if (k == "hdrwhite") s.hdrWhite = std::max(20, std::min(1000, std::stoi(v)));
        else if (k == "interp") s.interp = lower(v) == "off" ? 0.0 : std::max(0.0, std::min(480.0, std::stod(v)));
        else if (k == "scale") s.scale = lower(v) == "off" ? "" : lower(v);
        else if (k == "scalefilter") {
            const std::string c = lower(v);
            s.scaleFilter = (c == "bicubic" || c == "bilinear" || c == "neighbor" || c == "area") ? c : c == "nearest" ? "neighbor" : "lanczos";
        }
        else if (k == "loudnorm") s.loudnorm = lower(v) == "off" ? 0.0 : std::max(-70.0, std::min(-5.0, std::stod(v)));
        else if (k == "titledur") s.titleDur = std::max(0.5, std::min(60.0, std::stod(v)));
        else if (k == "post") s.post = lower(v) == "off" ? "" : lower(v);
        else if (k == "vr") s.vr = (lower(v) == "off" || lower(v) == "mono" || lower(v) == "stereo" || lower(v) == "sbs" || lower(v) == "anaglyph" || lower(v) == "fulldome") ? lower(v) : "auto";
        else if (k == "vripd") s.vrIpd = std::max(0.0, std::min(10.0, std::stod(v)));
        else if (k == "warmup") s.warmup = std::max(0, std::min(3600, std::stoi(v)));
        else if (k == "metadata") s.metadata = !(v == "0" || lower(v) == "off" || lower(v) == "false" || lower(v) == "no");
        else if (k.size() > 9 && k.compare(0, 8, "ichannel") == 0 && k[8] >= '0' && k[8] <= '3' && k[9] == '.') {
            const int i = k[8] - '0';
            const std::string sub = k.substr(10), val = lower(v);
            if (sub == "filter") s.chFilter[i] = (val == "nearest" || val == "linear") ? val : "mipmap";
            else if (sub == "wrap") s.chWrap[i] = val == "clamp" ? val : "repeat";
            else if (sub == "vflip") s.chVflip[i] = !(val == "0" || val == "false" || val == "off" || val == "no");
        }
        else if (k.size() > 8 && k.compare(0, 6, "buffer") == 0 && k[6] >= 'a' && k[6] <= 'd' && k[7] == '.') {  // bufferA.scale / .size
            const int i = k[6] - 'a';
            const std::string sub = k.substr(8), val = lower(v);
            int w = 0, h = 0;
            if (sub == "scale") {
                const double sc = std::stod(val);
                s.bufScale[i] = sc > 0 ? std::min(sc, 16.0) : 0;
                s.bufW[i] = s.bufH[i] = 0;
            } else if (sub == "size") {
                const int got = sscanf(val.c_str(), "%dx%d", &w, &h);
                if (got >= 1 && w > 0 && (got == 1 || h > 0)) {
                    s.bufW[i] = std::min(w, 16384);
                    s.bufH[i] = std::min(got == 2 ? h : w, 16384);
                    s.bufScale[i] = 0;
                } else if (val == "off") {
                    s.bufW[i] = s.bufH[i] = 0;
                }
            }
        }
        else if (k == "gpu") s.gpu = (lower(v) == "high" || lower(v) == "low") ? lower(v) : "default";
        else if (k == "endaction") s.endaction = (lower(v) == "folder" || lower(v) == "sleep") ? lower(v) : "none";
        else if (k == "date") s.date = lower(v) == "off" ? "" : v;
        else if (k == "chunk") s.chunk = std::stod(v);
        else if (k == "samples") s.samples = std::max(1, std::min(100000, std::stoi(v)));
        else if (k == "target") s.target = lower(v) == "off" ? "" : lower(v);
        else if (k == "targetsize") {
            double mb = 0;
            s.targetSizeMB = lower(v) == "off" ? 0.0 : (parseSizeMB(v, mb) ? mb : s.targetSizeMB);
        }
        else if (k == "crop") s.crop = lower(v) == "off" ? "" : v;
        else if (k == "mastercache") s.masterCache = !(v == "0" || lower(v) == "off" || lower(v) == "false" || lower(v) == "no");
        else if (k == "cachemax") s.cacheMaxGB = std::max(0.1, std::min(10000.0, std::stod(v)));
        else if (k == "embed") s.embed = !(v == "0" || lower(v) == "off" || lower(v) == "false" || lower(v) == "no");
        else if (k == "animpreview") s.animPreview = (lower(v) == "gif" || lower(v) == "webp") ? lower(v) : "off";
        else if (k == "autocredit") s.autoCredit = !(v == "0" || lower(v) == "off" || lower(v) == "false" || lower(v) == "no");
        else if (k == "precheck") s.precheck = !(v == "0" || lower(v) == "off" || lower(v) == "false" || lower(v) == "no");
        else if (k == "stillformat") s.stillFormat = (lower(v) == "tiff" || lower(v) == "tif") ? "tiff" : "png";
        else if (k == "tile") s.tile = std::stoi(v) <= 0 ? 0 : std::max(64, std::min(16384, std::stoi(v)));
        else if (k == "printdpi") s.dpi = std::max(0, std::min(10000, std::stoi(v)));
        else if (k == "report") s.report = lower(v) == "json" ? "json" : (lower(v) == "text" || lower(v) == "txt") ? "text" : "off";
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
                                        "blur", "shutter", "loop", "loopmode", "loopfade", "timemap", "speed", "audiofollow", "samples", "precheck", "target", "targetsize", "crop", "mastercache", "cachemax", "embed", "animpreview", "autocredit", "stillformat", "tile", "printdpi", "fadein", "fadeout", "pbo", "warmup", "vr", "vripd", "param", "post", "lut", "alpha", "hdr", "hdrpeak", "hdrfall", "hdrwhite", "interp",
                                        "report", "scale", "scalefilter", "loudnorm", "title", "credit", "titledur", "chunk", "encoder", "color", "container",
                                        "codec", "depth", "exists", "name", "outdir", "audio", "metadata", "mouse", "key", "date", "endaction", "variant", "gpu"};
    for (const char* k : kKeys)
        if (key == k) return true;
    // ichannel0 a ichannel3, avec .filter .wrap .vflip
    if (key.size() >= 9 && key.compare(0, 8, "ichannel") == 0 && key[8] >= '0' && key[8] <= '3')
        return key.size() == 9 || key.substr(9) == ".filter" || key.substr(9) == ".wrap" || key.substr(9) == ".vflip";
    // buffera a bufferd, avec .scale .size
    if (key.size() >= 8 && key.compare(0, 6, "buffer") == 0 && key[6] >= 'a' && key[6] <= 'd') return key.substr(7) == ".scale" || key.substr(7) == ".size";
    return false;
}

void applySetting(Settings& s, const std::string& key, const std::string& value) { applyKV(s, key, value); }

std::wstring shaderOverridesOf(const std::wstring& shaderPath) {
    std::string out;
    for (const Override& o : shaderOverrides(shaderPath)) out += (out.empty() ? "" : ", ") + o.key + "=" + o.value;
    return W(out);
}

bool isOverridden(const std::vector<Override>& list, const char* key) {
    for (const Override& o : list)
        if (o.key == key) return true;
    return false;
}

void applyOverrides(Settings& s, const std::vector<Override>& list) {
    for (const Override& o : list) applyKV(s, o.key, o.value);
}

std::vector<Override> shaderOverrides(const std::wstring& shaderPath) {
    std::vector<Override> list;
    std::string text;
    if (!readFile(shaderPath, text) || text.size() >= 2 && (uint8_t)text[0] == 0xFF && (uint8_t)text[1] == 0xFE) return list;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.compare(0, 2, "//") != 0) continue;
        const std::string body = trim(line.substr(2));
        const size_t eq = body.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(body.substr(0, eq)));
        if (!isSettingKey(key)) continue;
        std::string val = trim(body.substr(eq + 1));
        if (key != "outdir" && key != "audio" && key != "name" && key != "mouse" && key != "key" && key != "lut" && key != "title" && key != "credit" && key.compare(0, 8, "ichannel") != 0) val = val.substr(0, val.find_first_of(" \t"));
        if (val.empty()) continue;
        list.push_back({key, val});
    }
    return list;
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
    fputs(("; name: " + i18n::tr8("ini.name") + "\r\n; iChannel0-3: " + i18n::tr8("ini.ichannel") + "\r\n; variant: " + i18n::tr8("ini.variant") + "\r\n; mouse: " + i18n::tr8("ini.mouse") + "\r\n; key: " + i18n::tr8("ini.key") + "\r\n; lut: " + i18n::tr8("ini.lut") + "\r\n; alpha, hdr, interp, scale, loudnorm, title, credit: " + i18n::tr8("ini.output_more") + "\r\n").c_str(), f);
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
    if (s.loopMode != "none") {
        put("loopmode", s.loopMode, "ini.loopmode");
        put("loopfade", num(s.loopFade), "ini.loopfade");
    }
    if (!s.timemap.empty()) put("timemap", s.timemap);  // pas de commentaire : la valeur garde sa forme exacte
    if (s.speed != 1.0) put("speed", num(s.speed), "ini.speed");
    if (s.audioFollow) put("audiofollow", "1", "ini.audiofollow");
    put("audio", U8(s.audio));
    put("fadein", num(s.fadein), "ini.fadein");
    put("fadeout", num(s.fadeout), "ini.fadeout");
    put("encoder", s.encoder, "ini.encoder");
    put("pbo", std::to_string(s.pbo), "ini.pbo");
    put("depth", std::to_string(s.depth), "ini.depth");
    put("name", U8(s.name));  // pas de commentaire : la valeur garde ses espaces
    put("exists", s.exists, "ini.exists");
    for (int i = 0; i < 4; i++) {
        if (s.channel[i].empty()) continue;
        const std::string key = "iChannel" + std::to_string(i);
        put(key, U8(s.channel[i]));
        if (!s.chFilter[i].empty()) put(key + ".filter", s.chFilter[i]);
        if (!s.chWrap[i].empty()) put(key + ".wrap", s.chWrap[i]);
        put(key + ".vflip", s.chVflip[i] ? "1" : "0");
    }
    for (int i = 0; i < 4; i++) {
        const std::string key = std::string("buffer") + (char)('A' + i);
        if (s.bufScale[i] > 0) put(key + ".scale", num(s.bufScale[i]));
        else if (s.bufW[i] > 0) put(key + ".size", std::to_string(s.bufW[i]) + "x" + std::to_string(s.bufH[i]));
    }
    if (!s.mouseKeys.empty()) put("mouse", formatMouseKeys(s.mouseKeys));  // pas de commentaire : la valeur garde ses espaces
    if (!s.keys.empty()) put("key", formatKeyPresses(s.keys));  // pas de commentaire : la valeur garde ses espaces
    if (!s.date.empty()) put("date", s.date, "ini.date");
    for (const auto& v : s.variants) put("variant", v);
    put("gpu", s.gpu, "ini.gpu");
    put("endaction", s.endaction, "ini.endaction");
    put("chunk", num(s.chunk), "ini.chunk");
    if (s.report != "off") put("report", s.report, "ini.report");
    if (s.samples > 1) put("samples", std::to_string(s.samples), "ini.samples");
    if (s.precheck) put("precheck", "1", "ini.precheck");
    if (!s.target.empty()) put("target", s.target, "ini.target");
    if (s.targetSizeMB > 0) put("targetsize", num(s.targetSizeMB) + "MB", "ini.targetsize");
    if (!s.crop.empty()) put("crop", s.crop, "ini.crop");
    if (s.masterCache) {
        put("mastercache", "1", "ini.mastercache");
        put("cachemax", num(s.cacheMaxGB), "ini.cachemax");
    }
    if (s.embed) put("embed", "1", "ini.embed");
    if (s.animPreview != "off") put("animpreview", s.animPreview, "ini.animpreview");
    if (s.autoCredit) put("autocredit", "1", "ini.autocredit");
    if (s.stillFormat != "png") put("stillformat", s.stillFormat, "ini.stillformat");
    if (s.tile > 0) put("tile", std::to_string(s.tile), "ini.tile");
    if (s.dpi > 0) put("printdpi", std::to_string(s.dpi), "ini.dpi");
    put("warmup", std::to_string(s.warmup), "ini.warmup");
    // sortie : transparence, HDR, interpolation, mise a l'echelle, son, texte incruste (seulement ce qui est regle)
    if (s.alpha) put("alpha", "1");
    if (s.hdr != "off") {
        put("hdr", s.hdr);
        put("hdrpeak", std::to_string(s.hdrPeak));
        put("hdrfall", std::to_string(s.hdrFall));
        put("hdrwhite", std::to_string(s.hdrWhite));
    }
    if (s.interp > 0) put("interp", num(s.interp));
    if (!s.scale.empty()) {
        put("scale", s.scale);
        put("scalefilter", s.scaleFilter);
    }
    if (s.loudnorm != 0) put("loudnorm", num(s.loudnorm));
    if (!s.title.empty()) put("title", U8(s.title));    // pas de commentaire : la valeur garde ses espaces
    if (!s.credit.empty()) put("credit", U8(s.credit));
    if (!s.title.empty() || !s.credit.empty()) put("titledur", num(s.titleDur));
    if (!s.post.empty()) put("post", s.post, "ini.post");
    if (!s.lut.empty()) put("lut", U8(s.lut));  // pas de commentaire : la valeur garde ses espaces
    put("vr", s.vr, "ini.vr");
    put("vripd", num(s.vrIpd), "ini.vripd");
    put("metadata", s.metadata ? "1" : "0", "ini.metadata");
    fclose(f);
}
