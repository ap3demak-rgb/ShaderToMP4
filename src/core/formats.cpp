// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "formats.h"
#include "i18n.h"
#include "process.h"
#include "util.h"
#include "platform/platform.h"
#include <set>

// ---------------------------------------------------------------------------
//  Tableaux des codecs et des conteneurs
// ---------------------------------------------------------------------------
static const FormatInfo kCodecs[] = {
    {"h264", L"H.264"},          {"h265", L"H.265"},          {"vp9", L"VP9"},
    {"av1", L"AV1"},             {"prores_proxy", L"ProRes Proxy"}, {"prores_lt", L"ProRes LT"},  // nom-technique
    {"prores_422", L"ProRes 422"}, {"prores_hq", L"ProRes HQ"}, {"prores_4444", L"ProRes 4444"},  // nom-technique
    {"dnxhr_sq", L"DNxHR SQ"},   {"dnxhr_hq", L"DNxHR HQ"},   {"dnxhr_hqx", L"DNxHR HQX"},  // nom-technique
    {"dnxhr_444", L"DNxHR 444"}};
static const FormatInfo kContainers[] = {{"mp4", L"MP4"},   {"mov", L"MOV"},   {"mkv", L"MKV"},
                                         {"webm", L"WebM"}, {"gif", L"GIF"},   {"webp", L"WebP"},
                                         {"png", L"PNG"},   {"tiff", L"TIFF"}, {"exr", L"EXR"}};

int codecCount() { return (int)(sizeof(kCodecs) / sizeof(kCodecs[0])); }
const FormatInfo& codecAt(int i) { return kCodecs[std::min(codecCount() - 1, std::max(0, i))]; }
int codecIndex(const std::string& id) {
    for (int i = 0; i < codecCount(); i++)
        if (id == kCodecs[i].id) return i;
    return 0;
}
int containerCount() { return (int)(sizeof(kContainers) / sizeof(kContainers[0])); }
const FormatInfo& containerAt(int i) { return kContainers[std::min(containerCount() - 1, std::max(0, i))]; }
int containerIndex(const std::string& id) {
    for (int i = 0; i < containerCount(); i++)
        if (id == kContainers[i].id) return i;
    return 0;
}

std::string normalizeCodec(const std::string& v) {
    const std::string c = lower(v);
    if (c == "hevc" || c == "x265") return "h265";
    if (c == "prores") return "prores_hq";
    if (c == "dnxhr") return "dnxhr_hq";
    for (int i = 0; i < codecCount(); i++)
        if (c == kCodecs[i].id) return c;
    return "h264";
}

std::string normalizeContainer(const std::string& v) {
    const std::string c = lower(v);
    for (int i = 0; i < containerCount(); i++)
        if (c == kContainers[i].id) return c;
    return "mp4";
}

OutKind outKind(const std::string& container) {
    if (container == "gif") return OutKind::Gif;
    if (container == "webp") return OutKind::Webp;
    if (container == "png" || container == "tiff" || container == "exr") return OutKind::Sequence;
    return OutKind::Video;
}

bool isProRes(const std::string& codec) { return codec.compare(0, 6, "prores") == 0; }
bool isDnxhr(const std::string& codec) { return codec.compare(0, 5, "dnxhr") == 0; }

bool codecContainerOk(const std::string& codec, const std::string& container) {
    if (outKind(container) != OutKind::Video) return true;  // images : le codec ne sert pas
    const bool open = codec == "vp9" || codec == "av1";
    const bool intermediate = isProRes(codec) || isDnxhr(codec);
    if (container == "webm") return open;
    if (container == "mov") return !open;
    if (container == "mp4") return !intermediate;
    return true;  // mkv : tout
}

bool outputSize(const Settings& s, int& w, int& h) {
    w = s.width;
    h = s.height;
    auto even = [](double v) { return std::max(16, std::min(16384, (int)(std::llround(v / 2.0) * 2))); };
    if (s.scale.empty()) return true;
    int tw = 0, th = 0;
    char x = 0;
    if (sscanf(s.scale.c_str(), "%d%c%d", &tw, &x, &th) == 3 && x == 'x' && tw >= 16 && th >= 16 && tw <= 16384 && th <= 16384) {
        w = even(tw);
        h = even(th);
        return true;
    }
    char* end = nullptr;
    const double f = strtod(s.scale.c_str(), &end);
    if (end != s.scale.c_str() && (*end == 0 || (*end == 'x' && end[1] == 0)) && f >= 0.1 && f <= 8.0) {
        w = even(s.width * f);
        h = even(s.height * f);
        return true;
    }
    return false;
}

bool alphaSupported(const std::string& codec, const std::string& container) {
    switch (outKind(container)) {
        case OutKind::Gif: return false;
        case OutKind::Webp:
        case OutKind::Sequence: return true;
        default: break;
    }
    return (codec == "prores_4444" && (container == "mov" || container == "mkv")) || (codec == "vp9" && (container == "webm" || container == "mkv"));
}

bool hdrSupported(const std::string& codec, const std::string& container) {
    return outKind(container) == OutKind::Video && (codec == "h265" || codec == "av1" || codec == "vp9") && codecContainerOk(codec, container);
}

// ---------------------------------------------------------------------------
//  Encodeurs presents dans ffmpeg
// ---------------------------------------------------------------------------
std::wstring findFfmpeg(const std::wstring& exeDir) {
    std::wstring ffmpeg = exeDir + platform::exeFileName(L"ffmpeg");
    if (!fileExists(ffmpeg)) {
        ffmpeg = platform::findInPath(platform::exeFileName(L"ffmpeg"));
        if (ffmpeg.empty()) ffmpeg = platform::exeFileName(L"ffmpeg");
    }
    return ffmpeg;
}

std::vector<std::string> requiredEncoders(const std::string& codec, const std::string& container) {
    switch (outKind(container)) {
        case OutKind::Gif: return {"gif"};
        case OutKind::Webp: return {"libwebp_anim"};
        case OutKind::Sequence: return {container};
        default: break;
    }
    if (codec == "h265") return {"libx265"};
    if (codec == "vp9") return {"libvpx-vp9"};
    if (codec == "av1") return {"libsvtav1", "libaom-av1"};
    if (isProRes(codec)) return {"prores_ks"};
    if (isDnxhr(codec)) return {"dnxhd"};
    return {"libx264"};
}

static std::mutex g_capMutex;
static std::map<std::wstring, std::set<std::string>> g_caps;

static std::set<std::string> listEncoders(const std::wstring& ffmpeg) {
    std::lock_guard<std::mutex> lock(g_capMutex);
    auto it = g_caps.find(ffmpeg);
    if (it != g_caps.end()) return it->second;
    std::set<std::string> names;
    const int rc = runProcess(L"\"" + ffmpeg + L"\" -hide_banner -encoders", [&](const std::wstring& line) {
        // " V....D libx264              libx264 H.264 ..." : 2e mot = nom de l'encodeur
        const std::string l = U8(line);
        if (l.size() < 9 || l[0] != ' ' || l[7] != ' ') return;
        size_t e = l.find(' ', 8);
        names.insert(l.substr(8, e == std::string::npos ? std::string::npos : e - 8));
    });
    if (rc == 0 && !names.empty()) g_caps[ffmpeg] = names;  // pas de cache si ffmpeg est absent : il peut arriver ensuite
    return names;
}

void forgetFfmpegCapabilities() {
    std::lock_guard<std::mutex> lock(g_capMutex);
    g_caps.clear();
}

static bool anyEncoder(const std::wstring& ffmpeg, const std::vector<std::string>& wanted) {
    const std::set<std::string> have = listEncoders(ffmpeg);
    if (have.empty()) return true;  // ffmpeg introuvable : on ne sait pas, on ne grise rien
    for (const auto& w : wanted)
        if (have.count(w)) return true;
    return false;
}

bool formatAvailable(const std::wstring& ffmpeg, const std::string& codec, const std::string& container) {
    return anyEncoder(ffmpeg, requiredEncoders(codec, container));
}
bool codecAvailable(const std::wstring& ffmpeg, const std::string& codec) {
    return anyEncoder(ffmpeg, requiredEncoders(codec, "mp4"));
}
bool containerAvailable(const std::wstring& ffmpeg, const std::string& container) {
    if (outKind(container) == OutKind::Video) return true;
    return anyEncoder(ffmpeg, requiredEncoders("h264", container));
}

// ---------------------------------------------------------------------------
//  Estimation de la taille
// ---------------------------------------------------------------------------
double renderedSeconds(const Settings& s) {
    const double fps = s.fps > 0 ? s.fps : 60.0;
    double d = s.duration > 0 ? s.duration : 10.0;
    if (s.loop > 0) d = s.loop * std::max(1.0, std::floor(d / s.loop + 0.5));
    return std::max(1LL, std::llround(d * fps)) / fps;
}

double estimateSizeBytes(const Settings& s, double seconds, bool audio) {
    // la sortie compte : cadence apres interpolation, taille apres mise a l'echelle, canal alpha en plus
    const double fps = s.interp > 0 && outKind(s.container) == OutKind::Video ? s.interp : (s.fps > 0 ? s.fps : 60.0);
    const double frames = seconds * fps;
    int ow = s.width, oh = s.height;
    outputSize(s, ow, oh);
    const double px = (double)ow * oh;
    const bool deep = s.depth >= 10 || s.hdr != "off";
    const double alphaK = s.alpha ? (s.container == "tiff" || s.container == "exr" ? 1.33 : 1.25) : 1.0;
    switch (outKind(s.container)) {
        case OutKind::Gif: return px * frames * 0.25;
        case OutKind::Webp: return px * frames * 0.08 * alphaK;
        case OutKind::Sequence:
            return px * frames * (s.container == "png" ? 1.4 : s.container == "tiff" ? 3.0 : 4.0) * (deep && s.container != "exr" ? 2.0 : 1.0) * alphaK;
        default: break;
    }
    double bytes;
    if (isProRes(s.codec) || isDnxhr(s.codec)) {  // debit quasi fixe, en bits par pixel et par image (1080p30 de reference)
        double bpp = 3.5;
        if (s.codec == "prores_proxy") bpp = 0.72;
        else if (s.codec == "prores_lt") bpp = 1.64;
        else if (s.codec == "prores_422") bpp = 2.36;
        else if (s.codec == "prores_4444") bpp = 5.3;
        else if (s.codec == "dnxhr_sq") bpp = 2.3;
        else if (s.codec == "dnxhr_444") bpp = 7.0;
        bytes = px * frames * bpp / 8.0;
    } else if (s.bitrate > 0) {
        bytes = s.bitrate * 125.0 * seconds;
    } else {
        const double bpp = 0.3 * std::pow(2.0, -(s.crf - 12) / 6.0);  // x264 : ordre de grandeur
        const double ratio = s.codec == "h265" ? 0.6 : s.codec == "vp9" ? 0.65 : s.codec == "av1" ? 0.5 : 1.0;
        bytes = px * frames * bpp * ratio / 8.0;
    }
    if (s.alpha) bytes *= alphaK;  // ProRes 4444 et VP9 : canal alpha en plus
    if (audio && !s.audio.empty()) bytes += 192.0 * 125.0 * seconds;
    return bytes;
}

std::wstring estimateText(const Settings& s, const std::wstring& dir) {
    const double bytes = estimateSizeBytes(s, renderedSeconds(s), true);
    auto round = [](double v, double scale) { return std::floor(v * scale + 0.5) / scale; };
    std::wstring txt = bytes >= 1e9 ? TXF("ui.est.gb", i18n::num(round(bytes / 1073741824.0, 100)).c_str())
                                    : TXF("ui.est.mb", i18n::num(round(bytes / 1048576.0, 10)).c_str());
    uint64_t freeB = 0;
    if (!dir.empty() && platform::freeSpace(dir, freeB) && bytes * 1.2 > (double)freeB) txt += L"  -  " + TX("ui.est.nospace");
    return txt;
}

// ---------------------------------------------------------------------------
//  Nom du fichier de sortie
// ---------------------------------------------------------------------------
static std::wstring expandName(const std::wstring& stem, const Settings& s) {
    std::wstring tpl = s.name.empty() ? L"{shader}" : s.name;
    const platform::DateTime t = platform::localTime();
    const std::pair<std::wstring, std::wstring> tokens[] = {
        {L"{shader}", stem},
        {L"{w}", std::to_wstring(s.width)},
        {L"{h}", std::to_wstring(s.height)},
        {L"{fps}", wfmt(L"%g", s.fps)},
        {L"{date}", wfmt(L"%04d-%02d-%02d", t.year, t.month, t.day)},
        {L"{codec}", W(s.codec)}};
    for (const auto& tk : tokens) {
        size_t p = 0;
        while ((p = tpl.find(tk.first, p)) != std::wstring::npos) {
            tpl.replace(p, tk.first.size(), tk.second);
            p += tk.second.size();
        }
    }
    for (auto& c : tpl)
        if (wcschr(L"\\/:*?\"<>|", c) || c < 32) c = L'_';
    while (!tpl.empty() && (tpl.back() == L'.' || tpl.back() == L' ')) tpl.pop_back();
    return tpl.empty() ? stem : tpl;
}

std::wstring outputFile(const std::wstring& shaderPath, const Settings& s, bool forWrite, bool* skip) {
    const size_t slash = shaderPath.find_last_of(L"\\/");
    std::wstring dir = slash == std::wstring::npos ? L"" : shaderPath.substr(0, slash + 1);
    std::wstring stem = shaderPath.substr(slash == std::wstring::npos ? 0 : slash + 1);
    if (size_t dot = stem.find_last_of(L'.'); dot != std::wstring::npos) stem.erase(dot);
    if (!s.outdir.empty()) {
        dir = s.outdir;
        if (dir.back() != L'\\' && dir.back() != L'/') dir += platform::pathSeparator();
    }
    const std::wstring base = expandName(stem, s);
    const bool seq = outKind(s.container) == OutKind::Sequence;
    auto nameOf = [&](int n) { return n <= 1 ? base : base + L"-" + std::to_wstring(n); };
    auto target = [&](int n) { return seq ? dir + nameOf(n) + L"_frames" : dir + nameOf(n) + L"." + W(s.container); };
    auto exists = [&](int n) { return seq ? platform::dirExists(target(n)) : fileExists(target(n)); };
    int n = 1;
    if (s.exists == "number") {
        if (forWrite) while (exists(n)) n++;
        else while (exists(n + 1)) n++;
    } else if (skip && forWrite && s.exists == "skip" && exists(1)) {
        *skip = true;
    }
    if (!seq) return target(n);
    return target(n) + platform::pathSeparator() + nameOf(n) + L"_000000." + W(s.container);
}

std::wstring sequenceDir(const std::wstring& firstFrame) { return firstFrame.substr(0, firstFrame.find_last_of(L"\\/")); }

std::wstring sequencePattern(const std::wstring& firstFrame) {
    const size_t p = firstFrame.rfind(L"_000000.");
    if (p == std::wstring::npos) return firstFrame;
    return firstFrame.substr(0, p) + L"_%06d" + firstFrame.substr(p + 7);
}
