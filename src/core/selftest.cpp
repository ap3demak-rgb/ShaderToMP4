// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "selftest.h"
#include "formats.h"
#include "gl.h"
#include "i18n.h"
#include "process.h"
#include "render.h"
#include "util.h"
#include "platform/platform.h"

// ---------------------------------------------------------------------------
//  Formats de sortie : chaque combinaison est rendue (64x64, 5 images) puis relue avec ffmpeg
// ---------------------------------------------------------------------------
namespace {

struct FmtCase {
    const char* codec;
    const char* container;
    int depth;
    const char* videoCodec;  // nom du flux tel que ffmpeg l'affiche
    const char* pixFmt;      // "" : non verifie
    int w, h;                // DNxHR demande au moins 256x120
    bool ref;                // image fixe : la premiere image est comparee a tests/<shader>.ref
};

const FmtCase kFmtCases[] = {
    {"h264", "mp4", 8, "h264", "yuv420p", 64, 64, false},
    {"h264", "mkv", 8, "h264", "yuv420p", 64, 64, false},
    {"h265", "mp4", 8, "hevc", "yuv420p", 64, 64, false},
    {"h265", "mp4", 10, "hevc", "yuv420p10le", 64, 64, false},
    {"vp9", "webm", 8, "vp9", "yuv420p", 64, 64, false},
    {"vp9", "webm", 10, "vp9", "yuv420p10le", 64, 64, false},
    {"av1", "webm", 8, "av1", "yuv420p", 64, 64, false},
    {"av1", "mkv", 10, "av1", "yuv420p10le", 64, 64, false},
    {"prores_hq", "mov", 8, "prores", "yuv422p10le", 64, 64, false},
    {"prores_4444", "mov", 8, "prores", "yuv444p12le", 64, 64, false},
    {"dnxhr_hq", "mov", 8, "dnxhd", "yuv422p", 256, 128, false},
    {"h264", "gif", 8, "gif", "", 64, 64, false},
    {"h264", "webp", 8, "webp", "", 64, 64, false},
    {"h264", "png", 8, "png", "rgb24", 64, 64, true},
    {"h264", "png", 10, "png", "rgb48be", 64, 64, true},
    {"h264", "tiff", 8, "tiff", "rgb24", 64, 64, true},
    {"h264", "exr", 8, "exr", "", 64, 64, false},
};

std::string probeInfo(const std::wstring& ffmpeg, const std::wstring& path) {
    std::string all;
    runProcess(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -i \"" + path + L"\"",
               [&](const std::wstring& line) { all += U8(line) + "\n"; });
    return all;
}

// Duree « Duration: hh:mm:ss.cc » en secondes, -1 si absente
double probeDuration(const std::string& info) {
    const size_t p = info.find("Duration: ");
    if (p == std::string::npos) return -1;
    int h = 0, m = 0;
    double s = 0;
    if (sscanf(info.c_str() + p + 10, "%d:%d:%lf", &h, &m, &s) != 3) return -1;
    return h * 3600.0 + m * 60.0 + s;
}

void runFormatTests(const std::wstring& exeDir, const std::wstring& dir, const std::wstring& shader, std::wstring& report,
                    int& total, int& fails) {
    const std::wstring ffmpeg = findFfmpeg(exeDir);
    std::string ver;
    if (runProcess(L"\"" + ffmpeg + L"\" -hide_banner -version", [&](const std::wstring& l) { ver += U8(l); }) != 0) {
        report += L"SKIP    formats: " + TX("selftest.fmt_no_ffmpeg") + L"\r\n";
        return;
    }
    const std::wstring outDir = dir + L"formats_out";
    removeTree(outDir);
    platform::makeDir(outDir);
    int index = 0;
    auto base = [&](const char* codec, const char* container, int depth) {
        Settings st;
        st.width = 64;
        st.height = 64;
        st.fps = 10;
        st.duration = 0.5;
        st.crf = 28;
        st.preset = "ultrafast";
        st.ss = 1;
        st.blur = 1;
        st.chunk = 0;
        st.codec = codec;
        st.container = container;
        st.depth = depth;
        st.outdir = outDir;
        st.name = L"case" + std::to_wstring(++index);
        return st;
    };
    auto render = [&](const Settings& st, std::wstring& e, std::string& logText) {
        Hooks H;
        H.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
        return convertFile(shader, exeDir, st, H, e) == Result::Ok;
    };

    for (const FmtCase& fc : kFmtCases) {
        const std::wstring label = W(std::string(fc.codec) + "/" + fc.container + (fc.depth > 8 ? " 10-bit" : ""));
        if (!formatAvailable(ffmpeg, fc.codec, fc.container)) {
            report += L"SKIP    " + label + L": " +
                      TXF("selftest.fmt_missing", W(requiredEncoders(fc.codec, fc.container).front()).c_str()) + L"\r\n";
            continue;
        }
        total++;
        Settings st = base(fc.codec, fc.container, fc.depth);
        st.width = fc.w;
        st.height = fc.h;
        if (fc.ref) st.start = 1.0;  // la premiere image est celle du temps 1 s, comme le test d'apercu
        std::wstring e, problem;
        std::string logText;
        auto bad = [&](const std::string& what) { problem = TXF("selftest.fmt_bad", W(what).c_str()); };
        if (!render(st, e, logText)) {
            problem = e.empty() ? TX("selftest.render_failed") : e;
        } else {
            const std::wstring first = outputFile(shader, st, false);
            const bool seq = outKind(st.container) == OutKind::Sequence;
            const std::string info = probeInfo(ffmpeg, seq ? sequencePattern(first) : first);
            if (info.find(std::string("Video: ") + fc.videoCodec) == std::string::npos) bad(std::string("codec != ") + fc.videoCodec);
            else if (info.find(" " + std::to_string(fc.w) + "x" + std::to_string(fc.h)) == std::string::npos) bad("size");
            else if (*fc.pixFmt && info.find(fc.pixFmt) == std::string::npos) bad(std::string("pix_fmt != ") + fc.pixFmt);
            else if (outKind(st.container) == OutKind::Video) {
                const double d = probeDuration(info);
                if (d < 0.35 || d > 0.8) bad("duration " + std::to_string(d));
            } else if (seq) {
                std::vector<platform::DirEntry> frames;
                platform::listDir(sequenceDir(first), frames);
                int count = 0;
                for (const auto& f : frames)
                    if (!f.isDir) count++;
                if (count != 5) bad("frames " + std::to_string(count) + " != 5");
            }
            if (problem.empty() && fc.ref) {  // image fixe : pixels compares a l'image de reference du shader
                const std::wstring raw = outDir + platform::pathSeparator() + L"first.raw";
                runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + sequencePattern(first) +
                               L"\" -frames:v 1 -f rawvideo -pix_fmt bgra \"" + raw + L"\"",
                           nullptr);
                std::string got, want;
                if (!readFile(raw, got) || !readFile(shader.substr(0, shader.find_last_of(L'.')) + L".ref", want)) bad("reference");
                else if (got.size() != want.size()) bad("reference size");
                else {
                    double sum = 0;
                    size_t cnt = 0;
                    for (size_t i = 0; i < got.size(); i++) {
                        if (i % 4 == 3) continue;  // alpha
                        sum += std::abs((int)(uint8_t)got[i] - (int)(uint8_t)want[i]);
                        cnt++;
                    }
                    if (cnt && sum / cnt > 2.0) bad("mean difference " + std::to_string(sum / cnt));
                }
                platform::removeFile(raw);
            }
        }
        if (problem.empty()) {
            report += L"PASS    " + label + L"\r\n";
        } else {
            fails++;
            report += L"FAIL    " + label + L": " + problem + L"\r\n";
        }
    }

    // Nom de sortie : modele, numerotation et saut d'un fichier existant
    if (formatAvailable(ffmpeg, "h264", "mp4")) {
        total++;
        Settings st = base("h264", "mp4", 8);
        st.name = L"{shader}_{w}x{h}";
        st.exists = "number";
        std::wstring e;
        std::string logText;
        const bool ok1 = render(st, e, logText), ok2 = render(st, e, logText);
        st.exists = "skip";
        const bool ok3 = render(st, e, logText);
        std::wstring stem = shader.substr(shader.find_last_of(L"\\/") + 1);
        stem.erase(stem.find_last_of(L'.'));
        const std::wstring pre = outDir + platform::pathSeparator() + stem + L"_64x64";
        const bool good = ok1 && ok2 && ok3 && fileExists(pre + L".mp4") && fileExists(pre + L"-2.mp4") && !fileExists(pre + L"-3.mp4");
        if (good) {
            report += L"PASS    " + TX("selftest.fmt_naming") + L"\r\n";
        } else {
            fails++;
            report += L"FAIL    " + TX("selftest.fmt_naming") + L"\r\n";
        }
    }
    // Parties : rendu par parties de 0,2 s puis assemblage par copie des flux (10 bits, WebM)
    if (formatAvailable(ffmpeg, "vp9", "webm")) {
        total++;
        Settings st = base("vp9", "webm", 10);
        st.chunk = 0.2;
        std::wstring e, problem;
        std::string logText;
        if (!render(st, e, logText)) {
            problem = e.empty() ? TX("selftest.render_failed") : e;
        } else {
            const std::wstring out = outputFile(shader, st, false);
            const double d = probeDuration(probeInfo(ffmpeg, out));
            if (d < 0.35 || d > 0.8 || platform::dirExists(out.substr(0, out.find_last_of(L'.')) + L".parts")) problem = TXF("selftest.fmt_bad", W("chunks").c_str());
        }
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_chunks") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    removeTree(outDir);
}

}  // namespace

int runSelfTest(const std::wstring& exeDir, bool update, bool quiet) {
    std::wstring dir = exeDir + L"tests/";
    if (!platform::dirExists(dir)) dir = exeDir + L"../tests/";
    std::wstring report;
    int total = 0, fails = 0;
    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        report = TXF("selftest.error", err.c_str()) + L"\r\n";
        fails = 1;
    } else {
        Settings base;
        base.width = 64;
        base.height = 64;
        base.ss = 1;
        base.blur = 1;
        std::vector<platform::DirEntry> entries, shaders;
        platform::listDir(dir, entries);
        for (const auto& en : entries)
            if (!en.isDir && en.name.size() > 5 && en.name.compare(en.name.size() - 5, 5, L".glsl") == 0) shaders.push_back(en);
        std::sort(shaders.begin(), shaders.end(), [](const platform::DirEntry& x, const platform::DirEntry& y) { return x.name < y.name; });
        if (shaders.empty()) {
            report = TXF("selftest.no_shaders", dir.c_str()) + L"\r\n";
            fails = 1;
        } else {
            for (const auto& en : shaders) {
                total++;
                const std::wstring file = en.name;
                const std::wstring ref = dir + file.substr(0, file.find_last_of(L'.')) + L".ref";
                std::vector<uint8_t> got;
                Hooks H;
                H.log = [](const std::wstring&) {};
                H.previewTime = 1.0;
                H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { got = std::move(px); };
                std::wstring e;
                std::wstring line;
                if (convertFile(dir + file, exeDir, base, H, e) != Result::Ok || got.empty()) {
                    line = L"FAIL    " + file + L": " + (e.empty() ? TX("selftest.render_failed") : e);
                    fails++;
                } else {
                    std::string want;
                    const bool have = readFile(ref, want);
                    if (update || !have) {
                        if (!update) {
                            line = L"FAIL    " + file + L": " + TX("selftest.ref_missing");
                            fails++;
                        } else if (FILE* f = platform::openFile(ref, "wb")) {
                            fwrite(got.data(), 1, got.size(), f);
                            fclose(f);
                            line = L"UPDATED " + file + L": " + TX("selftest.ref_written");
                        }
                    } else if (want.size() != got.size()) {
                        line = L"FAIL    " + file + L": " + TX("selftest.ref_size");
                        fails++;
                    } else {
                        double sum = 0;
                        size_t cnt = 0;
                        for (size_t i = 0; i < got.size(); i++) {
                            if (i % 4 == 3) continue;  // alpha
                            sum += std::abs((int)got[i] - (int)(uint8_t)want[i]);
                            cnt++;
                        }
                        const double mad = cnt ? sum / cnt : 0;  // ecart moyen sur 255
                        line = (mad <= 2.0 ? L"PASS    " : L"FAIL    ") + file + L" (" +
                               TXF(mad <= 2.0 ? "selftest.diff_ok" : "selftest.diff_bad", mad) + L")";
                        if (mad > 2.0) fails++;
                    }
                }
                report += line + L"\r\n";
            }
        }
        if (!shaders.empty()) runFormatTests(exeDir, dir, dir + shaders[0].name, report, total, fails);
        destroyGLContext(gl);
    }
    report += L"\r\n" + TXF("selftest.summary", total, fails) + L"\r\n";
    if (FILE* f = platform::openFile(dir + L"report.txt", "wb")) {
        std::string u = U8(report);
        fwrite(u.data(), 1, u.size(), f);
        fclose(f);
    }
#ifndef _WIN32
    if (quiet && fails) fputs(U8(report).c_str(), stderr);  // en ligne de commande, un echec doit rester lisible
#endif
    if (!quiet)
        platform::showMessage(APP_NAME_WSTR L" - " + TX("selftest.title"), report,
                              fails ? platform::MessageKind::Warning : platform::MessageKind::Info);
    return fails ? 1 : 0;
}
