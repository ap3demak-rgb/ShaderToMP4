// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "source.h"
#include "i18n.h"
#include "process.h"
#include "util.h"
#include "platform/platform.h"

namespace {

std::string lowerCopy(std::string s) { return lower(std::move(s)); }

// Secondes d'un « 00:00:10.04 »
bool parseClock(const std::string& t, double& out) {
    int h = 0, m = 0;
    double s = 0;
    if (sscanf(t.c_str(), "%d:%d:%lf", &h, &m, &s) != 3) return false;
    out = h * 3600.0 + m * 60.0 + s;
    return true;
}

}  // namespace

bool isSourcePattern(const std::wstring& path) { return path.find(L'%') != std::wstring::npos; }

std::wstring sourceStem(const std::wstring& path) {
    std::wstring name = path;
    while (!name.empty() && (name.back() == L'\\' || name.back() == L'/')) name.pop_back();
    const size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.erase(dot);
    for (size_t p; (p = name.find(L'%')) != std::wstring::npos;) name.erase(p, 1);  // motif de sequence : %04d devient 04d
    return name;
}

std::wstring resolveSource(const std::wstring& shaderDir, const std::wstring& path) {
    const bool absolute = path.size() > 1 && (path[1] == L':' || path[0] == L'/' || path[0] == L'\\');
    if (absolute || isSourcePattern(path) || fileExists(path) || platform::dirExists(path)) {
        if (!absolute && !shaderDir.empty() && isSourcePattern(path)) return shaderDir + path;
        return path;
    }
    const std::wstring rel = shaderDir + path;
    return fileExists(rel) || platform::dirExists(rel) ? rel : path;
}

double snapFps(double fps) {
    for (double base : {24.0, 30.0, 48.0, 60.0, 120.0}) {
        const double ntsc = base * 1000.0 / 1001.0;
        if (std::fabs(fps - ntsc) < 0.006) return ntsc;
    }
    return std::round(fps * 1000.0) / 1000.0;
}

std::vector<std::wstring> listSourceFiles(const std::wstring& dir) {
    static const std::set<std::wstring> kExt = {L"mp4", L"m4v", L"mov", L"mkv", L"webm", L"avi", L"wmv", L"flv", L"mpg", L"mpeg", L"ts", L"mts", L"m2ts",
                                                L"ogv", L"3gp", L"gif", L"png", L"jpg", L"jpeg", L"bmp", L"tif", L"tiff", L"webp"};
    std::vector<platform::DirEntry> entries;
    std::vector<std::wstring> out;
    if (!platform::listDir(dir, entries)) return out;
    for (const auto& e : entries) {
        if (e.isDir) continue;
        const size_t dot = e.name.find_last_of(L'.');
        if (dot == std::wstring::npos) continue;
        std::wstring ext = e.name.substr(dot + 1);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        if (kExt.count(ext)) out.push_back(e.name);
    }
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) { return lower(U8(a)) < lower(U8(b)); });
    const std::wstring sep = (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) ? L"" : std::wstring(1, platform::pathSeparator());
    for (auto& f : out) f = dir + sep + f;
    return out;
}

bool probeSource(const std::wstring& ffmpeg, const std::wstring& path, double seqFps, SourceInfo& o) {
    o = SourceInfo();
    std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -nostdin";
    if (isSourcePattern(path) && seqFps > 0) cmd += wfmt(L" -framerate %g", seqFps);
    cmd += L" -i \"" + path + L"\"";
    std::string all;
    runProcess(cmd, [&](const std::wstring& l) { all += U8(l) + "\n"; });
    bool haveVideo = false, noDuration = false, imageInput = false;
    bool inVideo = false;
    std::string videoCodec;
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find('\n', pos);
        if (e == std::string::npos) e = all.size();
        const std::string line = all.substr(pos, e - pos);
        pos = e + 1;
        if (line.compare(0, 8, "Input #0") == 0) {
            const std::string l = lowerCopy(line);
            imageInput = l.find("image2") != std::string::npos || l.find("_pipe") != std::string::npos;
        }
        const size_t d = line.find("Duration: ");
        if (d != std::string::npos) {
            double t = 0;
            if (parseClock(line.substr(d + 10), t)) o.duration = t;
            else noDuration = true;
        }
        if (line.find("Chapter #") != std::string::npos) o.chapters++;
        if (line.find("Stream #") != std::string::npos) {
            inVideo = false;
            const size_t v = line.find("Video: ");
            if (v != std::string::npos && !haveVideo && line.find("attached pic") == std::string::npos) {
                haveVideo = true;
                inVideo = true;
                const size_t c = line.find_first_of(" ,(", v + 7);
                videoCodec = line.substr(v + 7, c == std::string::npos ? std::string::npos : c - (v + 7));
                for (size_t i = v; i + 3 < line.size(); i++) {  // « 1920x1080 » entre espaces
                    if (!isdigit((unsigned char)line[i]) || (i > 0 && (isdigit((unsigned char)line[i - 1]) || line[i - 1] == 'x'))) continue;
                    int a = 0, b = 0, used = 0;
                    if (sscanf(line.c_str() + i, "%dx%d%n", &a, &b, &used) == 2 && a > 0 && b > 0 && line[i - 1] == ' ') {
                        const char next = line[i + used];
                        if (next == ' ' || next == ',' || next == '[') {
                            o.width = a;
                            o.height = b;
                            break;
                        }
                    }
                }
                double fps = 0, tbr = 0;  // « 30 fps, 30 tbr »
                size_t q = 0;
                while ((q = line.find(',', q)) != std::string::npos) {
                    q++;
                    double x = 0;
                    char unit[8] = {0};
                    if (sscanf(line.c_str() + q, " %lf %7s", &x, unit) == 2) {
                        if (strncmp(unit, "fps", 3) == 0) fps = x;
                        else if (strncmp(unit, "tbr", 3) == 0) tbr = x;
                    }
                }
                o.fps = fps > 0 ? fps : tbr;
                for (const char* tag : {"bt709", "bt470bg", "smpte170m", "bt2020", "smpte240m"})
                    if (line.find(tag, v) != std::string::npos) o.colorTagged = true;
            } else if (line.find("Audio: ") != std::string::npos) {
                o.hasAudio = true;
            } else if (const size_t sub = line.find("Subtitle: "); sub != std::string::npos) {
                const size_t c = line.find_first_of(" ,(", sub + 10);
                o.subCodecs.push_back(lowerCopy(line.substr(sub + 10, c == std::string::npos ? std::string::npos : c - (sub + 10))));
            }
        } else if (inVideo && line.find("rotation of") != std::string::npos) {
            const size_t r = line.find("rotation of");
            const double deg = atof(line.c_str() + r + 11);
            int rot = (int)std::lround(deg);
            rot = ((rot % 360) + 360) % 360;
            o.rotation = rot;
        }
    }
    if (!haveVideo || o.width <= 0 || o.height <= 0) return false;
    if (o.rotation == 90 || o.rotation == 270) std::swap(o.width, o.height);
    o.still = (noDuration || o.duration <= 0.0) ? true : (imageInput && !isSourcePattern(path) && o.duration < 0.1 && (videoCodec == "png" || videoCodec == "mjpeg" || videoCodec == "bmp" || videoCodec == "webp" || videoCodec == "tiff" || videoCodec == "jpegls" || videoCodec == "mjpeg"));
    if (o.fps <= 0) o.fps = 25;
    o.fps = snapFps(o.fps);
    o.ok = true;
    return true;
}

static bool sourceMatches(const Settings& s, const char* what) { return s.sourceMatch.find(what) != std::string::npos; }

bool applySource(Settings& s, const std::wstring& shaderDir, const std::wstring& ffmpeg, const std::set<std::string>& pinnedIn,
                 const std::function<void(const std::wstring&)>& log, std::wstring& err) {
    s.rt = SourceRuntime();
    std::set<std::string> pinned = pinnedIn;
    pinned.insert(s.pinned.begin(), s.pinned.end());
    if (s.source.empty()) return true;
    std::wstring pathA = resolveSource(shaderDir, s.source);
    if (platform::dirExists(pathA)) {  // dossier : le premier fichier sert a l'apercu ; le rendu complet traite tous les fichiers (render.cpp)
        const std::vector<std::wstring> files = listSourceFiles(pathA);
        if (files.empty()) {
            err = TXF("error.source_empty", pathA.c_str());
            return false;
        }
        pathA = files[0];
    }
    if (!isSourcePattern(pathA) && !fileExists(pathA)) {
        err = TXF("error.source_missing", pathA.c_str());
        return false;
    }
    SourceInfo a;
    if (!probeSource(ffmpeg, pathA, s.sourceFps, a)) {
        err = TXF("error.source_read", pathA.c_str());
        return false;
    }
    SourceRuntime& rt = s.rt;
    rt.active = true;
    rt.path = pathA;
    rt.width = a.width;
    rt.height = a.height;
    rt.fps = a.fps;
    rt.duration = a.duration;
    rt.still = a.still;
    rt.hasAudio = a.hasAudio && !a.still;
    rt.chapters = a.chapters;
    if (!a.colorTagged && a.height >= 720) rt.matrix = "bt709";  // sans etiquette, les lecteurs prennent bt709 en haute definition (ffmpeg : bt601)
    rt.subCodecs = a.subCodecs;
    if (s.reverse && a.still) log(L"  " + TX("log.source_reverse_still"));
    if (s.reverse && !s.source2.empty()) {
        err = TX("error.source_reverse_transition");
        return false;
    }
    if (sourceMatches(s, "size") && !pinned.count("width") && !pinned.count("height")) {
        s.width = a.width;
        s.height = a.height;
    }
    if (sourceMatches(s, "fps") && !pinned.count("fps") && !a.still) s.fps = a.fps;
    if (a.still) log(L"  " + TX("log.source_still"));
    log(L"  " + TXF("log.source_info", pathA.c_str(), a.width, a.height, a.fps, a.duration));
    // iChannel0 : la source, a pleine taille (sauf si le shader choisit lui-meme son canal 0)
    if (s.channel[0].empty()) s.channel[0] = L"source:" + pathA;
    else log(L"  " + TX("log.source_channel0_set"));

    double durA = a.still ? s.duration : a.duration;
    if (!s.source2.empty()) {
        const std::wstring pathB = resolveSource(shaderDir, s.source2);
        if (!isSourcePattern(pathB) && !fileExists(pathB)) {
            err = TXF("error.source_missing", pathB.c_str());
            return false;
        }
        SourceInfo b;
        if (!probeSource(ffmpeg, pathB, s.sourceFps, b)) {
            err = TXF("error.source_read", pathB.c_str());
            return false;
        }
        rt.two = true;
        rt.path2 = pathB;
        rt.width2 = b.width;
        rt.height2 = b.height;
        rt.fps2 = b.fps;
        rt.duration2 = b.duration;
        rt.still2 = b.still;
        if (!b.colorTagged && b.height >= 720) rt.matrix2 = "bt709";
        rt.hasAudio2 = b.hasAudio && !b.still;
        if (s.channel[1].empty()) s.channel[1] = L"source2:" + pathB;
        else log(L"  " + TX("log.source_channel1_set"));
        const double durB = b.still ? 0.0 : b.duration;
        const double T = std::max(0.0, std::min(s.transitionDur, std::min(durA, b.still ? durA : durB)));
        const double off = s.transitionOffset;
        rt.transition = true;
        rt.progLen = T;
        rt.progStart = durA - T + off;
        rt.timeOffset2 = -rt.progStart;
        log(L"  " + TXF("log.source_transition", pathB.c_str(), T, rt.progStart));
        if (sourceMatches(s, "duration") && !pinned.count("duration")) {
            const double total = (durA - s.start) + (b.still ? T : durB) - T + off;
            s.duration = std::max(0.04, total);
        }
    } else if (sourceMatches(s, "duration") && !pinned.count("duration") && !a.still) {
        s.duration = std::max(0.04, a.duration - s.start);
    }
    // son : celui de la source, sauf sourceaudio=0 ou un son impose par le shader ou la ligne de commande
    if (s.sourceAudio && !pinned.count("audio")) {  // le son de la source remplace celui des reglages (sourceaudio=0 pour garder le sien)
        if (rt.transition && rt.hasAudio && rt.hasAudio2) {
            rt.audioFromSource = true;  // le programme fabrique le fondu enchaine (render.cpp)
        } else if (rt.hasAudio) {
            s.audio = pathA;
            rt.audioFromSource = true;
        }
    }
    return true;
}

std::wstring sourceExtraMaps(const Settings& s, bool& dropped) {
    dropped = false;
    if (!s.rt.active || !s.rt.audioFromSource || s.rt.transition || s.audio != s.rt.path) return L"";
    std::wstring a;
    if (s.rt.chapters > 0) a += L" -map_chapters 1";
    if (!s.rt.subCodecs.empty()) {
        auto allIn = [&](std::initializer_list<const char*> names) {
            for (const std::string& c : s.rt.subCodecs) {
                bool ok = false;
                for (const char* n : names)
                    if (c == n) ok = true;
                if (!ok) return false;
            }
            return true;
        };
        if (s.container == "mkv") {
            bool movText = false;  // le texte des mp4 (mov_text) n'entre pas tel quel dans Matroska : converti en srt
            for (const std::string& c : s.rt.subCodecs)
                if (c == "mov_text" || c == "text") movText = true;
            a += movText ? L" -map 1:s? -c:s srt" : L" -map 1:s? -c:s copy";
        }
        else if ((s.container == "mp4" || s.container == "mov") && allIn({"subrip", "srt", "ass", "ssa", "webvtt", "mov_text", "text"})) a += L" -map 1:s? -c:s mov_text";
        else if (s.container == "webm" && allIn({"subrip", "srt", "webvtt"})) a += L" -map 1:s? -c:s webvtt";
        else dropped = true;
    }
    return a;
}

bool buildTransitionAudio(const std::wstring& ffmpeg, const Settings& s, double totalSeconds, const std::wstring& outWav) {
    const SourceRuntime& rt = s.rt;
    const double startOut = std::max(0.0, rt.progStart - s.start), T = rt.progLen;
    const std::wstring fmt = L"aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo";
    const std::wstring delay = wfmt(L"%d", (int)std::lround(startOut * 1000.0));
    std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -ss " + wfmt(L"%g", s.start) + L" -i \"" + rt.path + L"\" -i \"" + rt.path2 + L"\"";
    cmd += L" -filter_complex \"[0:a]" + fmt + wfmt(L",afade=t=out:st=%g:d=%g[a];[1:a]", startOut, std::max(0.001, T)) + fmt + wfmt(L",afade=t=in:st=0:d=%g,adelay=", std::max(0.001, T)) +
           delay + L"|" + delay + L"[b];[a][b]amix=inputs=2:duration=longest:normalize=0\" -t " + wfmt(L"%g", totalSeconds) + L" \"" + outWav + L"\"";
    return runProcess(cmd, nullptr) == 0 && fileExists(outWav);
}
