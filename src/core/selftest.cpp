// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "selftest.h"
#include "formats.h"
#include "gl.h"
#include "locate.h"
#include "montage.h"
#include "looptools.h"
#include "timemap.h"
#include "vr.h"
#include "batches.h"
#include "channeldrop.h"
#include "glslsyntax.h"
#include "passfiles.h"
#include "filesettings.h"
#include "outputopts.h"
#include "report.h"
#include "overlay.h"
#include "params.h"
#include "passes.h"
#include "player.h"
#include "shadertoy_import.h"
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
    std::wstring renderShader = shader;  // shader rendu par `render` (circle.glsl, sauf pour les tests de passes)
    auto render = [&](const Settings& st, std::wstring& e, std::string& logText) {
        Hooks H;
        H.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
        return convertFile(renderShader, exeDir, st, H, e) == Result::Ok;
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
        Settings polluted;  // un config.ini ecrit avec un commentaire derriere le modele ne doit pas le polluer
        parseLines(polluted, "name={shader}_x      ; modele du nom\nname2=1\n", false);
        Settings polluted2;
        parseLines(polluted2, "name=      ; modele du nom\n", false);
        const bool good = polluted.name == L"{shader}_x" && polluted2.name.empty() && ok1 && ok2 && ok3 && fileExists(pre + L".mp4") && fileExists(pre + L"-2.mp4") && !fileExists(pre + L"-3.mp4");
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
    // Passes multiples : le rendu par parties (etat des tampons conserve d'une partie a l'autre) doit donner
    // exactement les memes images qu'un rendu d'un seul tenant
    if (formatAvailable(ffmpeg, "h264", "mkv") && fileExists(dir + L"buffers.glsl")) {
        total++;
        std::wstring problem;
        std::string rawA, rawB;
        renderShader = dir + L"buffers.glsl";
        for (int pass = 0; pass < 2 && problem.empty(); pass++) {
            Settings st = base("h264", "mkv", 8);
            st.crf = 0;
            st.chunk = pass ? 0.2 : 0;
            st.name = pass ? L"buffers_chunked" : L"buffers_single";
            std::wstring e;
            std::string logText;
            if (!render(st, e, logText)) {
                problem = e.empty() ? TX("selftest.render_failed") : e;
                break;
            }
            const std::wstring raw = outDir + platform::pathSeparator() + L"buffers.raw";
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + outputFile(dir + L"buffers.glsl", st, false) +
                           L"\" -f rawvideo -pix_fmt rgb24 \"" + raw + L"\"",
                       nullptr);
            readFile(raw, pass ? rawB : rawA);
        }
        if (problem.empty() && (rawA.empty() || rawA != rawB)) problem = TXF("selftest.fmt_bad", W("chunks != single").c_str());
        renderShader = shader;
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_buffers") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }

    // Passe Sound : 7 s de sinus 440 Hz a gauche (deux blocs de 512x512 echantillons), silence a droite ; le son relu dans la
    // video doit avoir la bonne amplitude, rester continu a la jonction des blocs et ne pas laisser de WAV temporaire
    if (formatAvailable(ffmpeg, "h264", "mkv")) {
        total++;
        std::wstring problem;
        const std::wstring sndShader = outDir + platform::pathSeparator() + L"sndtest.glsl";
        auto put = [&](const std::wstring& path, const char* text) {
            if (FILE* f = platform::openFile(path, "wb")) {
                fputs(text, f);
                fclose(f);
            }
        };
        put(sndShader, "void mainImage(out vec4 c, in vec2 p){ c = vec4(0.2, 0.4, 0.6, 1.0); }\n");
        put(outDir + platform::pathSeparator() + L"sndtest.sound.glsl",
            "vec2 mainSound(in int samp, float t){ return vec2(0.5 * sin(6.2831853 * 440.0 * t), 0.0); }\n");
        Settings st = base("h264", "mkv", 8);
        st.fps = 5;
        st.duration = 7;
        st.name = L"sndtest_out";
        std::wstring e;
        std::string logText;
        renderShader = sndShader;
        if (!render(st, e, logText)) {
            problem = e.empty() ? TX("selftest.render_failed") : e;
        } else {
            const std::wstring out = outputFile(sndShader, st, false);
            const std::wstring raw = outDir + platform::pathSeparator() + L"sndtest.raw";
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + out +
                           L"\" -vn -f s16le -ac 2 -ar 44100 \"" + raw + L"\"",
                       nullptr);
            std::string pcm;
            readFile(raw, pcm);
            const int16_t* sm = reinterpret_cast<const int16_t*>(pcm.data());
            const size_t count = pcm.size() / 4;  // echantillons stereo
            int peakL = 0, peakR = 0, jump = 0;
            if (count > 300000) {
                for (size_t i = 250000; i < 300000; i++) {
                    peakL = std::max(peakL, std::abs((int)sm[2 * i]));
                    peakR = std::max(peakR, std::abs((int)sm[2 * i + 1]));
                }
                for (size_t i = 262000; i < 262300; i++) jump = std::max(jump, std::abs((int)sm[2 * i] - (int)sm[2 * (i - 1)]));
            }
            if (count <= 300000 || peakL < 12000 || peakL > 20000 || peakR > 1500 || jump > 3000)
                problem = TXF("selftest.fmt_bad", wfmt(L"L %d, R %d, saut %d, %d", peakL, peakR, jump, (int)count).c_str());
            else if (fileExists(out + L".sound.wav"))
                problem = TXF("selftest.fmt_bad", W("wav").c_str());
        }
        renderShader = shader;
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_sound") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }

    // Apercu en temps reel : le lecteur envoie des images, suit un saut dans le temps et s'arrete proprement
    {
        const bool threads = platform::prepareOffThreadContexts();
        total++;
        std::atomic<int> frames{0};
        std::atomic<double> lastT{-1.0};
        Settings st = base("h264", "mp4", 8);
        st.duration = 4.0;
        std::mutex logMutex;
        std::wstring problem, playerLog;  // dernier message du lecteur, joint au diagnostic
        if (!threads) problem = TXF("selftest.fmt_bad", W("thread context").c_str());
        else {
            PreviewPlayer player;
            player.start(shader, exeDir, st, 0.0, [&](int w, int h, std::vector<uint8_t>&& px, double t) {
                if (w > 0 && h > 0 && px.size() == (size_t)w * h * 4) {
                    frames++;
                    lastT = t;
                }
            }, [] { return true; }, [&](const std::wstring& m) {
                std::lock_guard<std::mutex> l(logMutex);
                playerLog = m;
            });
            for (int i = 0; i < 100 && frames < 3; i++) platform::sleepMs(50);
            const int before = frames;
            player.seek(3.0);
            for (int i = 0; i < 60 && lastT < 2.9; i++) platform::sleepMs(25);
            player.setPlaying(false);
            platform::sleepMs(150);
            const int paused = frames;
            platform::sleepMs(250);
            if (before < 3) problem = TXF("selftest.fmt_bad", (W("no frame ") + (std::lock_guard<std::mutex>(logMutex), playerLog)).c_str());
            else if (lastT < 2.9) problem = TXF("selftest.fmt_bad", W("seek").c_str());
            else if (frames > paused + 1) problem = TXF("selftest.fmt_bad", W("pause").c_str());
        }  // le destructeur arrete le thread
        if (threads && problem.empty()) {  // rechargement automatique : le fichier change pendant la lecture, la couleur change aussi
            const std::wstring live = outDir + platform::pathSeparator() + L"live.glsl";
            auto write = [&](const char* body) {
                if (FILE* f = platform::openFile(live, "wb")) {
                    fputs(body, f);
                    fclose(f);
                }
            };
            write("void mainImage(out vec4 o, in vec2 p){ o = vec4(1.0, 0.0, 0.0, 1.0); }\n");
            std::atomic<int> lastRed{-1}, logs{0};
            PreviewPlayer p2;
            p2.start(live, exeDir, st, 0.0, [&](int w, int h, std::vector<uint8_t>&& px, double) {
                if (w > 0 && h > 0) lastRed = px[2];  // BGRA : rouge au 3e octet du premier pixel
            }, [] { return true; }, [&](const std::wstring&) { logs++; });
            for (int i = 0; i < 100 && lastRed < 0; i++) platform::sleepMs(25);
            const int first = lastRed;
            write("void mainImage(out vec4 o, in vec2 p){ o = vec4(0.0, 0.0, 1.0, 1.0); }\n");
            for (int i = 0; i < 120 && lastRed > 128; i++) platform::sleepMs(25);
            if (first < 200 || lastRed > 50 || logs < 1) problem = TXF("selftest.fmt_bad", W("reload").c_str());
        }
        platform::releaseOffThreadContexts();
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_player") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // Variantes : le rendu de base puis un rendu par variante (taille et nom propres)
    if (formatAvailable(ffmpeg, "h264", "gif")) {
        total++;
        const std::wstring vshader = outDir + platform::pathSeparator() + L"withvariant.glsl";
        const std::string vsrc = "// variant=small:width=32|height=32\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(p / iResolution.xy, 0.5, 1.0); }\n";
        if (FILE* f = platform::openFile(vshader, "wb")) {
            fwrite(vsrc.data(), 1, vsrc.size(), f);
            fclose(f);
        }
        Settings st = base("h264", "gif", 8);
        st.name = L"";  // modele par defaut : {shader}
        std::wstring e, problem;
        Hooks H;
        H.log = [](const std::wstring&) {};
        const bool ok = convertFile(vshader, exeDir, st, H, e) == Result::Ok;
        const std::wstring baseOut = outDir + platform::pathSeparator() + L"withvariant.gif";
        const std::wstring varOut = outDir + platform::pathSeparator() + L"withvariant_small.gif";
        if (!ok || !fileExists(baseOut) || !fileExists(varOut)) problem = TXF("selftest.fmt_bad", W("variant files").c_str());
        else if (probeInfo(ffmpeg, baseOut).find(" 64x64") == std::string::npos || probeInfo(ffmpeg, varOut).find(" 32x32") == std::string::npos)
            problem = TXF("selftest.fmt_bad", W("variant size").c_str());
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_variants") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // mainVR en 360 stereo : metadonnees spheriques ajoutees au MP4 (moov avant mdat : les positions des donnees sont decalees),
    // fichier toujours lisible et decodable sans erreur par ffmpeg
    if (formatAvailable(ffmpeg, "h264", "mp4")) {
        total++;
        const std::wstring vshader = outDir + platform::pathSeparator() + L"vrtest.glsl";
        const std::string vsrc =
            "// vr=stereo\nvoid mainVR(out vec4 o, in vec2 p, in vec3 ro, in vec3 rd){ o = vec4(rd * 0.5 + 0.5, 1.0); }\n";
        if (FILE* f = platform::openFile(vshader, "wb")) {
            fwrite(vsrc.data(), 1, vsrc.size(), f);
            fclose(f);
        }
        Settings st = base("h264", "mp4", 8);
        st.name = L"";
        std::wstring e, problem;
        Hooks H;
        H.log = [](const std::wstring&) {};
        const bool ok = convertFile(vshader, exeDir, st, H, e) == Result::Ok;
        const std::wstring vout = outDir + platform::pathSeparator() + L"vrtest.mp4";
        std::string bytes;
        static const char kUuid[17] = "\xff\xcc\x82\x63\xf8\x55\x4a\x93\x88\x14\x58\x7a\x02\x52\x1f\xdd";
        if (!ok || !readFile(vout, bytes)) {
            problem = e.empty() ? TX("selftest.render_failed") : e;
        } else if (bytes.find(std::string(kUuid, 16)) == std::string::npos || bytes.find("top-bottom") == std::string::npos) {
            problem = TXF("selftest.fmt_bad", W("no spherical metadata").c_str());
        } else {
            const std::string info = probeInfo(ffmpeg, vout);
            int decodeLines = 0;
            const int rc = runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -i \"" + vout + L"\" -f null -",
                                      [&](const std::wstring&) { decodeLines++; });
            const double d = probeDuration(info);
            if (info.find(" 64x64") == std::string::npos || d < 0.35 || d > 0.8) problem = TXF("selftest.fmt_bad", W("vr size/duration").c_str());
            else if (rc != 0 || decodeLines != 0) problem = TXF("selftest.fmt_bad", W("vr decode").c_str());
        }
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_vr") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // Chaine flottante complete : supersampling x2, flou de mouvement x4 et sortie 16 bits avec un effet de couleur sur un aplat
    // (saturation -1 : les trois canaux deviennent la luminance 0.2126 r + 0.7152 g + 0.0722 b)
    if (formatAvailable(ffmpeg, "h264", "png")) {
        total++;
        const std::wstring vshader = outDir + platform::pathSeparator() + L"postdeep.glsl";
        const std::string vsrc = "// post=saturation:-1\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(0.25, 0.5, 0.75, 1.0); }\n";
        if (FILE* f = platform::openFile(vshader, "wb")) {
            fwrite(vsrc.data(), 1, vsrc.size(), f);
            fclose(f);
        }
        Settings st = base("h264", "png", 10);
        st.ss = 2;
        st.blur = 4;
        st.name = L"postdeep";
        std::wstring e, problem;
        Hooks H;
        H.log = [](const std::wstring&) {};
        if (convertFile(vshader, exeDir, st, H, e) != Result::Ok) {
            problem = e.empty() ? TX("selftest.render_failed") : e;
        } else {
            const std::wstring first = outputFile(vshader, st, false);
            const std::wstring raw = outDir + platform::pathSeparator() + L"postdeep.raw";
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + sequencePattern(first) +
                           L"\" -frames:v 1 -f rawvideo -pix_fmt rgb48le \"" + raw + L"\"",
                       nullptr);
            std::string bytes;
            if (!readFile(raw, bytes) || bytes.size() < 6) {
                problem = TXF("selftest.fmt_bad", W("no frame").c_str());
            } else {
                const double want = (0.2126 * 0.25 + 0.7152 * 0.5 + 0.0722 * 0.75) * 65535.0;
                for (int c = 0; c < 3; c++) {
                    const double v = (uint8_t)bytes[(size_t)c * 2] | ((uint8_t)bytes[(size_t)c * 2 + 1] << 8);
                    if (std::fabs(v - want) > 0.02 * 65535.0) {
                        problem = TXF("selftest.fmt_bad", W("16-bit value " + std::to_string(v) + " != " + std::to_string(want)).c_str());
                        break;
                    }
                }
            }
            platform::removeFile(raw);
        }
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_postdeep") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // --- Sortie : transparence, HDR, mise a l'echelle, interpolation, normalisation du son, texte incruste ---------------------------
    {
        const std::wstring sep(1, platform::pathSeparator());
        auto put = [&](const std::wstring& name, const std::string& body) {
            const std::wstring path = outDir + sep + name;
            if (FILE* f = platform::openFile(path, "wb")) {
                fwrite(body.data(), 1, body.size(), f);
                fclose(f);
            }
            return path;
        };
        auto check = [&](const char* key, const std::wstring& problem) {
            total++;
            report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX(key) + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
            if (!problem.empty()) fails++;
        };
        auto bad = [&](const std::string& what) { return TXF("selftest.fmt_bad", W(what).c_str()); };
        auto renderOne = [&](const std::wstring& shader, const Settings& st, std::wstring& e, std::string& logText) {
            Hooks H;
            H.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
            return convertFile(shader, exeDir, st, H, e) == Result::Ok;
        };
        // premiere image decodee par ffmpeg (pre : options d'entree) au format de pixel pix
        auto decodeRaw = [&](const std::wstring& file, const wchar_t* pix, const std::wstring& pre) {
            const std::wstring raw = outDir + sep + L"dec.raw";
            platform::removeFile(raw);
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y " + pre + L" -i \"" + file + L"\" -frames:v 1 -f rawvideo -pix_fmt " + pix +
                           L" \"" + raw + L"\"",
                       nullptr);
            std::string bytes;
            readFile(raw, bytes);
            return bytes;
        };
        const std::string alphaSrc = "// alpha=1\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(1.0, 0.5, 0.0, p.x / iResolution.x); }\n";
        const std::wstring alphaShader = put(L"alpha_ramp.glsl", alphaSrc);

        // transparence : la rampe d'alpha et la couleur a alpha droit sortent intactes en PNG, ProRes 4444 et VP9
        for (const char* kind : {"png", "prores", "vp9"}) {
            const bool png = std::string(kind) == "png", pro = std::string(kind) == "prores";
            if (!formatAvailable(ffmpeg, pro ? "prores_4444" : png ? "h264" : "vp9", png ? "png" : pro ? "mov" : "webm")) continue;
            Settings st = base(pro ? "prores_4444" : png ? "h264" : "vp9", png ? "png" : pro ? "mov" : "webm", png ? 8 : pro ? 10 : 8);
            std::wstring e, problem;
            std::string logText;
            if (!renderOne(alphaShader, st, e, logText)) {
                problem = e.empty() ? TX("selftest.render_failed") : e;
            } else {
                const std::wstring first = outputFile(alphaShader, st, false);
                const std::wstring file = png ? sequencePattern(first) : first;
                const std::string px = decodeRaw(file, L"rgba", std::wstring(png ? L"" : (!pro ? L"-c:v libvpx-vp9" : L"")));
                if (px.size() < 64 * 64 * 4) problem = bad(std::string(kind) + " no frame");
                else {
                    const size_t i = (32 * 64 + 32) * 4;  // alpha = (32.5 / 64) * 255 = 129
                    const int a = (uint8_t)px[i + 3], r = (uint8_t)px[i], g = (uint8_t)px[i + 1];
                    const int tol = png ? 3 : 10;
                    if (std::abs(a - 129) > tol) problem = bad(std::string(kind) + " alpha " + std::to_string(a));
                    else if (png && (r < 253 || std::abs(g - 128) > 3)) problem = bad("straight color " + std::to_string(r) + "," + std::to_string(g));
                }
            }
            check(png ? "selftest.fmt_alpha_png" : pro ? "selftest.fmt_alpha_prores" : "selftest.fmt_alpha_vp9", problem);
        }
        // transparence avec supersampling x2 et flou de mouvement x4 : alpha 0.5 constant, couleur d'origine (comptabilisation premultipliee)
        if (formatAvailable(ffmpeg, "h264", "png")) {
            const std::wstring flat = put(L"alpha_flat.glsl", "// alpha=1\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(0.2, 0.4, 0.6, 0.5); }\n");
            Settings st = base("h264", "png", 8);
            st.ss = 2;
            st.blur = 4;
            std::wstring e, problem;
            std::string logText;
            if (!renderOne(flat, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                const std::string px = decodeRaw(sequencePattern(outputFile(flat, st, false)), L"rgba", L"");
                if (px.size() < 4) problem = bad("no frame");
                else {
                    const int r = (uint8_t)px[0], g = (uint8_t)px[1], b = (uint8_t)px[2], a = (uint8_t)px[3];
                    if (std::abs(r - 51) > 3 || std::abs(g - 102) > 3 || std::abs(b - 153) > 3 || std::abs(a - 128) > 3)
                        problem = bad("rgba " + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," + std::to_string(a));
                }
            }
            check("selftest.fmt_alpha_blur", problem);
        }
        // combinaisons refusees avant le rendu, avec un message
        {
            struct Case {
                const char* codec;
                const char* container;
                const char* extra;  // reglages ajoutes (cle=valeur)
                const char* what;
            };
            const Case cases[] = {{"h264", "mp4", "alpha=1", "alpha / H.264"},   {"prores_422", "mov", "alpha=1", "alpha / ProRes 422"},
                                  {"h265", "mp4", "hdr=pq|alpha=1", "hdr + alpha"}, {"h264", "mp4", "hdr=pq", "hdr / H.264"},
                                  {"vp9", "webm", "alpha=1|interp=20", "interp + alpha"}, {"h264", "mp4", "scale=banane", "scale invalide"}};
            std::wstring problem;
            for (const Case& c : cases) {
                Settings st = base(c.codec, c.container, 8);
                std::string rest = c.extra;
                while (!rest.empty()) {
                    const size_t bar = rest.find('|');
                    const std::string kv = rest.substr(0, bar);
                    const size_t eq = kv.find('=');
                    applySetting(st, kv.substr(0, eq), kv.substr(eq + 1));
                    rest = bar == std::string::npos ? "" : rest.substr(bar + 1);
                }
                std::wstring e;
                std::string logText;
                if (renderOne(renderShader, st, e, logText) || e.empty()) {
                    problem = bad(std::string("accepted: ") + c.what);
                    break;
                }
            }
            check("selftest.fmt_out_refused", problem);
        }
        // HDR10 et HLG : etiquettes BT.2020, 10 bits, valeur du gris 0.5 apres sRGB -> lineaire -> PQ / HLG, luminances declarees
        if (formatAvailable(ffmpeg, "h265", "mp4")) {
            const std::wstring grey = put(L"hdr_grey.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.5, 0.5, 0.5, 1.0); }\n");
            auto lin = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
            auto pq = [](double L) {
                const double p = std::pow(L, 0.1593017578125);
                return std::pow((0.8359375 + 18.8515625 * p) / (1.0 + 18.6875 * p), 78.84375);
            };
            for (const char* mode : {"pq", "hlg"}) {
                Settings st = base("h265", "mp4", 10);
                st.hdr = mode;
                std::wstring e, problem;
                std::string logText;
                if (!renderOne(grey, st, e, logText)) {
                    problem = e.empty() ? TX("selftest.render_failed") : e;
                } else {
                    const std::wstring out = outputFile(grey, st, false);
                    const std::string info = probeInfo(ffmpeg, out);
                    const std::string px = decodeRaw(out, L"yuv420p10le", L"");
                    double v;
                    if (std::string(mode) == "pq") v = pq(lin(0.5) * 203.0 / 10000.0);
                    else {
                        const double E = lin(0.5) * 0.265;
                        v = E <= 1.0 / 12.0 ? std::sqrt(3.0 * E) : 0.17883277 * std::log(12.0 * E - 0.28466892) + 0.55991073;
                    }
                    const double want = 64.0 + 876.0 * v;
                    const size_t i = (32 * 64 + 32) * 2;
                    const double got = px.size() > i + 1 ? (uint8_t)px[i] + 256.0 * (uint8_t)px[i + 1] : -1;
                    const char* trc = std::string(mode) == "pq" ? "smpte2084" : "arib-std-b67";
                    if (info.find("yuv420p10le") == std::string::npos || info.find("bt2020nc") == std::string::npos || info.find(trc) == std::string::npos)
                        problem = bad("tags: " + info.substr(info.find("Video:") == std::string::npos ? 0 : info.find("Video:"), 120));
                    else if (std::fabs(got - want) > 6.0) problem = bad("luma " + std::to_string(got) + " != " + std::to_string(want));
                    else if (std::string(mode) == "pq") {  // luminances declarees : MaxCLL / MaxFALL lues par le decodeur
                        std::string si;
                        runProcess(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -i \"" + out + L"\" -vf showinfo -frames:v 1 -f null -",
                                   [&](const std::wstring& l) { si += U8(l) + "\n"; });
                        if (lower(si).find("maxcll=1000") == std::string::npos) problem = bad("no MaxCLL");
                    }
                }
                check(std::string(mode) == "pq" ? "selftest.fmt_hdr_pq" : "selftest.fmt_hdr_hlg", problem);
            }
        }
        // mise a l'echelle : taille de sortie (facteur et WxH) et filtre (voisin le plus proche : quatre couleurs exactement)
        if (formatAvailable(ffmpeg, "h264", "png")) {
            const std::wstring quad = put(L"quad.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(p.x < 8.0 ? 1.0 : 0.0, p.y < 8.0 ? 1.0 : 0.0, 0.0, 1.0); }\n");
            std::wstring problem;
            for (const char* filt : {"neighbor", "bilinear"}) {
                Settings st = base("h264", "png", 8);
                st.width = st.height = 16;
                st.scale = "64x64";
                st.scaleFilter = filt;
                std::wstring e;
                std::string logText;
                if (!renderOne(quad, st, e, logText)) {
                    problem = e.empty() ? TX("selftest.render_failed") : e;
                    break;
                }
                const std::wstring pat = sequencePattern(outputFile(quad, st, false));
                const std::string px = decodeRaw(pat, L"rgb24", L"");
                std::set<std::string> colors;
                for (size_t i = 0; i + 2 < px.size(); i += 3) colors.insert(px.substr(i, 3));
                if (px.size() != 64 * 64 * 3) problem = bad(std::string("size ") + std::to_string(px.size()));
                else if (std::string(filt) == "neighbor" && colors.size() != 4) problem = bad("neighbor colors " + std::to_string(colors.size()));
                else if (std::string(filt) == "bilinear" && colors.size() <= 4) problem = bad("bilinear colors " + std::to_string(colors.size()));
                if (!problem.empty()) break;
            }
            if (problem.empty()) {  // facteur
                Settings st = base("h264", "png", 8);
                st.scale = "2x";
                std::wstring e;
                std::string logText;
                if (!renderOne(quad, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else if (probeInfo(ffmpeg, sequencePattern(outputFile(quad, st, false))).find(" 128x128") == std::string::npos) problem = bad("2x size");
            }
            check("selftest.fmt_scale", problem);
        }
        // interpolation d'images : rendu a 10 img/s, sortie a 20 img/s, meme duree
        if (formatAvailable(ffmpeg, "h264", "mp4")) {
            Settings st = base("h264", "mp4", 8);
            st.duration = 1.0;
            st.interp = 20;
            std::wstring e, problem;
            std::string logText;
            if (!renderOne(renderShader, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                const std::string info = probeInfo(ffmpeg, outputFile(renderShader, st, false));
                const double d = probeDuration(info);
                if (info.find(" 20 fps") == std::string::npos) problem = bad("fps: " + info.substr(info.find("Video:") == std::string::npos ? 0 : info.find("Video:"), 120));
                else if (d < 0.85 || d > 1.3) problem = bad("duration " + std::to_string(d));
            }
            check("selftest.fmt_interp", problem);
        }
        // normalisation du son : un sinus de 10 s ressort a -16 LUFS (+/- 3) avec loudnorm=-16
        if (formatAvailable(ffmpeg, "h264", "mp4")) {
            const std::wstring tone = outDir + sep + L"tone10.wav";
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -f lavfi -i sine=frequency=440:duration=10 -ar 44100 -ac 1 \"" + tone + L"\"",
                       nullptr);
            Settings st = base("h264", "mp4", 8);
            st.fps = 5;
            st.duration = 10;
            st.audio = tone;
            st.loudnorm = -16;
            std::wstring e, problem;
            std::string logText;
            if (!fileExists(tone)) problem = bad("no tone");
            else if (!renderOne(renderShader, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                std::string summary;
                runProcess(L"\"" + ffmpeg + L"\" -hide_banner -nostats -nostdin -i \"" + outputFile(renderShader, st, false) + L"\" -af ebur128 -f null -",
                           [&](const std::wstring& l) { summary += U8(l) + "\n"; });
                const size_t p = summary.rfind("I:");
                double lufs = 0;
                if (p == std::string::npos || sscanf(summary.c_str() + p + 2, "%lf", &lufs) != 1) problem = bad("no loudness");
                else if (std::fabs(lufs + 16.0) > 3.0) problem = bad("loudness " + std::to_string(lufs));
            }
            check("selftest.fmt_loudnorm", problem);
        }
        // apercu avec transparence : le damier gris se voit ou l'alpha est presque nul (a gauche), la couleur ou il est presque plein (a droite)
        {
            Settings st;
            st.width = st.height = 64;
            std::vector<uint8_t> got;
            Hooks H;
            H.log = [](const std::wstring&) {};
            H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { got = std::move(px); };
            std::wstring e, problem;
            if (convertFile(alphaShader, exeDir, st, H, e) != Result::Ok || got.size() != 64 * 64 * 4) {
                problem = e.empty() ? TX("selftest.render_failed") : e;
            } else {
                auto at = [&](int x, int y, int c) { return (int)got[((size_t)y * 64 + x) * 4 + c]; };  // BGRA
                // (1, 1) : alpha 0.02 sur la case claire du damier... la premiere case (0..7) est la plus sombre (150)
                const bool left = std::abs(at(1, 1, 2) - 150) < 8 && std::abs(at(1, 1, 1) - 150) < 8 && std::abs(at(1, 1, 0) - 150) < 8;
                const bool right = at(62, 1, 2) > 245 && at(62, 1, 1) > 120 && at(62, 1, 1) < 136 && at(62, 1, 0) < 12;
                if (!left || !right) problem = bad("checkerboard / color");
            }
            check("selftest.fmt_alpha_preview", problem);
        }
        {  // champs de la fenetre « Sortie » : aller-retour avec Settings, valeurs invalides refusees sans rien modifier
            std::wstring problem, e;
            Settings s0;
            s0.width = 1280;
            s0.height = 720;
            OutputFields f;
            outputFieldsFrom(s0, f);
            f.alpha = true;
            f.hdr = 1;
            f.hdrWhite = L"150";
            f.scale = L"2x";
            f.scaleFilter = 4;
            f.interp = L"60";
            f.loudnorm = L"-16";
            f.title = L"Titre";
            f.titleDur = L"4,5";
            Settings s1 = s0;
            if (!outputFieldsApply(f, s1, e) || !s1.alpha || s1.hdr != "pq" || s1.hdrWhite != 150 || s1.scale != "2x" || s1.scaleFilter != "neighbor" ||
                s1.interp != 60 || s1.loudnorm != -16 || s1.title != L"Titre" || s1.titleDur != 4.5) {
                problem = bad("apply");
            } else {
                OutputFields back;
                outputFieldsFrom(s1, back);
                Settings s2 = s0;
                if (!outputFieldsApply(back, s2, e) || s2.scale != s1.scale || s2.interp != s1.interp || s2.title != s1.title || s2.loudnorm != s1.loudnorm)
                    problem = bad("round trip");
            }
            for (int k = 0; k < 5 && problem.empty(); k++) {
                OutputFields v = f;
                if (k == 0) v.scale = L"abc";
                else if (k == 1) v.hdrPeak = L"5";
                else if (k == 2) v.interp = L"999";
                else if (k == 3) v.loudnorm = L"3";
                else v.titleDur = L"0";
                Settings s3 = s0;
                if (outputFieldsApply(v, s3, e) || s3.alpha || s3.title != s0.title || e.empty()) problem = bad("invalid " + std::to_string(k));
            }
            if (problem.empty()) {  // reglages avances : nom, fichier existant, fin de lot, parties, file GPU, obturateur
                OutputFields v;
                outputFieldsFrom(s0, v);
                v.name = L"{shader}_{w}x{h}";
                v.exists = 1;
                v.endAction = 2;
                v.chunk = L"12,5";
                v.pbo = L"5";
                v.shutter = L"0,25";
                v.metadata = false;
                Settings s4 = s0;
                if (!outputFieldsApply(v, s4, e) || s4.name != L"{shader}_{w}x{h}" || s4.exists != "number" || s4.endaction != "sleep" || s4.chunk != 12.5 || s4.pbo != 5 ||
                    s4.shutter != 0.25 || s4.metadata)
                    problem = bad("advanced");
                v.pbo = L"9";
                Settings s5 = s0;
                if (problem.empty() && (outputFieldsApply(v, s5, e) || s5.pbo != s0.pbo)) problem = bad("pbo");
            }
            check("selftest.fmt_outputopts", problem);
        }
        // rapport de rendu : JSON et texte a cote de la video, avec images, taille, etat ; un rendu refuse produit un rapport d'echec
        if (formatAvailable(ffmpeg, "h264", "mp4")) {
            std::wstring problem, e;
            std::string logText;
            const std::wstring shader = put(L"report_src.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(p / iResolution.xy, 0.5, 1.0); }\n");
            Settings st = base("h264", "mp4", 8);
            st.report = "json";
            st.name = L"report_json";
            std::string json;
            if (!renderOne(shader, st, e, logText)) {
                problem = e.empty() ? TX("selftest.render_failed") : e;
            } else if (!readFile(outDir + sep + L"report_json.report.json", json)) {
                problem = bad("report.json");
            } else if (json.find("\"status\": \"ok\"") == std::string::npos || json.find("\"frames\": 5") == std::string::npos || json.find("\"width\": 64") == std::string::npos ||
                       json.find("\"container\": \"mp4\"") == std::string::npos || json.find("\"bytes\": 0") != std::string::npos || json.find("\"warnings\": [") == std::string::npos) {
                problem = bad("json content");
            }
            if (problem.empty()) {
                st.report = "text";
                st.name = L"report_text";
                std::string text;
                if (!renderOne(shader, st, e, logText) || !readFile(outDir + sep + L"report_text.report.txt", text)) problem = bad("report.txt");
                else if (text.find(U8(TX("log.report.title"))) == std::string::npos || text.find(U8(TX("log.report.status_ok"))) == std::string::npos) problem = bad("text content");
            }
            if (problem.empty()) {  // echec : le conteneur refuse le codec -> rapport d'echec avec le message d'erreur
                st.report = "json";
                st.name = L"report_fail";
                st.codec = "prores_hq";
                std::string fail;
                if (renderOne(shader, st, e, logText)) problem = bad("failure expected");
                else if (!readFile(outDir + sep + L"report_fail.report.json", fail) || fail.find("\"status\": \"failed\"") == std::string::npos || fail.find("\"error\"") == std::string::npos)
                    problem = bad("failure report");
            }
            check("selftest.fmt_report", problem);
        }
        {  // reglages propres a un shader : difference avec les reglages generaux, fichier jobs/, ecriture dans le shader (CRLF conserve)
            std::wstring problem, e;
            const std::wstring fsShader = put(L"fs_shader.glsl", "// fps=24\r\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(1.0); }\r\n");
            Settings general;
            Settings edited = general;
            edited.fps = 30;
            edited.width = 640;
            edited.audio = W("C:/musique avec espaces.wav");
            const std::vector<Override> diff = diffMainSettings(general, edited);
            Settings applied = general;
            applyOverrides(applied, diff);
            if (diff.size() != 3 || !isOverridden(diff, "fps") || !isOverridden(diff, "width") || !isOverridden(diff, "audio") || applied.fps != 30 ||
                applied.width != 640 || applied.audio != edited.audio) {
                problem = bad("diff");
            } else {
                saveFileSettings(outDir, fsShader, diff);
                const Settings reloaded = settingsForFile(general, outDir, fsShader);
                if (!fileSettingsExist(outDir, fsShader) || reloaded.fps != 30 || reloaded.audio != edited.audio) problem = bad("jobs file");
                removeFileSettings(outDir, fsShader);
                if (problem.empty() && fileSettingsExist(outDir, fsShader)) problem = bad("remove");
            }
            if (problem.empty()) {
                std::string text;
                std::vector<Override> toWrite = {{"fps", "30"}, {"width", "640"}};
                if (!writeSettingsIntoShader(fsShader, toWrite, e) || !readFile(fsShader, text)) problem = bad("write");
                else if (text != "// width=640\r\n// fps=30\r\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(1.0); }\r\n") problem = bad("content " + std::to_string(text.size()));
            }
            check("selftest.fmt_filesettings", problem);
        }
        {  // fichier depose sur la fenetre : nature d'apres l'extension, ligne iChannelN (chemin relatif au shader quand le fichier s'y trouve)
            std::wstring problem;
            const Override a = channelOverride(L"C:/a/b/shader.glsl", L"C:\\A\\B\\tex\\wood.png", DropKind::Image, 1);
            const Override b = channelOverride(L"C:/a/b/shader.glsl", L"D:/musique/boucle.wav", DropKind::Sound, 0);
            const Override c = channelOverride(L"C:/a/b/shader.glsl", L"C:/a/b/clip.mp4", DropKind::Video, 9);
            if (classifyDropped(L"x.PNG") != DropKind::Image || classifyDropped(L"y.wav") != DropKind::Sound || classifyDropped(L"z.mp4") != DropKind::Video ||
                classifyDropped(L"s.glsl") != DropKind::Shader || classifyDropped(L"archive.zip") != DropKind::Other)
                problem = bad("kind");
            else if (a.key != "iChannel1" || a.value != "tex/wood.png" || b.key != "iChannel0" || b.value != "audio:D:/musique/boucle.wav" || c.key != "iChannel3" ||
                     c.value != "video:clip.mp4")
                problem = bad(a.key + "=" + a.value + " " + b.value + " " + c.value);
            check("selftest.fmt_channeldrop", problem);
        }
        {  // editeur : coloration GLSL (nature de chaque plage) et squelettes des passes (ils compilent et se rendent)
            std::wstring problem, e;
            const std::wstring code = W("float x = 1.5; // note\n#define A 1\n// fps=30\nvec3 c = iTime + sin(x);\n/* a\nb */");
            const std::vector<SyntaxSpan> spans = highlightGlsl(code);
            auto kindOf = [&](const char* word) {
                const size_t at = code.find(W(word));
                for (const SyntaxSpan& s : spans)
                    if ((size_t)s.start == at) return s.kind;
                return SyntaxKind::Plain;
            };
            if (kindOf("float") != SyntaxKind::Type || kindOf("1.5") != SyntaxKind::Number || kindOf("// note") != SyntaxKind::Comment ||
                kindOf("#define") != SyntaxKind::Preprocessor || kindOf("// fps=30") != SyntaxKind::Directive || kindOf("iTime") != SyntaxKind::Uniform ||
                kindOf("sin") != SyntaxKind::Function || kindOf("/* a") != SyntaxKind::Comment || kindOf("x =") != SyntaxKind::Plain)
                problem = bad("syntax");
            if (problem.empty()) {
                const std::wstring imageFile = put(L"tpl.glsl", "// iChannel1=bufferA\nvoid mainImage(out vec4 o, in vec2 p){ o = texture(iChannel1, p / iResolution.xy); }\n");
                std::vector<PassTab> tabs = passTabs(imageFile);
                int created = 0;
                for (PassTab& t : tabs)
                    if (t.id == "bufferA" || t.id == "common" || t.id == "post") {
                        std::wstring perr;
                        if (t.exists || !createPassFile(t, perr)) problem = bad("create " + t.id);
                        created++;
                    }
                if (problem.empty() && (tabs.size() != 9 || created != 3 || !passTabs(imageFile)[1].exists)) problem = bad("tabs");
                if (problem.empty()) {
                    Settings st = base("h264", "mp4", 8);
                    std::vector<uint8_t> got;
                    Hooks H;
                    H.log = [](const std::wstring&) {};
                    H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { got = std::move(px); };
                    if (convertFile(imageFile, exeDir, st, H, e) != Result::Ok || got.size() != 64 * 64 * 4) problem = e.empty() ? TX("selftest.render_failed") : e;
                }
            }
            check("selftest.fmt_editor", problem);
        }
        {  // lots et reglages par fichier etendus : numeros de lot retrouves, difference sur la fenetre Sortie et les variantes
            std::wstring problem;
            BatchMap bm;
            bm[L"C:/a/un.glsl"] = 2;
            bm[L"C:/a/deux.glsl"] = 4;
            saveBatches(outDir, bm);
            const BatchMap back = loadBatches(outDir);
            if (back.size() != 2 || batchOf(back, L"C:/a/un.glsl") != 2 || batchOf(back, L"C:/a/deux.glsl") != 4 || batchOf(back, L"C:/a/trois.glsl") != 0 ||
                batchLabel(back, L"C:/a/un.glsl") != L"[2] C:/a/un.glsl")
                problem = bad("batches");
            saveBatches(outDir, BatchMap());
            if (problem.empty()) {
                Settings general;
                Settings edited = general;
                edited.alpha = true;
                edited.hdr = "pq";
                edited.title = W("Titre");
                edited.scale = "2x";
                edited.variants = {"720p:width=1280|height=720"};
                const std::vector<Override> diff = diffMainSettings(general, edited);
                Settings applied = general;
                applied.variants = {"ancienne:crf=30"};
                applyFileOverrides(applied, diff);
                if (!applied.alpha || applied.hdr != "pq" || applied.title != W("Titre") || applied.scale != "2x" || applied.variants.size() != 1 || applied.variants[0] != "720p:width=1280|height=720")
                    problem = bad("diff output");
                Settings none = edited;
                none.variants.clear();
                Settings cleared = edited;
                cleared.variants = {"x:crf=1"};
                applyFileOverrides(cleared, diffMainSettings(edited, none));
                if (problem.empty() && !cleared.variants.empty()) problem = bad("variants cleared");
            }
            check("selftest.fmt_batches", problem);
        }
        {  // reglages imposes par le shader (// cle=valeur) : liste, cles reconnues, valeurs appliquees (les champs de la fenetre les montrent grises)
            std::wstring problem;
            const std::wstring forcedShader = put(L"forced.glsl", "// fps=24\n// width=640\n// title=Mon titre\n// pas un reglage = 3\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(1.0); }\n");
            const std::vector<Override> list = shaderOverrides(forcedShader);
            Settings s0;
            s0.fps = 60;
            s0.width = 1920;
            Settings s1 = s0;
            applyOverrides(s1, list);
            if (list.size() != 3 || !isOverridden(list, "fps") || !isOverridden(list, "width") || !isOverridden(list, "title") || isOverridden(list, "height"))
                problem = bad("list");
            else if (s1.fps != 24 || s1.width != 640 || s1.title != W("Mon titre") || s1.height != s0.height)
                problem = bad("apply");
            check("selftest.fmt_forced", problem);
        }
        // planche-contact 3x2 et vignette : le rouge suit iTime (duree 6 s), chaque vignette est tiree au milieu de son intervalle (0.5, 1.5, ... 5.5 s)
        if (formatAvailable(ffmpeg, "h264", "png")) {
            const std::wstring ramp = put(L"time_ramp.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(iTime / 6.0, 0.0, 0.0, 1.0); }\n");
            std::wstring problem;
            Settings st = base("h264", "png", 8);
            st.width = 320;
            st.height = 180;
            st.duration = 6;
            st.fps = 10;
            {
                Hooks H;
                H.log = [](const std::wstring&) {};
                H.sheetCols = 3;
                H.sheetRows = 2;
                std::wstring e;
                if (convertFile(ramp, exeDir, st, H, e) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::wstring sheet = outputPath(ramp, st.outdir, L"_sheet.png");
                    const std::string px = decodeRaw(sheet, L"rgb24", L"");
                    // 3 x 320 + 2 x 4 de marge entre les vignettes + 2 x 4 de bord = 976 ; 2 x 180 + 4 + 8 = 372
                    if (px.size() != (size_t)976 * 372 * 3) problem = bad("sheet size " + std::to_string(px.size()));
                    else
                        for (int k = 0; k < 6 && problem.empty(); k++) {
                            const int cx = 4 + (k % 3) * 324 + 160, cy = 4 + (k / 3) * 184 + 90;
                            const int red = (uint8_t)px[((size_t)cy * 976 + cx) * 3];
                            const int want = (int)std::lround((k + 0.5) / 6.0 * 255.0);
                            if (std::abs(red - want) > 4) problem = bad("tile " + std::to_string(k) + " red " + std::to_string(red) + " != " + std::to_string(want));
                        }
                }
            }
            if (problem.empty()) {  // vignette a 3 s : rouge 0.5
                Hooks H;
                H.log = [](const std::wstring&) {};
                H.snapshot = true;
                H.thumbTime = 3.0;
                std::wstring e;
                if (convertFile(ramp, exeDir, st, H, e) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::string px = decodeRaw(outputPath(ramp, st.outdir, L"_thumb.png"), L"rgb24", L"");
                    if (px.size() != (size_t)320 * 180 * 3 || std::abs((int)(uint8_t)px[0] - 128) > 3) problem = bad("thumb " + std::to_string(px.size()));
                }
            }
            check("selftest.fmt_sheet", problem);
        }
        // montage : trois couleurs enchainees (coupe, fondu enchaine, fondu par le noir) ; duree 2 + 1.5 + 2 + 1 = 6.5 s, couleurs aux bons instants
        if (formatAvailable(ffmpeg, "h264", "mp4")) {
            put(L"m_red.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(1.0, 0.0, 0.0, 1.0); }\n");
            put(L"m_green.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.0, 1.0, 0.0, 1.0); }\n");
            put(L"m_blue.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.0, 0.0, 1.0, 1.0); }\n");
            const std::wstring montage = put(L"demo_mix.montage",
                                             "# montage de test\nset width=64\nset height=64\nset fps=10\nset codec=h264\nset container=mp4\nset crf=18\nset preset=ultrafast\n"
                                             "duration=2\nm_red.glsl\n\"m_green.glsl\" 2 crossfade 0.5\nm_blue.glsl 2 cut\nm_red.glsl 2 fade 1\n");
            Settings st = base("h264", "mp4", 8);
            st.name = L"";
            st.audio = L"";
            std::wstring e, problem;
            std::string logText;
            if (!renderOne(montage, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                const std::wstring out = outputFile(montage, st, false);
                const double d = probeDuration(probeInfo(ffmpeg, out));
                if (d < 6.35 || d > 6.65) problem = bad("duration " + std::to_string(d));
                else {
                    struct At {
                        double t;
                        int r, g, b, tol;
                    };
                    for (const At& a : {At{0.3, 255, 0, 0, 12}, At{3.0, 0, 255, 0, 12}, At{4.2, 0, 0, 255, 12}, At{5.0, 0, 0, 0, 150}, At{6.4, 255, 0, 0, 12}}) {  // 5.0 s : milieu du fondu par le noir (rouge fonce, ni bleu ni vert)
                        const std::string px = decodeRaw(out, L"rgb24", wfmt(L"-ss %.2f", a.t));
                        if (px.size() < 3) {
                            problem = bad("no frame");
                            break;
                        }
                        if (std::abs((uint8_t)px[0] - a.r) > a.tol || std::abs((uint8_t)px[1] - a.g) > a.tol || std::abs((uint8_t)px[2] - a.b) > a.tol) {
                            problem = bad("t=" + std::to_string(a.t) + " rgb " + std::to_string((uint8_t)px[0]) + "," + std::to_string((uint8_t)px[1]) + "," +
                                          std::to_string((uint8_t)px[2]));
                            break;
                        }
                    }
                }
                if (problem.empty() && platform::dirExists(out.substr(0, out.find_last_of(L'.')) + L".clips")) problem = bad("clips folder left");
            }
            check("selftest.fmt_montage", problem);
            // erreurs de lecture : transition inconnue, clip absent, fichier vide
            std::wstring problem2;
            const char* bads[] = {"m_red.glsl 2 balayage\n", "absent.glsl 3\n", "# rien\n"};
            for (const char* body : bads) {
                const std::wstring bm = put(L"bad.montage", body);
                MontageDef def;
                std::wstring pe;
                if (parseMontage(bm, 2.0, def, pe) || pe.empty()) {
                    problem2 = bad(std::string("accepted: ") + body);
                    break;
                }
            }
            check("selftest.fmt_montage_errors", problem2);
        }
        // texte incruste : le titre apparait au centre (image a 0.3 s), absent sans reglage ; ignore si ce ffmpeg n'a pas drawtext ou s'il manque une police
        if (formatAvailable(ffmpeg, "h264", "png") && ffmpegHasDrawtext(ffmpeg) && !platform::systemFontFile().empty()) {
            std::wstring problem;
            long long lit[2] = {0, 0};
            for (int withTitle = 0; withTitle < 2 && problem.empty(); withTitle++) {
                const std::wstring black = put(L"title_black.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.0, 0.0, 0.0, 1.0); }\n");
                Settings st = base("h264", "png", 8);
                st.width = st.height = 128;
                if (withTitle) {
                    st.title = L"HELLO";
                    st.credit = W("credit: by it's 100% me, ok");  // deux-points, apostrophe, pourcentage et virgule : echappements de drawtext
                }
                std::wstring e;
                std::string logText;
                if (!renderOne(black, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::string px = decodeRaw(sequencePattern(outputFile(black, st, false)), L"gray", L"-framerate 10 -ss 0.3");
                    if (px.size() != 128 * 128) problem = bad("frame size " + std::to_string(px.size()));
                    else
                        for (unsigned char c : px)
                            if (c > 100) lit[withTitle]++;
                }
            }
            if (problem.empty() && (lit[0] != 0 || lit[1] < 30)) problem = bad("lit pixels " + std::to_string(lit[0]) + " / " + std::to_string(lit[1]));
            check("selftest.fmt_overlay", problem);
        }
        // --- Temps et boucle (phase 16) : courbe de temps, ralenti, boucles sans couture, recherche de periode, image fixe moyennee, vignette intelligente ---
        {
            // courbe de temps : points, vitesse lisse, marche arriere, aller-retour, erreurs, vitesse constante
            {
                std::wstring problem;
                TimeMap tm;
                std::wstring pe;
                auto approx = [](double a, double b) { return std::fabs(a - b) < 1e-6; };
                if (!TimeMap::parse("0:0;4:2;8:10", tm, pe)) problem = bad("parse " + U8(pe));
                else if (!approx(tm.at(0), 0) || !approx(tm.at(4), 2) || !approx(tm.at(8), 10) || !tm.monotone() || tm.identity()) problem = bad("points");
                else if (std::fabs(tm.speedAt(4 - 1e-6) - tm.speedAt(4 + 1e-6)) > 1e-3) problem = bad("speed jump at a point");
                else if (tm.at(12) < 10) problem = bad("after the last point");
                if (problem.empty()) {
                    TimeMap back, hold, pp, slow;
                    if (!TimeMap::parse("0:5;4:0", back, pe) || !approx(back.at(4), 0) || back.monotone()) problem = bad("reverse");
                    else if (!TimeMap::parse("0:0;2:1;4:1;6:3", hold, pe) || !approx(hold.at(3), 1) || !approx(hold.speedAt(3), 0)) problem = bad("freeze");
                    else {
                        pp = TimeMap::pingPong(2);
                        if (!approx(pp.at(1), 1) || !approx(pp.at(2), 2) || !approx(pp.at(3), 1) || !approx(pp.at(4), 0) || !approx(pp.at(5), 1) || pp.monotone()) problem = bad("ping-pong");
                        slow.setSpeed(0.25);
                        if (!approx(slow.at(4), 1) || slow.identity()) problem = bad("speed");
                    }
                }
                if (problem.empty()) {  // textes refuses avec un message
                    for (const char* badSpec : {"1;2", "0:0;0:1", "0:0", "a:b", "0:0;-1:2"}) {
                        TimeMap t2;
                        std::wstring e2;
                        if (TimeMap::parse(badSpec, t2, e2) || e2.empty()) {
                            problem = bad(std::string("accepted ") + badSpec);
                            break;
                        }
                    }
                }
                if (problem.empty()) {  // reglages : lus du shader, ecrits dans config.ini, signature des parties
                    Settings s1;
                    applySetting(s1, "timemap", "0:0;4:2");
                    applySetting(s1, "loopmode", "pingpong");
                    applySetting(s1, "loopfade", "2.5");
                    applySetting(s1, "speed", "0.5");
                    applySetting(s1, "samples", "16");
                    const std::wstring ini = outDir + sep + L"p16.ini";
                    writeIni(ini, s1);
                    std::string txt;
                    readFile(ini, txt);
                    Settings s2;
                    parseLines(s2, txt, false);
                    if (s2.timemap != "0:0;4:2" || s2.loopMode != "pingpong" || s2.loopFade != 2.5 || s2.speed != 0.5 || s2.samples != 16 || !sameSettings(s1, s2))
                        problem = bad("settings round trip");
                    else if (!isSettingKey("timemap") || !isSettingKey("loopmode") || !isSettingKey("loopfade") || !isSettingKey("speed") || !isSettingKey("samples"))
                        problem = bad("setting keys");
                }
                if (problem.empty()) {  // champs de la fenetre Sortie (temps et boucle) : aller-retour avec les reglages, valeurs invalides refusees
                    Settings so;
                    OutputFields f;
                    outputFieldsFrom(so, f);
                    f.loopMode = 2;
                    f.loopFade = L"2,5";
                    f.timemap = L"0:0; 4:2";
                    f.speed = L"0,5";
                    f.samples = L"32";
                    std::wstring oe;
                    if (!outputFieldsApply(f, so, oe) || so.loopMode != "pingpong" || so.loopFade != 2.5 || so.timemap != "0:0;4:2" || so.speed != 0.5 || so.samples != 32)
                        problem = bad("fields");
                    else {
                        for (int k = 0; k < 4 && problem.empty(); k++) {
                            OutputFields g1 = f;
                            if (k == 0) g1.speed = L"0";
                            else if (k == 1) g1.loopFade = L"0.01";
                            else if (k == 2) g1.samples = L"0";
                            else g1.timemap = L"1;2";
                            const Settings before = so;
                            std::wstring ee;
                            if (outputFieldsApply(g1, so, ee) || ee.empty() || !sameSettings(before, so)) problem = bad("invalid field accepted " + std::to_string(k));
                        }
                    }
                }
                check("selftest.fmt_timemap", problem);
            }
            const std::wstring ramp = put(L"p16_ramp.glsl", "void mainImage(out vec4 o, in vec2 p){ float v = clamp(iTime / 4.0, 0.0, 1.0); o = vec4(v, v, v, 1.0); }\n");
            // niveau de gris du centre de l'image a l'instant t d'une sequence PNG, -1 si illisible
            auto grayAt = [&](const std::wstring& pattern, double t) {
                const std::string px = decodeRaw(pattern, L"gray", wfmt(L"-framerate 10 -ss %.2f", t));
                return px.size() == 64 * 64 ? (int)(uint8_t)px[32 * 64 + 32] : -1;
            };
            auto seqSettings = [&](double duration) {
                Settings st = base("h264", "png", 8);
                st.duration = duration;
                return st;
            };
            // vrai ralenti : chaque image est calculee a l'instant exact du shader (vitesse 0.25), le flou de mouvement suit la vitesse
            if (formatAvailable(ffmpeg, "h264", "png")) {
                std::wstring problem;
                Settings st = seqSettings(2);
                st.speed = 0.25;
                std::wstring e;
                std::string logText;
                if (!renderOne(ramp, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::wstring pat = sequencePattern(outputFile(ramp, st, false));
                    const int a = grayAt(pat, 1.0), b = grayAt(pat, 1.1), c = grayAt(pat, 1.9);  // temps shader 0.25, 0.275, 0.475
                    if (std::abs(a - 16) > 2 || std::abs(b - 17) > 2 || std::abs(c - 30) > 2 || a == b) problem = bad("frames " + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c));
                }
                if (problem.empty()) {  // flou : 4 sous-images sur un obturateur plein, pente 2.5 par seconde
                    const std::wstring fast = put(L"p16_fast.glsl", "void mainImage(out vec4 o, in vec2 p){ float v = clamp(iTime * 2.5, 0.0, 1.0); o = vec4(v, v, v, 1.0); }\n");
                    int got[2] = {0, 0};
                    for (int k = 0; k < 2 && problem.empty(); k++) {
                        Settings sb = seqSettings(0.5);
                        sb.blur = 4;
                        sb.shutter = 1.0;
                        sb.speed = k ? 0.25 : 1.0;
                        if (!renderOne(fast, sb, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                        else got[k] = grayAt(sequencePattern(outputFile(fast, sb, false)), 0.0);
                    }
                    if (problem.empty() && (std::abs(got[0] - 24) > 3 || std::abs(got[1] - 6) > 3)) problem = bad("blur " + std::to_string(got[0]) + "," + std::to_string(got[1]));
                }
                check("selftest.fmt_slowmo", problem);
            }
            // boucle par fondu : la derniere image rejoint la premiere ; couture controlee sur la video et dans le rapport
            if (formatAvailable(ffmpeg, "h264", "png") && formatAvailable(ffmpeg, "h264", "mp4")) {
                std::wstring problem;
                Settings st = seqSettings(4);
                st.loopMode = "crossfade";
                st.loopFade = 1.0;
                std::wstring e;
                std::string logText;
                if (!renderOne(ramp, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::wstring pat = sequencePattern(outputFile(ramp, st, false));
                    const int f0 = grayAt(pat, 0.0), f5 = grayAt(pat, 0.5), f10 = grayAt(pat, 1.0), f20 = grayAt(pat, 2.0), f39 = grayAt(pat, 3.9);
                    if (std::abs(f0 - 255) > 2 || std::abs(f5 - 143) > 3 || std::abs(f10 - 64) > 2 || std::abs(f20 - 128) > 2 || std::abs(f39 - 249) > 3)
                        problem = bad("frames " + std::to_string(f0) + "," + std::to_string(f5) + "," + std::to_string(f10) + "," + std::to_string(f20) + "," + std::to_string(f39));
                }
                for (int withLoop = 1; withLoop >= 0 && problem.empty(); withLoop--) {  // avec le fondu : couture invisible ; sans : la rampe saute de 249 a 0
                    Settings sv = base("h264", "mp4", 8);
                    sv.duration = 4;
                    sv.loopMode = withLoop ? "crossfade" : "none";
                    sv.loop = withLoop ? 0 : 4;  // le controle de couture se lance aussi avec une periode donnee
                    sv.report = "json";
                    sv.crf = 18;
                    if (!renderOne(ramp, sv, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else {
                        const std::wstring file = outputFile(ramp, sv, false);
                        LoopCheck lc;
                        if (!checkLoopSeam(ffmpeg, file, lc)) problem = bad("seam check");
                        else if (lc.perfect != (withLoop == 1)) problem = bad("seam " + std::to_string(lc.seam) + " vs " + std::to_string(lc.typical));
                        else {
                            std::string js;
                            readFile(file.substr(0, file.find_last_of(L'.')) + L".report.json", js);
                            if (js.find(withLoop ? "\"perfect\": true" : "\"perfect\": false") == std::string::npos) problem = bad("report loop");
                        }
                    }
                }
                check("selftest.fmt_loopcross", problem);
            }
            // aller-retour : le temps monte puis redescend ; les passes a historique le refusent, avancer seulement est accepte
            if (formatAvailable(ffmpeg, "h264", "png")) {
                std::wstring problem;
                Settings st = seqSettings(4);
                st.loopMode = "pingpong";
                std::wstring e;
                std::string logText;
                if (!renderOne(ramp, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::wstring pat = sequencePattern(outputFile(ramp, st, false));
                    const int g0 = grayAt(pat, 0.0), g10 = grayAt(pat, 1.0), g20 = grayAt(pat, 2.0), g30 = grayAt(pat, 3.0), g39 = grayAt(pat, 3.9);
                    if (std::abs(g0) > 2 || std::abs(g10 - 64) > 2 || std::abs(g20 - 128) > 2 || std::abs(g30 - 64) > 2 || std::abs(g39 - 6) > 3)
                        problem = bad("frames " + std::to_string(g0) + "," + std::to_string(g10) + "," + std::to_string(g20) + "," + std::to_string(g30) + "," + std::to_string(g39));
                }
                if (problem.empty()) {
                    const std::wstring hist = put(L"p16_hist.glsl", "// iChannel0=bufferA\nvoid mainImage(out vec4 o, in vec2 p){ o = texture(iChannel0, p / iResolution.xy); }\n");
                    put(L"p16_hist.bufferA.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.25, 0.5, 0.75, 1.0); }\n");
                    struct Case {
                        const char* mode;
                        const char* curve;
                        bool ok;
                    };
                    for (const Case& c : {Case{"pingpong", "", false}, Case{"none", "0:2;2:0", false}, Case{"crossfade", "", false}, Case{"none", "0:0;2:1", true}}) {
                        Settings sh = seqSettings(1);
                        sh.loopMode = c.mode;
                        sh.timemap = c.curve;
                        std::wstring e2;
                        std::string log2;
                        const bool ran = renderOne(hist, sh, e2, log2);
                        if (ran != c.ok) {
                            problem = bad(std::string("passes ") + c.mode + " " + c.curve + (ran ? " accepted" : " refused"));
                            break;
                        }
                        if (!ran && e2 != TX(std::string(c.mode) == "crossfade" ? "error.loop_history" : "error.timemap_history")) {
                            problem = bad("passes message");
                            break;
                        }
                    }
                }
                check("selftest.fmt_pingpong", problem);
            }
            // recherche de periode : shader periodique (2.5 s), image fixe, rampe sans retour
            {
                std::wstring problem;
                const std::wstring periodic = put(L"p16_periodic.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.5 + 0.5 * sin(6.2831853 * iTime / 2.5 + p.x * 0.05), 0.5 + 0.5 * cos(6.2831853 * iTime / 2.5 + p.y * 0.05), 0.3, 1.0); }\n");
                const std::wstring still = put(L"p16_still.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(p.x / iResolution.x, 0.4, 0.2, 1.0); }\n");
                Settings sp = base("h264", "png", 8);
                sp.duration = 8;
                Hooks H;
                H.log = [](const std::wstring&) {};
                LoopFound lf;
                std::wstring e;
                if (!findLoop(periodic, exeDir, sp, 0, H, lf, e)) problem = bad("search " + U8(e));
                else if (!lf.found || lf.quality != 2 || std::fabs(lf.period - 2.5) > 0.02) problem = bad("period " + std::to_string(lf.period) + " q" + std::to_string(lf.quality));
                else if (!findLoop(still, exeDir, sp, 0, H, lf, e) || !lf.isStatic) problem = bad("static");
                else {
                    sp.duration = 6;
                    if (!findLoop(ramp, exeDir, sp, 0, H, lf, e) || lf.found) problem = bad("ramp loop " + std::to_string(lf.period));
                }
                check("selftest.fmt_loopfind", problem);
            }
            // image fixe moyennee : le bruit baisse avec le nombre d'echantillons ; une interruption garde le resultat partiel a la bonne echelle
            if (formatAvailable(ffmpeg, "h264", "png")) {
                std::wstring problem;
                const std::wstring noisy = put(L"p16_noisy.glsl", "void mainImage(out vec4 o, in vec2 p){ float r = fract(sin(float(iFrame) * 12.9898 + p.x * 78.233 + p.y * 37.719) * 43758.5453); o = vec4(vec3(r), 1.0); }\n");
                auto spread = [&](const std::string& px, double& mean) {  // moyenne et ecart type des niveaux de gris
                    double s1 = 0, s2 = 0;
                    for (unsigned char c : px) {
                        s1 += c;
                        s2 += (double)c * c;
                    }
                    const double n = (double)px.size();
                    mean = s1 / n;
                    return std::sqrt(std::max(0.0, s2 / n - mean * mean));
                };
                double sd[3] = {0, 0, 0}, mean[3] = {0, 0, 0};
                std::string partialLog;
                for (int k = 0; k < 3 && problem.empty(); k++) {
                    Settings ss = seqSettings(1);
                    ss.samples = k == 0 ? 1 : k == 1 ? 64 : 100000;
                    std::atomic<bool> stop{false};
                    Hooks Hs;
                    Hs.snapshot = true;
                    Hs.log = [&](const std::wstring& l) { partialLog += U8(l) + "\n"; };
                    if (k == 2) {  // interrompu apres le premier releve de progression
                        Hs.cancel = &stop;
                        Hs.progress = [&](long long, long long, double, double) { stop = true; };
                    }
                    std::wstring e2;
                    if (convertFile(noisy, exeDir, ss, Hs, e2) != Result::Ok) problem = e2.empty() ? TX("selftest.render_failed") : e2;
                    else {
                        const std::string px = decodeRaw(outputPath(noisy, ss.outdir, L".png"), L"gray", L"");
                        if (px.size() != 64 * 64) problem = bad("image size " + std::to_string(px.size()));
                        else sd[k] = spread(px, mean[k]);
                    }
                }
                if (problem.empty()) {
                    if (sd[0] < 55 || sd[1] > sd[0] / 3 || std::fabs(mean[1] - 128) > 8 || std::fabs(mean[2] - 128) > 10 || sd[2] > sd[0])
                        problem = bad("spread " + std::to_string(sd[0]) + "," + std::to_string(sd[1]) + "," + std::to_string(sd[2]) + " mean " + std::to_string(mean[1]) + "," + std::to_string(mean[2]));
                }
                check("selftest.fmt_samples", problem);
            }
            // vignette intelligente : le noir des deux premieres secondes ne gagne pas, trois propositions distinctes dans la partie riche
            {
                std::wstring problem;
                const std::wstring half = put(L"p16_half.glsl",
                                              "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; vec3 c = 0.5 + 0.5 * cos(6.2831853 * (u.x * 3.0 + u.y * 2.0 + vec3(0.0, 0.33, 0.67) + iTime * 0.1));"
                                              " c *= 0.6 + 0.4 * sin(u.x * 40.0) * sin(u.y * 30.0); o = vec4(iTime < 2.0 ? vec3(0.02) : c, 1.0); }\n");
                Settings sp = base("h264", "png", 8);
                sp.duration = 4;
                Hooks H;
                H.log = [](const std::wstring&) {};
                std::vector<BestFrame> best;
                std::wstring e;
                if (!findBestFrames(half, exeDir, sp, 24, 3, H, best, e) || best.size() != 3) problem = bad("frames " + std::to_string(best.size()) + " " + U8(e));
                else {
                    for (size_t i = 0; i < best.size() && problem.empty(); i++) {
                        if (best[i].t < 2.0) problem = bad("black frame chosen " + std::to_string(best[i].t));
                        for (size_t j = i + 1; j < best.size(); j++)
                            if (std::fabs(best[i].t - best[j].t) < 0.1) problem = bad("not distinct");
                    }
                    if (problem.empty() && best[0].score < best[1].score) problem = bad("order");
                }
                check("selftest.fmt_smartthumb", problem);
            }
        }
        // --- Tres grandes images et projections (phase 17) -----------------------------------------------------------------------------------------
        {
            auto seqSettings = [&](double duration) {
                Settings st = base("h264", "png", 8);
                st.duration = duration;
                return st;
            };
            // affiche par tuiles : identique a un rendu d'un seul tenant (exact), avec sur-echantillonnage (a un niveau pres), en 16 bits, bandes gardees pour reprendre
            if (formatAvailable(ffmpeg, "h264", "png")) {
                std::wstring problem;
                const std::wstring grad = put(L"p17_grad.glsl", "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; float k = mod(floor(p.x / 7.0) + floor(p.y / 5.0), 2.0); o = vec4(u.x, u.y, k * 0.5 + 0.25 * sin(iResolution.x * 0.01), 1.0); }\n");
                auto still = [&](Settings st, const std::atomic<bool>* cancel, std::wstring& e, std::string& logText) {
                    Hooks Hs;
                    Hs.snapshot = true;
                    Hs.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
                    long long tiles = 0;
                    std::atomic<bool> stop{false};
                    if (cancel) {  // interrompu apres trois tuiles
                        Hs.cancel = &stop;
                        Hs.progress = [&](long long n, long long, double, double) {
                            tiles = n;
                            if (n >= 3) stop = true;
                        };
                    }
                    return convertFile(grad, exeDir, st, Hs, e);
                };
                auto bytesOf = [&](const Settings& st, const wchar_t* pix) { return decodeRaw(outputPath(grad, st.outdir, L".png"), pix, L""); };
                Settings plain = seqSettings(1);
                plain.width = 96;
                plain.height = 64;
                std::wstring e;
                std::string logText;
                if (still(plain, nullptr, e, logText) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
                const std::string ref8 = problem.empty() ? bytesOf(plain, L"rgb24") : std::string();
                if (problem.empty()) {  // 3 x 2 tuiles de 32 pixels
                    Settings tiled = plain;
                    tiled.tile = 32;
                    if (still(tiled, nullptr, e, logText) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else if (logText.find("3 x 2") == std::string::npos && logText.find("tiles") == std::string::npos && logText.find("tuiles") == std::string::npos) problem = bad("not tiled");
                    else if (bytesOf(tiled, L"rgb24") != ref8 || ref8.size() != 96 * 64 * 3) problem = bad("tiled differs");
                }
                if (problem.empty()) {  // sur-echantillonnage x2 : moyenne par blocs, a un niveau pres de la reduction de ffmpeg
                    Settings a = plain, b = plain;
                    a.ss = b.ss = 2;
                    b.tile = 64;
                    if (still(a, nullptr, e, logText) != Result::Ok || still(b, nullptr, e, logText) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else {
                        Settings bb = b;
                        const std::string tb = bytesOf(bb, L"rgb24");
                        still(a, nullptr, e, logText);
                        const std::string ta = bytesOf(a, L"rgb24");
                        int worst = 0;
                        for (size_t i = 0; i < ta.size() && i < tb.size(); i++) worst = std::max(worst, std::abs((int)(uint8_t)ta[i] - (int)(uint8_t)tb[i]));
                        if (ta.size() != tb.size() || ta.size() != 96 * 64 * 3 || worst > 1) problem = bad("supersampling " + std::to_string(worst));
                    }
                }
                if (problem.empty()) {  // 16 bits par composante : exact aussi
                    Settings d1 = plain, d2 = plain;
                    d1.depth = d2.depth = 10;
                    d2.tile = 32;
                    if (still(d1, nullptr, e, logText) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else {
                        const std::string r16 = bytesOf(d1, L"rgb48le");
                        if (still(d2, nullptr, e, logText) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
                        else if (r16.size() != 96 * 64 * 6 || bytesOf(d2, L"rgb48le") != r16) problem = bad("16-bit differs");
                    }
                }
                for (int tile : {0, 32}) {  // resolution ecrite dans le PNG (300 ppp = 11811 points par metre), avec et sans tuiles
                    if (!problem.empty()) break;
                    Settings d = plain;
                    d.dpi = 300;
                    d.tile = tile;
                    std::string png;
                    if (still(d, nullptr, e, logText) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else if (!readFile(outputPath(grad, d.outdir, L".png"), png)) problem = bad("png unreadable");
                    else {
                        const size_t at = png.find("pHYs");
                        const unsigned ppu = at == std::string::npos || at + 12 > png.size() ? 0u : ((unsigned)(uint8_t)png[at + 4] << 24 | (unsigned)(uint8_t)png[at + 5] << 16 | (unsigned)(uint8_t)png[at + 6] << 8 | (uint8_t)png[at + 7]);
                        if (ppu < 11810 || ppu > 11812) problem = bad("dpi " + std::to_string(ppu));
                    }
                }
                if (problem.empty()) {  // interruption puis reprise : la bande du haut est gardee, le resultat est le meme, le dossier disparait
                    Settings t = plain;
                    t.tile = 32;
                    t.outdir = outDir + sep + L"resume";
                    platform::makeDir(t.outdir);
                    std::atomic<bool> any{true};
                    const Result first = still(t, &any, e, logText);
                    const std::wstring pngPath = outputPath(grad, t.outdir, L".png");
                    const std::wstring tilesDir = pngPath.substr(0, pngPath.find_last_of(L'.')) + L".tiles";
                    if (first != Result::Cancelled) problem = bad("interruption not seen");
                    else if (!platform::fileExists(tilesDir + L"/band_00000.png")) problem = bad("band not kept");
                    else if (still(t, nullptr, e, logText) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else if (bytesOf(t, L"rgb24") != ref8) problem = bad("resumed differs");
                    else if (platform::dirExists(tilesDir)) problem = bad("tiles folder left");
                }
                check("selftest.fmt_tiles", problem);
            }
            // modes VR : yeux cote a cote, haut / bas, anaglyphe, repères du fulldome, six faces de cubemap a l'endroit
            if (formatAvailable(ffmpeg, "h264", "png")) {
                std::wstring problem;
                auto stillOf = [&](const std::wstring& shader, const Settings& st, std::wstring& e) {
                    Hooks Hs;
                    Hs.snapshot = true;
                    Hs.log = [](const std::wstring&) {};
                    return convertFile(shader, exeDir, st, Hs, e) == Result::Ok;
                };
                // niveau (r, g, b) du pixel (x, y), premiere ligne en haut, d'une image PNG ; {-1} si illisible
                auto pixelOf = [&](const std::wstring& file, int w, int h, int x, int y, int rgb[3]) {
                    const std::string px = decodeRaw(file, L"rgb24", L"");
                    if (px.size() != (size_t)w * h * 3) return false;
                    for (int c = 0; c < 3; c++) rgb[c] = (uint8_t)px[((size_t)y * w + x) * 3 + c];
                    return true;
                };
                const std::wstring eye = put(L"p17_eye.glsl", "void mainVR(out vec4 o, in vec2 fc, in vec3 ro, in vec3 rd){ o = vec4(vec3(clamp(0.5 + 10.0 * ro.x, 0.0, 1.0)), 1.0); }\n");
                struct EyeCase {
                    const char* mode;
                    int w, h, x, y, want;
                };
                std::wstring e;
                int px[3] = {0, 0, 0};
                for (const EyeCase& c : {EyeCase{"stereo", 64, 128, 32, 32, 46}, EyeCase{"stereo", 64, 128, 32, 96, 209}, EyeCase{"sbs", 128, 64, 32, 32, 46},
                                         EyeCase{"sbs", 128, 64, 96, 32, 209}}) {
                    Settings st = seqSettings(1);
                    st.vr = c.mode;
                    st.width = c.w;
                    st.height = c.h;
                    if (!stillOf(eye, st, e)) {
                        problem = e.empty() ? TX("selftest.render_failed") : e;
                        break;
                    }
                    if (!pixelOf(outputPath(eye, st.outdir, L".png"), c.w, c.h, c.x, c.y, px) || std::abs(px[0] - c.want) > 6) {
                        problem = bad(std::string(c.mode) + " eye " + std::to_string(px[0]) + " want " + std::to_string(c.want));
                        break;
                    }
                }
                if (problem.empty()) {  // anaglyphe : le rouge vient de l'oeil gauche, le vert et le bleu de l'oeil droit
                    Settings st = seqSettings(1);
                    st.vr = "anaglyph";
                    st.width = st.height = 64;
                    if (!stillOf(eye, st, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else if (!pixelOf(outputPath(eye, st.outdir, L".png"), 64, 64, 32, 32, px) || std::abs(px[0] - 46) > 6 || std::abs(px[1] - 209) > 6 || std::abs(px[2] - 209) > 6)
                        problem = bad("anaglyph " + std::to_string(px[0]) + "," + std::to_string(px[1]) + "," + std::to_string(px[2]));
                }
                const std::wstring dirs = put(L"p17_dirs.glsl", "void mainVR(out vec4 o, in vec2 fc, in vec3 ro, in vec3 rd){ o = vec4(0.5 + 0.5 * rd, 1.0); }\n");
                if (problem.empty()) {  // fulldome : zenith au centre, devant en bas, droite = +x, noir hors du cercle
                    Settings st = seqSettings(1);
                    st.vr = "fulldome";
                    st.width = st.height = 64;
                    const std::wstring file = outputPath(dirs, st.outdir, L".png");
                    if (!stillOf(dirs, st, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else if (!pixelOf(file, 64, 64, 32, 32, px) || px[1] < 250 || std::abs(px[0] - 128) > 8 || std::abs(px[2] - 128) > 8)
                        problem = bad("zenith " + std::to_string(px[0]) + "," + std::to_string(px[1]) + "," + std::to_string(px[2]));
                    else if (!pixelOf(file, 64, 64, 32, 60, px) || px[2] > 14 || std::abs(px[1] - 149) > 12 || std::abs(px[0] - 128) > 8)
                        problem = bad("front " + std::to_string(px[0]) + "," + std::to_string(px[1]) + "," + std::to_string(px[2]));
                    else if (!pixelOf(file, 64, 64, 62, 32, px) || px[0] < 245)
                        problem = bad("right " + std::to_string(px[0]));
                    else if (!pixelOf(file, 64, 64, 1, 1, px) || px[0] > 3 || px[1] > 3 || px[2] > 3)
                        problem = bad("corner not black");
                }
                if (problem.empty()) {  // six faces : l'axe de chaque face au centre, et la haut / droite de l'image
                    Settings st = seqSettings(1);
                    st.width = st.height = 32;
                    Hooks Hs;
                    Hs.log = [](const std::wstring&) {};
                    std::vector<std::wstring> made;
                    if (!renderCubeFaces(dirs, exeDir, st, Hs, made, e) || made.size() != 6) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else {
                        struct Face {
                            int face, x, y;
                            int ch;       // composante a verifier
                            bool high;    // vraie : proche de 255 ; fausse : proche de 0
                        };
                        // centre de chaque face : px -> r haut, nx -> r bas, py -> g haut, ny -> g bas, pz -> b haut, nz -> b bas ;
                        // haut de la face +x : g haut ; droite de la face +x : b haut (+z) ; haut de la face +y : b haut (+z) ; droite de la face +z : r bas (-x)
                        const Face cases[] = {{0, 16, 16, 0, true}, {1, 16, 16, 0, false}, {2, 16, 16, 1, true}, {3, 16, 16, 1, false}, {4, 16, 16, 2, true}, {5, 16, 16, 2, false},
                                              {0, 16, 1, 1, true}, {0, 30, 16, 2, true}, {2, 16, 1, 2, true}, {4, 30, 16, 0, false}};
                        for (const Face& c : cases) {
                            if (!pixelOf(made[(size_t)c.face], 32, 32, c.x, c.y, px)) {
                                problem = bad("face file " + std::to_string(c.face));
                                break;
                            }
                            const bool ok = c.high ? px[c.ch] >= 190 : px[c.ch] <= 65;
                            if (!ok) {
                                problem = bad("face " + std::to_string(c.face) + " at " + std::to_string(c.x) + "," + std::to_string(c.y) + " ch" + std::to_string(c.ch) + "=" + std::to_string(px[c.ch]));
                                break;
                            }
                        }
                    }
                }
                check("selftest.fmt_vrmodes", problem);
            }
            // carte de chaleur : la moitie droite de l'ecran est chere, l'image est teintee, les chiffres sont rendus
            {
                std::wstring problem;
                const std::wstring heavy = put(L"p17_heavy.glsl",
                                               "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; float a = 0.0;"
                                               " if (u.x > 0.5) { for (int i = 0; i < 900; i++) a += sin(float(i) * u.x + iTime) * cos(a + u.y); }"
                                               " o = vec4(u, 0.5 + 0.5 * sin(a), 1.0); }\n");
                Settings sp = base("h264", "png", 8);
                sp.width = 1920;
                sp.height = 1080;
                Hooks H;
                H.log = [](const std::wstring&) {};
                HeatStats hs;
                std::wstring file, e;
                if (!renderHeatmap(heavy, exeDir, sp, 8, 4, -1, H, hs, file, e)) problem = bad("heatmap " + U8(e));
                else {
                    double left = 0, right = 0;
                    for (int r = 0; r < hs.rows; r++)
                        for (int c = 0; c < hs.cols; c++) (c < hs.cols / 2 ? left : right) += hs.tileMs[(size_t)r * hs.cols + c];
                    uint8_t r0, g0, b0, r1, g1, b1;
                    heatColor(0, r0, g0, b0);
                    heatColor(1, r1, g1, b1);
                    const std::string img = decodeRaw(file, L"rgb24", L"");
                    if (hs.tileMs.size() != 32 || right < 1.5 * left || hs.hotCol < hs.cols / 2 || hs.hotShare <= 0 || hs.maxMs < hs.minMs)
                        problem = bad("tiles left " + std::to_string(left) + " right " + std::to_string(right));
                    else if (b0 <= r0 || r1 <= b1) problem = bad("colour ramp");
                    else if (img.size() != (size_t)960 * 540 * 3) problem = bad("image size " + std::to_string(img.size()));
                    else if (describeHeat(hs).empty()) problem = bad("figures");
                }
                check("selftest.fmt_heatmap", problem);
            }
            // temps GPU par passe dans le rapport de rendu
            if (formatAvailable(ffmpeg, "h264", "mp4")) {
                std::wstring problem;
                const std::wstring hist = put(L"p17_hist.glsl", "// iChannel0=bufferA\nvoid mainImage(out vec4 o, in vec2 p){ o = texture(iChannel0, p / iResolution.xy); }\n");
                put(L"p17_hist.bufferA.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.25, 0.5, 0.75, 1.0); }\n");
                Settings sv = base("h264", "mp4", 8);
                sv.duration = 1;
                sv.report = "json";
                std::wstring e;
                std::string logText;
                if (!renderOne(hist, sv, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    std::string js;
                    const std::wstring file = outputFile(hist, sv, false);
                    readFile(file.substr(0, file.find_last_of(L'.')) + L".report.json", js);
                    const size_t g = js.find("\"gpu_ms\": {");
                    if (g == std::string::npos || js.find("\"sampled_frames\": 10", g) == std::string::npos) problem = bad("gpu_ms missing");
                    else {
                        double image = 0, buffers = -1;
                        const size_t pi = js.find("\"image\": ", g), pb = js.find("\"buffers\": ", g);
                        if (pi != std::string::npos) image = atof(js.c_str() + pi + 9);
                        if (pb != std::string::npos) buffers = atof(js.c_str() + pb + 11);
                        if (image <= 0 || buffers <= 0) problem = bad("passes " + std::to_string(image) + "," + std::to_string(buffers));
                    }
                }
                check("selftest.fmt_gpupasses", problem);
            }
        }
    }
    // Canaux dynamiques et cubemap par fichiers : audio (spectre), video (images au bon temps), six faces de couleurs distinctes
    {
        const std::wstring sep(1, platform::pathSeparator());
        auto gen = [&](const std::wstring& args) {
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y " + args, nullptr);
        };
        auto writeShader = [&](const std::wstring& name, const std::string& body) {
            const std::wstring path = outDir + sep + name;
            if (FILE* f = platform::openFile(path, "wb")) {
                fwrite(body.data(), 1, body.size(), f);
                fclose(f);
            }
            return path;
        };
        auto preview = [&](const std::wstring& shaderPath, int w, int h, double time, std::vector<uint8_t>& got, std::wstring& e) {
            Settings st = base("h264", "mp4", 8);
            st.width = w;
            st.height = h;
            Hooks H;
            H.log = [](const std::wstring&) {};
            H.previewTime = time;
            H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { got = std::move(px); };
            return convertFile(shaderPath, exeDir, st, H, e) == Result::Ok && got.size() == (size_t)w * h * 4;
        };
        auto report1 = [&](const char* key, const std::wstring& problem) {
            total++;
            report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX(key) + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
            if (!problem.empty()) fails++;
        };
        auto bad = [&](const std::string& what) { return TXF("selftest.fmt_bad", W(what).c_str()); };

        // audio : un sinus de 1 kHz donne un pic dans les premieres raies du spectre, rien dans les hautes frequences
        {
            gen(L"-f lavfi -i sine=frequency=1000:duration=2 -ar 44100 -ac 1 \"" + outDir + sep + L"tone.wav\"");
            const std::wstring sh = writeShader(L"audiotest.glsl",
                "// iChannel0=audio:tone.wav\n"
                "void mainImage(out vec4 o, in vec2 p){ float lo = 0.0, hi = 0.0;"
                " for (int i = 0; i < 40; i++) { lo = max(lo, texture(iChannel0, vec2(float(i) / 330.0, 0.25)).x);"
                " hi = max(hi, texture(iChannel0, vec2(0.5 + float(i) / 80.0, 0.25)).x); }"
                " float wave = texture(iChannel0, vec2(0.3, 0.75)).x; o = vec4(lo, hi, wave, 1.0); }\n");
            std::vector<uint8_t> got;
            std::wstring e, problem;
            if (!fileExists(outDir + sep + L"tone.wav")) problem = bad("no tone");
            else if (!preview(sh, 16, 16, 1.0, got, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else if (got[2] < 200 || got[1] > 60) problem = bad("spectrum r=" + std::to_string(got[2]) + " g=" + std::to_string(got[1]));
            else if (got[0] < 20) problem = bad("waveform");
            report1("selftest.fmt_audio", problem);
        }
        // parametres lies au son : le niveau et les bandes du meme fichier (sinus de 1 kHz : mediums au maximum, graves nuls)
        {
            const std::string tone = U8(outDir + sep + L"tone.wav");
            const std::wstring sh = writeShader(L"paramaudio.glsl",
                "// audio=" + tone + "\n// uniform float lvl = audio.level\n// uniform float mid = audio.mid\n// uniform float bas = audio.bass\n"
                "void mainImage(out vec4 o, in vec2 p){ o = vec4(lvl * 4.0, mid, bas, 1.0); }\n");
            std::vector<uint8_t> got;
            std::wstring e, problem;
            if (!preview(sh, 16, 16, 1.0, got, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else if (got[2] < 100 || got[2] > 155 || got[1] < 230 || got[0] > 30)
                problem = bad("level r=" + std::to_string(got[2]) + " mid g=" + std::to_string(got[1]) + " bass b=" + std::to_string(got[0]));
            report1("selftest.fmt_param_audio", problem);
        }
        // video : l'image montree au temps t est celle que ffmpeg decode a ce temps
        {
            gen(L"-f lavfi -i testsrc=s=64x64:d=3:r=30 -pix_fmt yuv420p \"" + outDir + sep + L"clip.mp4\"");
            const std::wstring sh = writeShader(L"videotest.glsl",
                "// iChannel0=video:clip.mp4\n// iChannel0.filter=nearest\n"
                "void mainImage(out vec4 o, in vec2 p){ o = vec4(texture(iChannel0, p / iResolution.xy).rgb, 1.0); }\n");
            std::wstring problem;
            for (double when : {0.0, 1.5}) {
                std::vector<uint8_t> got;
                std::wstring e;
                const std::wstring raw = outDir + sep + L"clip.raw";
                gen(L"-ss " + wfmt(L"%g", when) + L" -i \"" + outDir + sep + L"clip.mp4\" -frames:v 1 -f rawvideo -pix_fmt bgra \"" + raw + L"\"");
                std::string want;
                if (!preview(sh, 64, 64, when, got, e) || !readFile(raw, want) || want.size() != got.size()) {
                    problem = e.empty() ? bad("video frame") : e;
                    break;
                }
                double sum = 0;
                size_t cnt = 0;
                for (size_t i = 0; i < got.size(); i++) {
                    if (i % 4 == 3) continue;
                    sum += std::abs((int)got[i] - (int)(uint8_t)want[i]);
                    cnt++;
                }
                if (sum / cnt > 4.0) {
                    problem = bad("video t=" + std::to_string(when) + " diff " + std::to_string(sum / cnt));
                    break;
                }
            }
            report1("selftest.fmt_video", problem);
        }
        // import d'un export JSON de Shadertoy (hors ligne) : fichiers de passes, commentaires de canaux, rendu
        {
            std::wstring problem;
            std::vector<std::wstring> logs;
            std::wstring e;
            const std::wstring imported = resolveShaderInput(outDir + sep, dir + L"shadertoy_export.json", [&](const std::wstring& m) { logs.push_back(m); }, e);
            std::vector<uint8_t> got;
            if (imported.empty()) problem = e;
            else if (!fileExists(imported) || logs.size() < 2) problem = bad("import files");
            else {
                std::string mainTxt, bufA, common;
                const std::wstring stem = imported.substr(0, imported.size() - 5);
                readFile(imported, mainTxt);
                readFile(stem + L".bufferA.glsl", bufA);
                readFile(stem + L".common.glsl", common);
                if (mainTxt.find("// iChannel0=bufferA") == std::string::npos || mainTxt.find("// iChannel1=noise") == std::string::npos ||
                    bufA.empty() || common.find("#define COL") == std::string::npos)
                    problem = bad("import content");
                else if (!preview(imported, 16, 16, 0.0, got, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else if (std::abs((int)got[2] - 51) > 4 || std::abs((int)got[1] - 153) > 4 || std::abs((int)got[0] - 230) > 4)
                    problem = bad("imported render");
            }
            report1("selftest.fmt_import", problem);
        }
        // passe Cubemap A : la cubemap rendue face par face est coherente avec les directions lues par l'image
        {
            writeShader(L"cubepass.cubemapA.glsl",
                "void mainCubemap(out vec4 o, in vec2 p, in vec3 ro, in vec3 rd){ o = vec4(abs(rd), 1.0); }\n");
            const std::wstring sh = writeShader(L"cubepass.glsl",
                "// iChannel0=cubeA\n"
                "void mainImage(out vec4 o, in vec2 p){ vec2 uv = p / iResolution.xy * 2.0 - 1.0; vec3 d = normalize(vec3(uv.x, uv.y, 0.7 + uv.x * uv.y));"
                " vec3 s = texture(iChannel0, d).rgb; o = vec4(abs(s - abs(d)) * 20.0, 1.0); }\n");
            std::vector<uint8_t> got;
            std::wstring e, problem;
            if (!preview(sh, 32, 32, 0.0, got, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                double sum = 0;
                for (size_t i = 0; i < got.size(); i++)
                    if (i % 4 != 3) sum += got[i];
                if (sum / (got.size() * 0.75) > 6.0) problem = bad("cubemap pass mapping " + std::to_string(sum / (got.size() * 0.75)));
            }
            report1("selftest.fmt_cubepass", problem);
        }
        // cubemap par six fichiers : chaque direction lit la couleur de sa face
        {
            static const char* kColors[6] = {"0xFF0000", "0x00FFFF", "0x00FF00", "0xFF00FF", "0x0000FF", "0xFFFF00"};  // les noms de ffmpeg ne sont pas du RVB pur
            for (int i = 0; i < 6; i++)
                gen(L"-f lavfi -i color=c=" + W(kColors[i]) + L":s=8x8 -frames:v 1 \"" + outDir + sep + L"face" + std::to_wstring(i) + L".png\"");
            const std::wstring sh = writeShader(L"cubetest.glsl",
                "// iChannel0=cube:face%d.png\n// iChannel0.filter=nearest\n"
                "void mainImage(out vec4 o, in vec2 p){ vec3 d[6] = vec3[6](vec3(1,0,0), vec3(-1,0,0), vec3(0,1,0), vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1));"
                " int k = int(p.x / 16.0); o = vec4(texture(iChannel0, d[k]).rgb, 1.0); }\n");
            static const uint8_t want[6][3] = {{255, 0, 0}, {0, 255, 255}, {0, 255, 0}, {255, 0, 255}, {0, 0, 255}, {255, 255, 0}};  // R G B
            std::vector<uint8_t> got;
            std::wstring e, problem;
            if (!preview(sh, 96, 16, 0.0, got, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else
                for (int i = 0; i < 6 && problem.empty(); i++) {
                    const uint8_t* px = &got[((size_t)8 * 96 + i * 16 + 8) * 4];  // BGRA, centre de la case
                    const int dr = std::abs((int)px[2] - want[i][0]), dg = std::abs((int)px[1] - want[i][1]), db = std::abs((int)px[0] - want[i][2]);
                    if (dr > 40 || dg > 40 || db > 40) problem = bad("face " + std::to_string(i));
                }
            report1("selftest.fmt_cube", problem);
        }
    }
    // Erreurs GLSL localisees « fichier:ligne: » et reconnues par le journal cliquable
    {
        total++;
        const std::wstring broken = outDir + platform::pathSeparator() + L"broken.glsl";
        const std::string src = "// ligne 1\nvoid mainImage(out vec4 o, in vec2 p)\n{\n    o = vec4(undefined_name, 0.0, 0.0, 1.0);\n}\n";
        if (FILE* f = platform::openFile(broken, "wb")) {
            fwrite(src.data(), 1, src.size(), f);
            fclose(f);
        }
        Settings st = base("h264", "mp4", 8);
        std::vector<uint8_t> unused;
        Hooks H;
        H.log = [](const std::wstring&) {};
        H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { unused = std::move(px); };
        std::wstring e, problem;
        if (convertFile(broken, exeDir, st, H, e) == Result::Ok) {
            problem = TXF("selftest.fmt_bad", W("no error").c_str());
        } else {
            std::wstring path;
            int line = 0;
            size_t pos = 0;
            bool found = false;
            while (pos <= e.size() && !found) {
                size_t nl = e.find(L'\n', pos);
                if (nl == std::wstring::npos) nl = e.size();
                found = parseLocation(e.substr(pos, nl - pos), path, line) && line == 4 && path == broken;
                pos = nl + 1;
            }
            if (!found) problem = TXF("selftest.fmt_bad", W("file:line").c_str());
        }
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_locate") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // Rapport de compatibilite des passes : canal declare mais jamais lu, passe que personne n'utilise
    {
        total++;
        const std::wstring dir = outDir + platform::pathSeparator();
        auto put = [&](const std::wstring& name, const std::string& body) {
            if (FILE* f = platform::openFile(dir + name, "wb")) {
                fwrite(body.data(), 1, body.size(), f);
                fclose(f);
            }
        };
        const std::string mainCode =
            "// iChannel0=bufferA\n// iChannel1=noise\nvoid mainImage(out vec4 o, in vec2 p){ o = texture(iChannel0, p / iResolution.xy); }\n";
        put(L"rpt.glsl", mainCode);
        put(L"rpt.bufferA.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(1.0); }\n");
        put(L"rpt.bufferC.glsl", "// iChannel0=bufferC\nvoid mainImage(out vec4 o, in vec2 p){ o = texture(iChannel0, p / iResolution.xy) + 0.1; }\n");
        BufferPasses bp;
        Hooks H;
        H.log = [](const std::wstring&) {};
        bp.discover(dir + L"rpt.glsl", [](const std::wstring& p, std::string& out) { return readFile(p, out); }, H);
        Settings st;
        parseLines(st, mainCode, true);
        std::vector<std::wstring> notes;
        bp.report(st, mainCode, notes);
        std::wstring problem;
        if (notes.size() != 2 || notes[0].find(L"Image") == std::wstring::npos || notes[1].find(L"Buffer C") == std::wstring::npos)
            problem = TXF("selftest.fmt_bad", W("notes " + std::to_string(notes.size())).c_str());
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_compat_passes") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // Parametres : un nom deja pris (variable Shadertoy, uniform du shader, doublon) ou une valeur invalide arrete le rendu avec un message
    {
        total++;
        const std::wstring dir = outDir + platform::pathSeparator();
        struct Case {
            const char* name;
            const char* body;
            const char* mustContain;  // texte attendu dans l'erreur
        };
        const Case cases[] = {
            {"pe1.glsl", "// uniform float iTime = 1.0\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(1.0); }\n", "iTime"},
            {"pe2.glsl", "uniform float k;\n// uniform float k = 1.0\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(k); }\n", "k"},
            {"pe3.glsl", "// uniform float a = 1.0\n// uniform float a = 2.0\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(a); }\n", "a"},
            {"pe4.glsl", "// uniform float a = 1.0\n// key a=0:1 ; x:2\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(a); }\n", "x:2"},
            {"pe5.glsl", "// uniform float a = zz\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(a); }\n", "zz"}};
        std::wstring problem;
        for (const Case& c : cases) {
            if (FILE* f = platform::openFile(dir + W(c.name), "wb")) {
                fputs(c.body, f);
                fclose(f);
            }
            Settings st;
            st.width = st.height = 16;
            std::vector<uint8_t> unused;
            Hooks H;
            H.log = [](const std::wstring&) {};
            H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { unused = std::move(px); };
            std::wstring e;
            if (convertFile(dir + W(c.name), exeDir, st, H, e) == Result::Ok || e.find(W(c.mustContain)) == std::wstring::npos) {
                problem = TXF("selftest.fmt_bad", W(std::string(c.name) + " " + U8(e)).c_str());
                break;
            }
        }
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_param_errors") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // Lecture des parametres hors rendu (fenetres de reglage) : BOM UTF-8, fins de ligne CRLF, images cles et temps des cles
    {
        total++;
        const std::wstring path = outDir + platform::pathSeparator() + L"paramload.glsl";
        const std::string body = "\xEF\xBB\xBF// uniform float v = 1 [0..2]\r\n// key v=0:0 ; 3:2\r\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(v); }\r\n";
        if (FILE* f = platform::openFile(path, "wb")) {
            fwrite(body.data(), 1, body.size(), f);
            fclose(f);
        }
        std::vector<ParamInfo> infos;
        std::vector<double> keys;
        std::wstring err, problem;
        if (!loadShaderParams(exeDir, path, infos, err, nullptr, &keys))
            problem = err;
        else if (infos.size() != 1 || infos[0].name != "v" || !infos[0].animated || keys.size() != 2 || keys[1] != 3.0)
            problem = TXF("selftest.fmt_bad", W("params " + std::to_string(infos.size()) + " keys " + std::to_string(keys.size())).c_str());
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_param_load") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // LUT 3D : l'interpolation tetraedrique du GPU est comparee a une version CPU sur les 4096 pixels d'un degrade
    {
        total++;
        const std::wstring dir = outDir + platform::pathSeparator();
        const int N = 5;
        std::vector<float> lutv((size_t)N * N * N * 3);
        uint32_t x = 0x9E3779B9u;
        for (auto& v : lutv) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            v = (x >> 8) / 16777216.0f;
        }
        std::string cube = "LUT_3D_SIZE 5\n";
        char line[96];
        for (size_t i = 0; i < (size_t)N * N * N; i++) {
            snprintf(line, sizeof(line), "%.6f %.6f %.6f\n", lutv[i * 3], lutv[i * 3 + 1], lutv[i * 3 + 2]);
            cube += line;
        }
        auto put = [&](const std::wstring& name, const std::string& body) {
            if (FILE* f = platform::openFile(dir + name, "wb")) {
                fwrite(body.data(), 1, body.size(), f);
                fclose(f);
            }
        };
        put(L"random5.cube", cube);
        put(L"lutrandom.glsl",
            "// lut=random5.cube\nvoid mainImage(out vec4 o, in vec2 p){ vec2 uv = p / iResolution.xy; o = vec4(uv, 0.37, 1.0); }\n");
        Settings st;
        st.width = st.height = 64;
        std::vector<uint8_t> got;
        Hooks H;
        H.log = [](const std::wstring&) {};
        H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { got = std::move(px); };
        std::wstring e, problem;
        if (convertFile(dir + L"lutrandom.glsl", exeDir, st, H, e) != Result::Ok || got.size() != 64 * 64 * 4) {
            problem = e.empty() ? TX("selftest.render_failed") : e;
        } else {
            auto fetch = [&](int r, int g, int b, int c) { return (double)lutv[(((size_t)b * N + g) * N + r) * 3 + c]; };
            int worst = 0;
            for (int yTop = 0; yTop < 64 && problem.empty(); yTop++)
                for (int xx = 0; xx < 64; xx++) {
                    const double in[3] = {(xx + 0.5) / 64.0, (63 - yTop + 0.5) / 64.0, 0.37};
                    int i0[3], i1[3];
                    double f[3];
                    for (int c = 0; c < 3; c++) {
                        const double p = in[c] * (N - 1);
                        i0[c] = (int)std::floor(p);
                        f[c] = p - i0[c];
                        i1[c] = std::min(i0[c] + 1, N - 1);
                    }
                    for (int c = 0; c < 3; c++) {  // interpolation tetraedrique, memes six cas que le GPU
                        auto C = [&](int r, int g, int b) { return fetch(r ? i1[0] : i0[0], g ? i1[1] : i0[1], b ? i1[2] : i0[2], c); };
                        double v;
                        if (f[0] > f[1]) {
                            if (f[1] > f[2]) v = C(0, 0, 0) + (C(1, 0, 0) - C(0, 0, 0)) * f[0] + (C(1, 1, 0) - C(1, 0, 0)) * f[1] + (C(1, 1, 1) - C(1, 1, 0)) * f[2];
                            else if (f[0] > f[2]) v = C(0, 0, 0) + (C(1, 0, 0) - C(0, 0, 0)) * f[0] + (C(1, 0, 1) - C(1, 0, 0)) * f[2] + (C(1, 1, 1) - C(1, 0, 1)) * f[1];
                            else v = C(0, 0, 0) + (C(0, 0, 1) - C(0, 0, 0)) * f[2] + (C(1, 0, 1) - C(0, 0, 1)) * f[0] + (C(1, 1, 1) - C(1, 0, 1)) * f[1];
                        } else {
                            if (f[2] > f[1]) v = C(0, 0, 0) + (C(0, 0, 1) - C(0, 0, 0)) * f[2] + (C(0, 1, 1) - C(0, 0, 1)) * f[1] + (C(1, 1, 1) - C(0, 1, 1)) * f[0];
                            else if (f[2] > f[0]) v = C(0, 0, 0) + (C(0, 1, 0) - C(0, 0, 0)) * f[1] + (C(0, 1, 1) - C(0, 1, 0)) * f[2] + (C(1, 1, 1) - C(0, 1, 1)) * f[0];
                            else v = C(0, 0, 0) + (C(0, 1, 0) - C(0, 0, 0)) * f[1] + (C(1, 1, 0) - C(0, 1, 0)) * f[0] + (C(1, 1, 1) - C(1, 1, 0)) * f[2];
                        }
                        const int want = (int)std::lround(std::min(1.0, std::max(0.0, v)) * 255.0);
                        const int have = got[((size_t)yTop * 64 + xx) * 4 + (2 - c)];  // BGRA
                        worst = std::max(worst, std::abs(want - have));
                    }
                }
            if (worst > 2) problem = TXF("selftest.fmt_bad", W("tetrahedral max difference " + std::to_string(worst)).c_str());
        }
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_lut") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // Fichiers LUT invalides, effets inconnus : le rendu s'arrete avec un message qui nomme le fichier ou l'effet
    {
        total++;
        const std::wstring dir = outDir + platform::pathSeparator();
        struct Case {
            const char* shader;
            const char* cube;      // contenu du .cube ("" = pas de fichier)
            const char* directive; // ligne // du shader
            const char* contains;  // texte attendu dans l'erreur
        };
        const Case cases[] = {
            {"lb1.glsl", "LUT_3D_SIZE 2\n1 1 1\n", "// lut=lb1.cube", "lb1.cube"},                               // trop peu de lignes
            {"lb2.glsl", "LUT_3D_SIZE 1\n0 0 0\n", "// lut=lb2.cube", "lb2.cube"},                               // taille hors limites
            {"lb3.glsl", "LUT_1D_SIZE 2\nWHATEVER 3\n0 0 0\n1 1 1\n", "// lut=lb3.cube", "lb3.cube:2"},         // mot-cle inconnu, ligne 2
            {"lb4.glsl", "LUT_1D_SIZE 2\n0 0 0\n1 x 1\n", "// lut=lb4.cube", "lb4.cube:3"},                    // ligne de donnees invalide
            {"lb5.glsl", "", "// lut=absent.cube", "absent.cube"},                                              // fichier absent
            {"lb6.glsl", "", "// post=bloom,glitter", "glitter"},                                              // effet inconnu
            {"lb7.glsl", "", "// post=bloom:fort", "fort"}};                                                   // quantite invalide
        std::wstring problem;
        for (const Case& c : cases) {
            const std::string body = std::string(c.directive) + "\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(1.0); }\n";
            if (FILE* f = platform::openFile(dir + W(c.shader), "wb")) {
                fwrite(body.data(), 1, body.size(), f);
                fclose(f);
            }
            if (*c.cube) {
                const std::wstring cubeName = W(std::string(c.shader).substr(0, 3) + ".cube");
                if (FILE* f = platform::openFile(dir + cubeName, "wb")) {
                    fputs(c.cube, f);
                    fclose(f);
                }
            }
            Settings st;
            st.width = st.height = 16;
            std::vector<uint8_t> unused;
            Hooks H;
            H.log = [](const std::wstring&) {};
            H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { unused = std::move(px); };
            std::wstring e;
            if (convertFile(dir + W(c.shader), exeDir, st, H, e) == Result::Ok || e.find(W(c.contains)) == std::wstring::npos) {
                problem = TXF("selftest.fmt_bad", W(std::string(c.shader) + " " + U8(e)).c_str());
                break;
            }
        }
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_lut_errors") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // Grain : du bruit autour de la valeur d'origine, sans la deplacer
    {
        total++;
        const std::wstring dir = outDir + platform::pathSeparator();
        const std::string body = "// post=grain:0.2\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(0.5, 0.5, 0.5, 1.0); }\n";
        if (FILE* f = platform::openFile(dir + L"grain.glsl", "wb")) {
            fwrite(body.data(), 1, body.size(), f);
            fclose(f);
        }
        Settings st;
        st.width = st.height = 64;
        std::vector<uint8_t> got;
        Hooks H;
        H.log = [](const std::wstring&) {};
        H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { got = std::move(px); };
        std::wstring e, problem;
        if (convertFile(dir + L"grain.glsl", exeDir, st, H, e) != Result::Ok || got.size() != 64 * 64 * 4) {
            problem = e.empty() ? TX("selftest.render_failed") : e;
        } else {
            double sum = 0, sq = 0;
            for (size_t i = 0; i < got.size(); i += 4) {
                sum += got[i + 1];
                sq += (double)got[i + 1] * got[i + 1];
            }
            const double n = got.size() / 4.0, mean = sum / n, sd = std::sqrt(std::max(0.0, sq / n - mean * mean));
            // bruit uniforme de largeur 0.2 : ecart type 0.2 / sqrt(12) = 0.058 soit 14.7 sur 255 ; moyenne inchangee
            if (std::fabs(mean - 127.5) > 4.0 || sd < 10.0 || sd > 20.0)
                problem = TXF("selftest.fmt_bad", W("grain mean " + std::to_string(mean) + " sd " + std::to_string(sd)).c_str());
        }
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_grain") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
        if (!problem.empty()) fails++;
    }
    // Textures par fichier : image decodee par ffmpeg (PNG 8 bits, HDR flottant) lue en 1:1 par le shader
    for (const char* ext : {"png", "hdr"}) {
        total++;
        const std::wstring texName = std::wstring(L"tex.") + W(ext);
        const std::wstring texPath = outDir + platform::pathSeparator() + texName;
        const std::wstring shaderPath = outDir + platform::pathSeparator() + L"usetex.glsl";
        std::wstring problem;
        runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -f lavfi -i testsrc=s=16x16:d=1 -frames:v 1 \"" + texPath + L"\"",
                   nullptr);
        const std::string src = std::string("// iChannel0=") + U8(texName) +
                                "\n// iChannel0.filter=nearest\n// iChannel0.wrap=clamp\n"
                                "void mainImage(out vec4 o, in vec2 p){ o = vec4(texture(iChannel0, p / iResolution.xy).rgb, 1.0); }\n";
        if (FILE* f = platform::openFile(shaderPath, "wb")) {
            fwrite(src.data(), 1, src.size(), f);
            fclose(f);
        }
        Settings st = base("h264", "mp4", 8);
        st.width = 16;
        st.height = 16;
        std::vector<uint8_t> got;
        Hooks H;
        H.log = [](const std::wstring&) {};
        H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { got = std::move(px); };
        std::wstring e;
        if (!fileExists(texPath)) {
            problem = TXF("selftest.fmt_missing", W(ext).c_str());
        } else if (convertFile(shaderPath, exeDir, st, H, e) != Result::Ok || got.size() != 16 * 16 * 4) {
            problem = e.empty() ? TX("selftest.render_failed") : e;
        } else if (std::string(ext) == "png") {  // meme orientation et memes pixels que l'image d'origine
            const std::wstring raw = outDir + platform::pathSeparator() + L"tex.raw";
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + texPath +
                           L"\" -frames:v 1 -f rawvideo -pix_fmt bgra \"" + raw + L"\"",
                       nullptr);
            std::string want;
            if (!readFile(raw, want) || want.size() != got.size()) {
                problem = TXF("selftest.fmt_bad", W("texture size").c_str());
            } else {
                double sum = 0;
                size_t cnt = 0;
                for (size_t i = 0; i < got.size(); i++) {
                    if (i % 4 == 3) continue;
                    sum += std::abs((int)got[i] - (int)(uint8_t)want[i]);
                    cnt++;
                }
                if (sum / cnt > 2.0) problem = TXF("selftest.fmt_bad", W("mean difference " + std::to_string(sum / cnt)).c_str());
            }
        } else {  // flottant : l'image ne doit pas etre noire
            size_t lit = 0;
            for (size_t i = 0; i < got.size(); i++)
                if (i % 4 != 3 && got[i] > 8) lit++;
            if (lit < 64) problem = TXF("selftest.fmt_bad", W("black texture").c_str());
        }
        const std::wstring label = TX("selftest.fmt_texture") + L" " + W(ext);
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + label + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
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
            if (!en.isDir && en.name.size() > 5 && en.name.compare(en.name.size() - 5, 5, L".glsl") == 0 &&
                en.name.find(L'.') == en.name.size() - 5)  // les passes <nom>.bufferA.glsl ne sont pas des tests
                shaders.push_back(en);
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
        if (!shaders.empty()) {  // les formats sont verifies avec circle.glsl (a defaut le premier shader)
            std::wstring pick = shaders[0].name;
            for (const auto& en : shaders)
                if (en.name == L"circle.glsl") pick = en.name;
            runFormatTests(exeDir, dir, dir + pick, report, total, fails);
        }
        destroyGLContext(gl);
    }
    {  // chaque langue disponible se charge et ses textes a marques de format s'affichent
        const std::string previous = i18n::language();
        std::wstring langs, problem;
        for (const auto& code : i18n::available()) {
            if (!i18n::setLanguage(code)) {
                problem += W(code) + L" ";
                continue;
            }
            const std::wstring a = TX("ui.btn.start"), b = TXF("status.progress", 1LL, 2LL, 3, L"4", L"5");
            if (a.empty() || a[0] == L'[' || b.empty() || b[0] == L'[') problem += W(code) + L" ";
            langs += W(code) + L" ";
        }
        i18n::setLanguage(previous);
        total++;
        if (problem.empty()) {
            report += L"PASS    " + TX("selftest.languages") + L": " + langs + L"\r\n";
        } else {
            fails++;
            report += L"FAIL    " + TX("selftest.languages") + L": " + problem + L"\r\n";
        }
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
