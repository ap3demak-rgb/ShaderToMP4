// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "publish.h"
#include "app_info.h"
#include "formats.h"
#include <cstring>
#include "i18n.h"
#include "json.h"
#include "process.h"
#include "util.h"
#include "platform/platform.h"
#include <ctime>

// --- Cibles ------------------------------------------------------------------------------------------------------------------------------------
namespace {

// Limites indicatives des plates-formes, tenues a la main : elles changent, presets/targets.ini les corrige sans recompiler.
const struct {
    const char* id;
    const char* label;
    int w, h;
    double fps;
    const char* codec;
    double maxSeconds, maxMB;
} kTargets[] = {
    {"youtube-1080p", "YouTube 1080p", 1920, 1080, 60, "h264", 0, 0},
    {"youtube-shorts", "YouTube Shorts (vertical)", 1080, 1920, 30, "h264", 180, 0},
    {"instagram-reels", "Instagram Reels (vertical)", 1080, 1920, 30, "h264", 180, 0},
    {"tiktok", "TikTok (vertical)", 1080, 1920, 30, "h264", 600, 287},
    {"square-social", "Square 1:1 (social feeds)", 1080, 1080, 30, "h264", 60, 0},
    {"x-twitter", "X / Twitter", 1920, 1080, 30, "h264", 140, 512},
    {"discord-8mb", "Messaging, 8 MB", 1280, 720, 30, "h264", 0, 8},
    {"discord-25mb", "Messaging, 25 MB", 1280, 720, 30, "h264", 0, 25},
};

std::string lowerTrim(const std::string& s) { return lower(trim(s)); }

}  // namespace

bool parseTargets(const std::string& text, std::vector<Target>& out) {
    Target cur;
    bool open = false;
    auto flush = [&] {
        if (open && !cur.id.empty()) out.push_back(cur);
        cur = Target();
        open = false;
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            flush();
            cur.id = lowerTrim(line.substr(1, line.size() - 2));
            open = true;
            continue;
        }
        const size_t eq = line.find('=');
        if (!open || eq == std::string::npos) continue;
        const std::string k = lowerTrim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "label") cur.label = W(v);
        else if (k == "width") cur.width = atoi(v.c_str());
        else if (k == "height") cur.height = atoi(v.c_str());
        else if (k == "fps") cur.fps = atof(v.c_str());
        else if (k == "codec") cur.codec = lowerTrim(v);
        else if (k == "container") cur.container = lowerTrim(v);
        else if (k == "maxduration") cur.maxSeconds = atof(v.c_str());
        else if (k == "maxsize") cur.maxMB = atof(v.c_str());
    }
    flush();
    return true;
}

std::vector<Target> publishTargets(const std::wstring& exeDir) {
    std::vector<Target> all;
    for (const auto& b : kTargets) {
        Target t;
        t.id = b.id;
        t.label = W(b.label);
        t.width = b.w;
        t.height = b.h;
        t.fps = b.fps;
        t.codec = b.codec;
        t.container = "mp4";
        t.maxSeconds = b.maxSeconds;
        t.maxMB = b.maxMB;
        all.push_back(t);
    }
    std::string text;
    if (readFile(exeDir + L"presets/targets.ini", text)) {
        std::vector<Target> user;
        parseTargets(text, user);
        for (Target& u : user) {
            if (u.label.empty()) u.label = W(u.id);
            if (u.container.empty()) u.container = "mp4";
            bool replaced = false;
            for (Target& t : all)
                if (t.id == u.id) {
                    t = u;
                    replaced = true;
                }
            if (!replaced) all.push_back(u);
        }
    }
    return all;
}

const Target* findTarget(const std::vector<Target>& all, const std::string& id) {
    const std::string k = lowerTrim(id);
    for (const Target& t : all)
        if (t.id == k) return &t;
    return nullptr;
}

void applyTarget(Settings& s, const Target& t, const std::vector<std::string>& skip) {
    auto skipped = [&](const char* key) { return std::find(skip.begin(), skip.end(), key) != skip.end(); };
    if (t.width > 0 && !skipped("width")) s.width = t.width;
    if (t.height > 0 && !skipped("height")) s.height = t.height;
    if (t.fps > 0 && !skipped("fps")) s.fps = t.fps;
    if (!t.codec.empty() && !skipped("codec")) s.codec = normalizeCodec(t.codec);
    if (!t.container.empty() && !skipped("container")) s.container = normalizeContainer(t.container);
    if (t.maxMB > 0 && !skipped("targetsize") && s.targetSizeMB <= 0) s.targetSizeMB = t.maxMB;
}

std::vector<std::wstring> checkTarget(const Target& t, const Settings& s, double seconds, int outW, int outH) {
    std::vector<std::wstring> w;
    if (t.maxSeconds > 0 && seconds > t.maxSeconds + 0.01) w.push_back(TXF("log.target_duration", t.label.c_str(), seconds, t.maxSeconds));
    if ((t.width > 0 && outW != t.width) || (t.height > 0 && outH != t.height)) w.push_back(TXF("log.target_size", t.label.c_str(), outW, outH, t.width, t.height));
    if ((!t.container.empty() && normalizeContainer(t.container) != s.container) || (!t.codec.empty() && normalizeCodec(t.codec) != s.codec))
        w.push_back(TXF("log.target_format", t.label.c_str(), W(s.codec).c_str(), W(s.container).c_str()));
    if (t.maxMB > 0 && s.targetSizeMB > t.maxMB + 1e-9) w.push_back(TXF("log.target_weight", t.label.c_str(), s.targetSizeMB, t.maxMB));
    return w;
}

bool cropSize(const std::string& spec, int w, int h, int& cw, int& ch) {
    double a = 0, b = 0;
    cw = w;
    ch = h;
    if (trim(spec).empty()) return true;
    if (sscanf(trim(spec).c_str(), "%lf:%lf", &a, &b) != 2 || a <= 0 || b <= 0) return false;
    const double ratio = a / b;
    if ((double)w / h > ratio) {
        cw = std::max(16, (int)std::floor(h * ratio) & ~1);
        ch = h;
    } else {
        cw = w;
        ch = std::max(16, (int)std::floor(w / ratio) & ~1);
    }
    cw = std::min(cw, w);
    ch = std::min(ch, h);
    return true;
}

// --- Taille cible ---------------------------------------------------------------------------------------------------------------------------
bool parseSizeMB(const std::string& textIn, double& mb) {
    const std::string text = lowerTrim(textIn);
    if (text.empty()) return false;
    char* end = nullptr;
    const double v = strtod(text.c_str(), &end);
    if (end == text.c_str() || v <= 0 || !std::isfinite(v)) return false;
    const std::string unit = trim(std::string(end));
    if (unit.empty() || unit == "mb" || unit == "m" || unit == "mo") mb = v;
    else if (unit == "kb" || unit == "k" || unit == "ko") mb = v / 1000.0;
    else if (unit == "gb" || unit == "g" || unit == "go") mb = v * 1000.0;
    else return false;
    return true;
}

bool planBitrate(double mb, double seconds, int audioKbps, BitratePlan& out) {
    out = BitratePlan();
    if (mb <= 0 || seconds <= 0) return false;
    const double bits = mb * 1e6 * 0.96 * 8.0;  // 4 % pour le conteneur
    const double video = bits / seconds / 1000.0 - audioKbps;
    if (video < 20) return false;
    out.videoKbps = (int)std::floor(video);
    out.audioKbps = audioKbps;
    out.seconds = seconds;
    return true;
}

int correctedBitrate(int kbps, uint64_t actualBytes, uint64_t targetBytes) {
    if (actualBytes == 0) return kbps;
    const double ratio = (double)targetBytes * 0.985 / (double)actualBytes;
    return std::max(20, (int)std::floor(kbps * ratio * 0.97));
}

// --- Licence ---------------------------------------------------------------------------------------------------------------------------------
LicenseInfo detectLicense(const std::string& code) {
    LicenseInfo L;
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= code.size() && lines.size() < 120) {
        size_t e = code.find('\n', pos);
        if (e == std::string::npos) e = code.size();
        lines.push_back(code.substr(pos, e - pos));
        pos = e + 1;
    }
    bool shadertoy = false;
    std::string comments;  // le texte des commentaires, en minuscules
    bool inBlock = false;
    for (std::string line : lines) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.compare(0, 12, "// Shadertoy") == 0 && !shadertoy) {  // en-tete de l'import : « // Shadertoy: titre - auteur - adresse »
            shadertoy = true;
            std::string rest = trim(t.substr(12));
            if (!rest.empty() && rest[0] == ':') rest = trim(rest.substr(1));
            std::vector<std::string> parts;
            size_t p = 0;
            while (p <= rest.size()) {
                size_t d = rest.find(" - ", p);
                if (d == std::string::npos) d = rest.size();
                parts.push_back(trim(rest.substr(p, d - p)));
                p = d + 3;
            }
            if (!parts.empty() && parts[0].compare(0, 4, "http") != 0) L.title = W(parts[0]);
            if (parts.size() >= 2 && parts[1].compare(0, 4, "http") != 0) L.author = W(parts[1]);
            continue;
        }
        std::string c;
        if (inBlock) {
            const size_t e = t.find("*/");
            c = e == std::string::npos ? t : t.substr(0, e);
            if (e != std::string::npos) inBlock = false;
        } else if (t.compare(0, 2, "//") == 0) {
            c = t.substr(2);
        } else if (t.compare(0, 2, "/*") == 0) {
            const size_t e = t.find("*/", 2);
            c = e == std::string::npos ? t.substr(2) : t.substr(2, e - 2);
            if (e == std::string::npos) inBlock = true;
        } else if (!t.empty() && !inBlock) {
            continue;
        }
        comments += lower(c) + "\n";
        if (L.author.empty()) {  // « Copyright (c) 2019 Nom », « Author: Nom »
            const std::string lc = lower(c);
            size_t k = lc.find("author:");
            if (k != std::string::npos) L.author = W(trim(c.substr(k + 7)));
            else if ((k = lc.find("copyright")) != std::string::npos) {
                std::string rest = trim(c.substr(k + 9));
                for (const char* skipWord : {"(c)", "\xC2\xA9", ":"})
                    if (rest.compare(0, strlen(skipWord), skipWord) == 0) rest = trim(rest.substr(strlen(skipWord)));
                while (!rest.empty() && (isdigit((unsigned char)rest[0]) || rest[0] == '-' || rest[0] == ',' || rest[0] == ' ')) rest.erase(0, 1);
                if (!rest.empty() && rest.size() < 80) L.author = W(rest);
            }
        }
    }
    auto has = [&](const char* needle) { return comments.find(needle) != std::string::npos; };
    auto ccVersion = [&]() -> std::string {
        for (const char* v : {"4.0", "3.0", "2.5", "2.0", "1.0"})
            if (has(v)) return v;
        return "3.0";
    };
    if (has("by-nc-sa") || has("by nc sa") || has("attribution-noncommercial-sharealike")) {
        L.id = "CC-BY-NC-SA-" + ccVersion();
        L.nonCommercial = true;
    } else if (has("by-nc-nd") || has("attribution-noncommercial-noderivs")) {
        L.id = "CC-BY-NC-ND-" + ccVersion();
        L.nonCommercial = true;
    } else if (has("by-nc") || has("attribution-noncommercial")) {
        L.id = "CC-BY-NC-" + ccVersion();
        L.nonCommercial = true;
    } else if (has("licenses/by-sa") || has("cc by-sa") || has("cc-by-sa")) {
        L.id = "CC-BY-SA-" + ccVersion();
    } else if (has("licenses/by-nd") || has("cc by-nd") || has("cc-by-nd")) {
        L.id = "CC-BY-ND-" + ccVersion();
    } else if (has("licenses/by/") || has("cc by ") || has("cc-by ") || has("cc by\n") || has("cc-by\n") || has("cc by 4") || has("cc by 3")) {
        L.id = "CC-BY-" + ccVersion();
    } else if (has("cc0") || has("public domain") || has("unlicense")) {
        L.id = has("cc0") ? "CC0-1.0" : "Public-Domain";
    } else if (has("mit license") || has("spdx-license-identifier: mit")) {
        L.id = "MIT";
    } else if (has("apache license") || has("apache-2.0")) {
        L.id = "Apache-2.0";
    } else if (has("gnu general public license") || has("spdx-license-identifier: gpl")) {
        L.id = "GPL";
    } else if (has("bsd license") || has("spdx-license-identifier: bsd")) {
        L.id = "BSD";
    } else if (has("non-commercial") || has("noncommercial") || has("not for commercial") || has("non commercial")) {
        L.id = "Custom-NC";
        L.nonCommercial = true;
    } else if (shadertoy) {  // l'import ne copie pas la licence : Shadertoy applique CC BY-NC-SA 3.0 quand l'auteur n'en dit pas d'autre
        L.id = "CC-BY-NC-SA-3.0";
        L.nonCommercial = true;
        L.assumed = true;
    }
    L.found = !L.id.empty() || !L.author.empty() || !L.title.empty();
    return L;
}

std::wstring licenseText(const LicenseInfo& l) {
    std::string id = l.id;
    if (id.compare(0, 6, "CC-BY-") == 0) {  // « CC-BY-NC-SA-3.0 » -> « CC BY-NC-SA 3.0 »
        const size_t last = id.rfind('-');
        std::string v = id.substr(last + 1);
        if (!v.empty() && isdigit((unsigned char)v[0])) id = "CC BY" + id.substr(5, last - 5) + " " + v;
    }
    return W(id);
}

// --- Source embarquee ------------------------------------------------------------------------------------------------------------------------
std::string base64Encode(const std::string& in) {
    static const char* tab = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
        out += tab[v >> 18];
        out += tab[(v >> 12) & 63];
        out += tab[(v >> 6) & 63];
        out += tab[v & 63];
    }
    if (i + 1 == in.size()) {
        const uint32_t v = (uint8_t)in[i] << 16;
        out += tab[v >> 18];
        out += tab[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        const uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8;
        out += tab[v >> 18];
        out += tab[(v >> 12) & 63];
        out += tab[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

bool base64Decode(const std::string& text, std::string& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        else return false;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += (char)((acc >> bits) & 0xFF);
        }
    }
    return true;
}

namespace {

std::string jsonEscape(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += (char)c;
                }
        }
    }
    return o + "\"";
}

}  // namespace

std::string bundleToJson(const SourceBundle& b) {
    std::string o = "{\"v\":1,\"app\":" + jsonEscape(b.appVersion) + ",\"shader\":" + jsonEscape(b.shaderName) + ",\"files\":[";
    for (size_t i = 0; i < b.files.size(); i++) o += std::string(i ? "," : "") + "{\"name\":" + jsonEscape(b.files[i].first) + ",\"text\":" + jsonEscape(b.files[i].second) + "}";
    return o + "],\"settings\":" + jsonEscape(b.settingsIni) + "}";
}

bool bundleFromJson(const std::string& json, SourceBundle& b) {
    JsonValue v;
    std::string err;
    if (!parseJson(json, v, err) || v.kind != JsonValue::Object) return false;
    b = SourceBundle();
    b.appVersion = v.str("app");
    b.shaderName = v.str("shader");
    b.settingsIni = v.str("settings");
    const JsonValue* files = v.get("files");
    if (!files || files->kind != JsonValue::Array) return false;
    for (const JsonValue& f : files->items) b.files.push_back({f.str("name"), f.str("text")});
    return !b.shaderName.empty() && !b.files.empty();
}

static std::string ffmetaEscape(const std::string& value) {
    std::string esc;
    for (char c : value) {
        if (c == '=' || c == ';' || c == '#' || c == '\\' || c == '\n') esc += '\\';
        esc += c;
    }
    return esc;
}

bool writeBundleMetadata(const std::wstring& path, const SourceBundle& b, const std::vector<std::pair<std::string, std::string>>& extra) {
    std::string text = ";FFMETADATA1\n";
    for (const auto& kv : extra) text += kv.first + "=" + ffmetaEscape(kv.second) + "\n";
    text += "shadertomp4=" + ffmetaEscape(base64Encode(bundleToJson(b))) + "\n";
    return writeFileAtomic(path, text);
}

bool readBundleFromVideo(const std::wstring& ffmpeg, const std::wstring& video, SourceBundle& b, std::wstring& err) {
    const std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -i \"" + video + L"\" -f ffmetadata -";
    auto proc = platform::Process::start(platform::splitCommand(cmd), false, true);
    if (!proc) {
        err = TX("error.ffmpeg_launch");
        return false;
    }
    std::string all;
    char buf[8192];
    size_t got;
    while ((got = proc->readOutput(buf, sizeof(buf))) > 0) all.append(buf, got);
    proc->wait();
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find('\n', pos);
        if (e == std::string::npos) e = all.size();
        std::string line = all.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos || lower(line.substr(0, eq)) != "shadertomp4") continue;
        std::string value, raw;
        for (size_t i = eq + 1; i < line.size(); i++) {
            if (line[i] == '\\' && i + 1 < line.size()) i++;
            raw += line[i];
        }
        std::string json;
        if (base64Decode(raw, json) && bundleFromJson(json, b)) return true;
    }
    err = TX("error.reproduce_none");
    return false;
}

SourceBundle makeBundle(const std::wstring& shaderPath, const std::string& settingsIni) {
    SourceBundle b;
    b.appVersion = APP_VER_STR;
    b.settingsIni = settingsIni;
    const size_t cut = shaderPath.find_last_of(L"\\/");
    const std::wstring dir = cut == std::wstring::npos ? L"" : shaderPath.substr(0, cut + 1);
    const std::wstring name = shaderPath.substr(cut == std::wstring::npos ? 0 : cut + 1);
    const size_t dot = name.find_last_of(L'.');
    const std::wstring stem = dot == std::wstring::npos ? name : name.substr(0, dot);
    b.shaderName = U8(name);
    size_t total = 0;
    std::vector<platform::DirEntry> entries;
    platform::listDir(dir.empty() ? L"." : dir, entries);
    std::sort(entries.begin(), entries.end(), [](const platform::DirEntry& a, const platform::DirEntry& c) { return a.name < c.name; });
    for (const auto& en : entries) {
        if (en.isDir || en.name.compare(0, stem.size(), stem) != 0) continue;
        const std::wstring rest = en.name.substr(stem.size());
        const std::wstring ext = dot == std::wstring::npos ? L"" : name.substr(dot);
        bool ok = en.name == name;
        for (const wchar_t* p : {L".common", L".bufferA", L".bufferB", L".bufferC", L".bufferD", L".cubemapA", L".sound", L".post", L".vert", L".shadow", L".compute"})
            if (rest == std::wstring(p) + ext) ok = true;
        if (!ok) continue;
        std::string text;
        if (!readFile(dir + en.name, text) || total + text.size() > 2u * 1024 * 1024) continue;
        total += text.size();
        b.files.push_back({U8(en.name), text});
    }
    return b;
}

// --- Cache -----------------------------------------------------------------------------------------------------------------------------------------
std::wstring cacheDir(const std::wstring& exeDir) { return exeDir + L"cache"; }

static bool endsWith(const std::wstring& s, const wchar_t* suffix) {
    const size_t n = wcslen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

uint64_t cacheSize(const std::wstring& exeDir) {
    uint64_t total = 0;
    std::vector<platform::DirEntry> entries;
    if (!platform::listDir(cacheDir(exeDir), entries)) return 0;
    for (const auto& en : entries) {
        uint64_t b = 0;
        if (!en.isDir && endsWith(en.name, L".mkv") && platform::fileSize(cacheDir(exeDir) + L"/" + en.name, b)) total += b;
    }
    return total;
}

uint64_t cachePurge(const std::wstring& exeDir) {
    const uint64_t freed = cacheSize(exeDir);
    const std::wstring dir = cacheDir(exeDir);
    std::vector<platform::DirEntry> entries;
    if (!platform::listDir(dir, entries)) return 0;
    for (const auto& en : entries) {
        const std::wstring p = dir + L"/" + en.name;
        if (en.isDir) {
            clearDir(p);
            platform::removeDir(p);
        } else {
            platform::removeFile(p);
        }
    }
    platform::removeDir(dir);
    return freed;
}

void cacheTouch(const std::wstring& master, const std::wstring& info) {
    const std::wstring card = master.substr(0, master.find_last_of(L'.')) + L".txt";
    writeFileAtomic(card, "used=" + std::to_string((long long)time(nullptr)) + "\n" + U8(info) + "\n");
}

void cacheTrim(const std::wstring& exeDir, uint64_t maxBytes, const std::wstring& keep) {
    struct Item {
        std::wstring path;
        uint64_t bytes;
        long long used;
    };
    std::vector<Item> items;
    uint64_t total = 0;
    const std::wstring dir = cacheDir(exeDir);
    std::vector<platform::DirEntry> entries;
    if (!platform::listDir(dir, entries)) return;
    for (const auto& en : entries) {
        if (en.isDir || !endsWith(en.name, L".mkv")) continue;
        Item it;
        it.path = dir + L"/" + en.name;
        if (!platform::fileSize(it.path, it.bytes)) continue;
        std::string card;
        long long used = 0;
        if (readFile(it.path.substr(0, it.path.find_last_of(L'.')) + L".txt", card)) sscanf(card.c_str(), "used=%lld", &used);
        it.used = used;
        total += it.bytes;
        items.push_back(it);
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.used < b.used; });
    for (const Item& it : items) {
        if (total <= maxBytes) break;
        if (it.path == keep) continue;
        platform::removeFile(it.path);
        platform::removeFile(it.path.substr(0, it.path.find_last_of(L'.')) + L".txt");
        total -= std::min(total, it.bytes);
    }
}

// --- Verification -------------------------------------------------------------------------------------------------------------------------------
bool verifyVideo(const std::wstring& ffmpeg, const std::wstring& video, VerifyResult& res, std::wstring& err) {
    res = VerifyResult();
    uint64_t bytes = 0;
    if (!platform::fileSize(video, bytes)) {
        err = TXF("error.verify_read", video.c_str());
        return false;
    }
    // duree et cadence annoncees
    std::string info;
    runProcess(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -i \"" + video + L"\"", [&](const std::wstring& line) { info += U8(line) + "\n"; });
    const size_t d = info.find("Duration: ");
    int h = 0, m = 0;
    double s = 0;
    if (d != std::string::npos && sscanf(info.c_str() + d + 10, "%d:%d:%lf", &h, &m, &s) == 3) res.seconds = h * 3600.0 + m * 60.0 + s;
    const size_t f = info.find(" fps");
    if (f != std::string::npos) {
        size_t b = f;
        while (b > 0 && (isdigit((unsigned char)info[b - 1]) || info[b - 1] == '.')) b--;
        res.fps = atof(info.c_str() + b);
    }
    if (res.seconds <= 0 || res.fps <= 0) {
        err = TXF("error.verify_read", video.c_str());
        return false;
    }
    // nombre d'images : decodage complet
    {
        auto proc = platform::Process::start(platform::splitCommand(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -i \"" + video + L"\" -map 0:v:0 -f null -"), false, true);
        if (!proc) {
            err = TX("error.ffmpeg_launch");
            return false;
        }
        std::string all;
        char buf[4096];
        size_t got;
        while ((got = proc->readOutput(buf, sizeof(buf))) > 0) all.append(buf, got);
        proc->wait();
        const size_t k = all.rfind("frame=");
        if (k != std::string::npos) res.frames = atoll(all.c_str() + k + 6);
    }
    platform::sha256File(video, res.sha256);
    res.checks.push_back(TXF("log.verify.duration", res.seconds));
    res.checks.push_back(TXF("log.verify.fps", res.fps));
    res.checks.push_back(TXF("log.verify.frames", res.frames));
    res.checks.push_back(TXF("log.verify.sha", W(res.sha256).c_str()));
    // rapport <video sans extension>.report.json
    const size_t dot = video.find_last_of(L'.');
    const std::wstring stem = dot == std::wstring::npos ? video : video.substr(0, dot);
    std::string rtext;
    JsonValue rep;
    std::string jerr;
    if (readFile(stem + L".report.json", rtext) && parseJson(rtext, rep, jerr)) {
        res.hadReport = true;
        const double rfps = rep.num("fps"), rsec = rep.num("media_seconds"), rbytes = rep.num("bytes");
        const std::string rsha = rep.str("sha256");
        if (rbytes > 0 && (uint64_t)rbytes != bytes) res.problems.push_back(TXF("log.verify.p_bytes", (long long)rbytes, (long long)bytes));
        if (!rsha.empty() && rsha != res.sha256) res.problems.push_back(TXF("log.verify.p_sha", W(rsha).c_str()));
        if (rfps > 0 && std::fabs(rfps - res.fps) > 0.02) res.problems.push_back(TXF("log.verify.p_fps", rfps, res.fps));
        if (rsec > 0 && std::fabs(rsec - res.seconds) > 1.0 / std::max(1.0, res.fps) + 0.06) res.problems.push_back(TXF("log.verify.p_duration", rsec, res.seconds));
        if (rfps > 0 && rsec > 0) {
            const long long expected = std::llround(rsec * rfps);
            if (std::llabs(expected - res.frames) > 1) res.problems.push_back(TXF("log.verify.p_frames", (long long)expected, (long long)res.frames));
        }
        res.checks.push_back(TX(res.problems.empty() ? "log.verify.report_ok" : "log.verify.report_bad"));
    } else {
        res.checks.push_back(TX("log.verify.no_report"));
    }
    return true;
}

// --- Apercu anime -----------------------------------------------------------------------------------------------------------------------------
bool makeAnimPreview(const std::wstring& ffmpeg, const std::wstring& video, const std::wstring& dest, bool webp, double videoSeconds) {
    const double start = std::max(0.0, (videoSeconds - 3.0) / 2.0);
    std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -ss " + wfmt(L"%.3f", start) + L" -t 3 -i \"" + video + L"\" -an -vf \"";
    if (webp) cmd += L"fps=15,scale=480:-2:flags=lanczos\" -c:v libwebp -quality 55 -loop 0";
    else cmd += L"fps=15,scale=480:-2:flags=lanczos,split[a][b];[a]palettegen=max_colors=96[p];[b][p]paletteuse=dither=bayer:bayer_scale=4\" -loop 0";
    cmd += L" \"" + dest + L"\"";
    return runProcess(cmd, nullptr) == 0;
}

bool verifyLines(const std::wstring& ffmpeg, const std::wstring& video, std::vector<std::wstring>& lines, bool& ok, std::wstring& err) {
    VerifyResult vr;
    if (!verifyVideo(ffmpeg, video, vr, err)) return false;
    lines = vr.checks;
    for (const std::wstring& p : vr.problems) lines.push_back(L"! " + p);
    ok = vr.problems.empty();
    if (ok) lines.push_back(TX("log.verify.ok"));
    return true;
}
