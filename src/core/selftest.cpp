// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "selftest.h"
#include "formats.h"
#include "gallery.h"
#include "updatecheck.h"
#include "crashinfo.h"
#include "fuzz.h"
#include "process.h"
#include "explain.h"
#include "gl.h"
#include "locate.h"
#include "montage.h"
#include "parts.h"
#include "source.h"
#include "dpx.h"
#include "ed25519.h"
#include "prolicense.h"
#include "proext.h"
#include "exr.h"
#include "exrtools.h"
#include "tifffloat.h"
#include "rhythm.h"
#include "params.h"
#include "expr.h"
#include "compare.h"
#include "looptools.h"
#include "quality.h"
#include "timemap.h"
#include "vr.h"
#include "batches.h"
#include "channeldrop.h"
#include "glslsyntax.h"
#include "passfiles.h"
#include "filesettings.h"
#include "outputopts.h"
#include "report.h"
#include "publish.h"
#include "webexport.h"
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

// Sous le detecteur de memoire tout est plusieurs fois plus lent : les cas qui comptent sur des delais courts (rechargement de l'apercu, partie lente) n'ont pas de sens
#ifdef __SANITIZE_ADDRESS__
static constexpr bool kTimingSensitiveOk = false;
#else
static constexpr bool kTimingSensitiveOk = true;
#endif

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

// Piste synthetique de l'analyse du rythme : un clic (sinus de 2 kHz) a `bpm` temps par minute, le premier a `offset` secondes ; un temps sur `perBar` est
// un accent (un grave de 80 Hz en plus) ; une nappe continue de 440 Hz dont le niveau est donne par `padGain(t)` dessine des sections de caractere
// different. Mono, 44100 Hz, 16 bits, ecrite en WAV.
bool writeClickTrack(const std::wstring& path, double seconds, double bpm, double offset, int perBar, const std::function<double(double)>& padGain) {
    const int rate = 44100;
    const size_t n = (size_t)(seconds * rate);
    std::vector<double> mix(n, 0.0);
    for (size_t i = 0; i < n; i++) mix[i] = padGain((double)i / rate) * std::sin(2 * 3.14159265358979 * 440.0 * (double)i / rate);
    const double period = 60.0 / bpm;
    for (int k = 0; offset + k * period < seconds - 0.06; k++) {
        const size_t at = (size_t)((offset + k * period) * rate);
        const bool accent = k % perBar == 0;
        for (size_t i = 0; i < (size_t)(0.06 * rate) && at + i < n; i++) {
            const double t = (double)i / rate;
            double v = 0.4 * std::exp(-t / 0.008) * std::sin(2 * 3.14159265358979 * 2000.0 * t);
            if (accent) v += 0.7 * std::exp(-t / 0.02) * std::sin(2 * 3.14159265358979 * 80.0 * t);
            mix[at + i] += v;
        }
    }
    std::string wav(44, '\0');
    auto put32 = [&](size_t pos, uint32_t v) { memcpy(&wav[pos], &v, 4); };
    auto put16 = [&](size_t pos, uint16_t v) { memcpy(&wav[pos], &v, 2); };
    memcpy(&wav[0], "RIFF", 4);
    put32(4, (uint32_t)(36 + n * 2));
    memcpy(&wav[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 1);
    put32(24, rate);
    put32(28, rate * 2);
    put16(32, 2);
    put16(34, 16);
    memcpy(&wav[36], "data", 4);
    put32(40, (uint32_t)(n * 2));
    for (size_t i = 0; i < n; i++) {
        const int16_t v = (int16_t)std::lround(std::min(0.95, std::max(-0.95, mix[i])) * 32767.0);
        wav.append((const char*)&v, 2);
    }
    FILE* f = platform::openFile(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(wav.data(), 1, wav.size(), f) == wav.size();
    fclose(f);
    return ok;
}

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
            if (kTimingSensitiveOk && (first < 200 || lastRed > 50 || logs < 1)) problem = TXF("selftest.fmt_bad", W("reload").c_str());
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
        // normalisation du son : le gain est fixe. Une musique douce puis forte (12 dB d'ecart) garde cet ecart apres normalisation (le filtre loudnorm
        // en une passe le reduisait : le volume montait et descendait)
        if (formatAvailable(ffmpeg, "h264", "mp4")) {
            const std::wstring music = outDir + sep + L"music_dyn.wav";
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -f lavfi -i sine=frequency=440:duration=10 -af \"volume='if(lt(t,5),0.1,0.4)':eval=frame\" -ar 44100 -ac 1 \"" +
                           music + L"\"",
                       nullptr);
            Settings st = base("h264", "mp4", 8);
            st.fps = 5;
            st.duration = 10;
            st.audio = music;
            st.loudnorm = -16;
            std::wstring e, problem;
            std::string logText;
            if (!fileExists(music)) problem = bad("no music");
            else if (!renderOne(renderShader, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                auto rms = [&](double from) {  // niveau efficace (dB) de 3 s du son de la video
                    std::string summary;
                    runProcess(L"\"" + ffmpeg + L"\" -hide_banner -nostats -nostdin " + wfmt(L"-ss %g -t 3 ", from) + L"-i \"" + outputFile(renderShader, st, false) +
                                   L"\" -vn -af astats=metadata=0 -f null -",
                               [&](const std::wstring& l) { summary += U8(l) + "\n"; });
                    const size_t p = summary.find("RMS level dB:");
                    double v = 0;
                    return p != std::string::npos && sscanf(summary.c_str() + p + 13, "%lf", &v) == 1 ? v : -1000.0;
                };
                const double soft = rms(1), loud = rms(6);
                const double expected = 20.0 * std::log10(4.0);  // 12 dB entre les deux moities
                if (soft < -900 || loud < -900) problem = bad("no level");
                else if (std::fabs((loud - soft) - expected) > 1.5) problem = bad("gain not constant: " + std::to_string(loud - soft) + " dB instead of " + std::to_string(expected));
            }
            check("selftest.fmt_loudnorm_constant", problem);
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
                if (problem.empty() && (tabs.size() != (proExt() ? 12u : 9u) || created != 3 || !passTabs(imageFile)[1].exists)) problem = bad("tabs");
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
        // --- Temps et boucle : courbe de temps, ralenti, boucles sans couture, recherche de periode, image fixe moyennee, vignette intelligente ---
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
                    for (const Case& c : {Case{"pingpong", "", true}, Case{"none", "0:2;2:0", true}, Case{"crossfade", "", false}, Case{"none", "0:0;2:1", true}}) {
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
        // --- Tres grandes images et projections -----------------------------------------------------------------------------------------
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
                int previewW = 0, previewH = 0;
                H.previewFrame = [&](int w, int h, std::vector<uint8_t>&&) {  // la carte va aussi a l'apercu
                    previewW = w;
                    previewH = h;
                };
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
                    else if (previewW != 960 || previewH != 540) problem = bad("preview " + std::to_string(previewW) + "x" + std::to_string(previewH));
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
                        const size_t pa = js.find("\"buffer_a\": ", g);
                        const double bufferA = pa != std::string::npos ? atof(js.c_str() + pa + 12) : -1.0;  // temps de la passe Buffer A seule
                        if (image <= 0 || buffers <= 0 || bufferA <= 0) problem = bad("passes " + std::to_string(image) + "," + std::to_string(buffers) + "," + std::to_string(bufferA));
                    }
                }
                check("selftest.fmt_gpupasses", problem);
            }
            // passes Buffer avec un temps qui recule : l'etat est rejoue depuis le debut, le meme etat revient au meme instant du shader, le cout est borne
            if (formatAvailable(ffmpeg, "h264", "png")) {
                std::wstring problem;
                const std::wstring counter = put(L"p16_counter.glsl", "// iChannel0=bufferA\nvoid mainImage(out vec4 o, in vec2 p){ float v = texture(iChannel0, p / iResolution.xy).r; o = vec4(v, v, v, 1.0); }\n");
                put(L"p16_counter.bufferA.glsl", "// iChannel0=bufferA\nvoid mainImage(out vec4 o, in vec2 p){ vec4 prev = texelFetch(iChannel0, ivec2(p), 0); o = vec4(prev.r + 0.01, 0.0, 0.0, 1.0); }\n");
                auto grayAtSeq = [&](const Settings& st, double t) {
                    const std::string px = decodeRaw(sequencePattern(outputFile(counter, st, false)), L"gray", wfmt(L"-framerate 10 -ss %.2f", t));
                    return px.size() == 64 * 64 ? (int)(uint8_t)px[32 * 64 + 32] : -1;
                };
                std::wstring e;
                std::string logText;
                Settings pp = seqSettings(4);
                pp.loopMode = "pingpong";  // aller-retour : instant du shader 0 -> 2 -> 0
                if (!renderOne(counter, pp, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const int a = grayAtSeq(pp, 1.0), b = grayAtSeq(pp, 2.0), c = grayAtSeq(pp, 3.0);  // etats apres 11, 21 et 11 pas (0.11 -> 28, 0.21 -> 54)
                    if (std::abs(a - 28) > 2 || std::abs(b - 54) > 2 || std::abs(c - a) > 1) problem = bad("pingpong " + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c));
                }
                if (problem.empty()) {  // courbe decroissante 2 s -> 0 s
                    Settings rv = seqSettings(2);
                    rv.timemap = "0:2;2:0";
                    if (!renderOne(counter, rv, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else {
                        const int a = grayAtSeq(rv, 0.0), b = grayAtSeq(rv, 1.0), c = grayAtSeq(rv, 1.9);
                        if (std::abs(a - 54) > 2 || std::abs(b - 28) > 3 || c > 12) problem = bad("reverse " + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c));
                    }
                }
                if (problem.empty()) {  // trop cher : aller-retour de 1000 s a 10 images par seconde
                    Settings big = seqSettings(1000);
                    big.loopMode = "pingpong";
                    big.chunk = 0;
                    if (renderOne(counter, big, e, logText) || e.find(L"400000") == std::wstring::npos) problem = bad("cost limit not enforced");
                }
                check("selftest.fmt_replay", problem);
            }
            // le son suit une vitesse constante : le passage fort (1.5 s) dure autant que la video ralentie (vitesse 0.5 : 2.4 s de video = 1.2 s de son)
            if (formatAvailable(ffmpeg, "h264", "mp4")) {
                std::wstring problem;
                const std::wstring rampA = put(L"p16_ramp.glsl", "void mainImage(out vec4 o, in vec2 p){ float v = clamp(iTime / 4.0, 0.0, 1.0); o = vec4(v, v, v, 1.0); }\n");
                const std::wstring tone = outDir + sep + L"p16_tone.wav";
                runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -f lavfi -i \"aevalsrc='if(lt(t,1.5),0.5*sin(2*PI*440*t),0)':s=44100:d=6\" \"" + tone + L"\"", nullptr);
                auto volume = [&](const std::wstring& file) {  // volume moyen (dB) entre 1.8 s et 2.2 s
                    double db = 0;
                    bool seen = false;
                    runProcess(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -ss 1.8 -t 0.4 -i \"" + file + L"\" -vn -af volumedetect -f null -", [&](const std::wstring& l) {
                        const std::string line = U8(l);
                        const size_t at = line.find("mean_volume: ");
                        if (at != std::string::npos) {
                            db = atof(line.c_str() + at + 13);
                            seen = true;
                        }
                    });
                    return seen ? db : 1.0;
                };
                double level[2] = {0, 0};
                for (int follow = 0; follow < 2 && problem.empty(); follow++) {
                    Settings sv = base("h264", "mp4", 8);
                    sv.duration = 2.4;
                    sv.speed = 0.5;
                    sv.audio = tone;
                    sv.audioFollow = follow == 1;
                    std::wstring e;
                    std::string logText;
                    if (!renderOne(rampA, sv, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else level[follow] = volume(outputFile(rampA, sv, false));
                }
                if (problem.empty() && (level[1] < -30 || level[0] > -60)) problem = bad("volume follow " + std::to_string(level[1]) + " / plain " + std::to_string(level[0]));
                check("selftest.fmt_audiofollow", problem);
            }
            // images fixes : passes Buffer en mode tuiles, formats TIFF et etiquette sRGB, fulldome d'un shader ordinaire
            if (formatAvailable(ffmpeg, "h264", "png")) {
                auto stillRun = [&](const std::wstring& shader, const Settings& st, std::wstring& e) {
                    Hooks Hs;
                    Hs.snapshot = true;
                    Hs.log = [](const std::wstring&) {};
                    return convertFile(shader, exeDir, st, Hs, e) == Result::Ok;
                };
                const std::wstring gradient = put(L"p17b_grad.glsl", "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; float k = mod(floor(p.x / 7.0) + floor(p.y / 5.0), 2.0); o = vec4(u.x, u.y, k * 0.5 + 0.25 * sin(iResolution.x * 0.01), 1.0); }\n");
                {  // une passe Buffer : calculee une fois a pleine taille, lue par chaque tuile ; trop grande -> refusee
                    std::wstring problem;
                    const std::wstring withPass = put(L"p17b_pass.glsl", "// iChannel0=bufferA\nvoid mainImage(out vec4 o, in vec2 p){ o = texture(iChannel0, p / iResolution.xy); }\n");
                    put(L"p17b_pass.bufferA.glsl", "void mainImage(out vec4 o, in vec2 p){ float k = mod(floor(p.x / 7.0) + floor(p.y / 5.0), 2.0); o = vec4(p / iResolution.xy, k, 1.0); }\n");
                    Settings plain = seqSettings(1);
                    plain.width = 96;
                    plain.height = 64;
                    std::wstring e;
                    if (!stillRun(withPass, plain, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else {
                        const std::string ref = decodeRaw(outputPath(withPass, plain.outdir, L".png"), L"rgb24", L"");
                        Settings tiled = plain;
                        tiled.tile = 32;
                        if (!stillRun(withPass, tiled, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                        else if (ref.size() != 96 * 64 * 3 || decodeRaw(outputPath(withPass, plain.outdir, L".png"), L"rgb24", L"") != ref) problem = bad("tiled pass differs");
                    }
                    if (problem.empty()) {
                        Settings huge = plain;
                        huge.width = huge.height = 9000;
                        if (stillRun(withPass, huge, e) || e.find(L"9000") == std::wstring::npos) problem = bad("large pass not refused");
                    }
                    check("selftest.fmt_tilepasses", problem);
                }
                {  // TIFF 8 et 16 bits (aussi par tuiles), memes pixels que le PNG, etiquette sRGB du PNG, resolution
                    std::wstring problem;
                    Settings png = seqSettings(1);
                    png.width = 96;
                    png.height = 64;
                    png.dpi = 300;
                    std::wstring e;
                    std::string pngBytes;
                    for (int deep = 0; deep < 2 && problem.empty(); deep++) {
                        png.depth = deep ? 10 : 8;
                        Settings tif = png;
                        tif.stillFormat = "tiff";
                        const wchar_t* pix = deep ? L"rgb48le" : L"rgb24";
                        if (!stillRun(gradient, png, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                        else {
                            const std::wstring pngFile = outputPath(gradient, png.outdir, L".png");
                            std::string bytes;
                            readFile(pngFile, bytes);
                            if (bytes.find("sRGB") == std::string::npos || bytes.find("pHYs") == std::string::npos) problem = bad("png tags");
                            const std::string refPixels = decodeRaw(pngFile, pix, L"");
                            if (problem.empty() && refPixels.size() != (size_t)96 * 64 * (deep ? 6 : 3)) problem = bad("png size");
                            for (int tile : {0, 32}) {
                                if (!problem.empty()) break;
                                tif.tile = tile;
                                const std::wstring tifFile = outputPath(gradient, tif.outdir, L".tif");
                                platform::removeFile(tifFile);
                                if (!stillRun(gradient, tif, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                                else {
                                    std::string head;
                                    readFile(tifFile, head);
                                    if (head.size() < 8 || head.compare(0, 4, std::string("II*\0", 4)) != 0) problem = bad("not a TIFF");
                                    else if (decodeRaw(tifFile, pix, L"") != refPixels) problem = bad(std::string("tiff differs deep=") + std::to_string(deep) + " tile=" + std::to_string(tile));
                                }
                            }
                        }
                    }
                    check("selftest.fmt_stillformat", problem);
                }
                {  // fulldome d'un shader plein ecran : le centre garde l'image, le coin est noir
                    std::wstring problem;
                    const std::wstring red = put(L"p17b_red.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(1.0, 0.0, 0.0, 1.0); }\n");
                    Settings st = seqSettings(1);
                    st.vr = "fulldome";
                    st.width = st.height = 64;
                    std::wstring e;
                    if (!stillRun(red, st, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else {
                        const std::string px = decodeRaw(outputPath(red, st.outdir, L".png"), L"rgb24", L"");
                        auto at = [&](int x, int y, int c) { return px.size() == 64 * 64 * 3 ? (int)(uint8_t)px[((size_t)y * 64 + x) * 3 + c] : -1; };
                        if (at(32, 32, 0) < 250 || at(32, 32, 1) > 5 || at(60, 32, 0) < 250 || at(1, 1, 0) > 3 || at(62, 62, 0) > 3) problem = bad("dome mask");
                    }
                    check("selftest.fmt_domeflat", problem);
                }
            }
            // --- Controle qualite, comparaison, images de reference, estimation ---------------------------------------------------------------------
            {
                Hooks quiet;
                quiet.log = [](const std::wstring&) {};
                auto find = [](const CheckReport& r, const char* kind) {
                    for (const CheckIssue& i : r.issues)
                        if (i.kind == kind) return &i;
                    return (const CheckIssue*)nullptr;
                };
                Settings cs = base("h264", "png", 8);
                cs.fps = 30;
                cs.duration = 3;
                cs.width = 128;
                cs.height = 72;
                {  // flashs, flashs rouges, valeurs invalides (lignes, carte), images noires, scintillement, aliasing, rapports
                    std::wstring problem;
                    const std::wstring flashing = put(L"p18_flash.glsl", "void mainImage(out vec4 o, in vec2 p){ float k = step(0.5, fract(iTime * 5.0)); o = vec4(vec3(k), 1.0); }\n");
                    const std::wstring gentle = put(L"p18_gentle.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(vec3(0.5 + 0.4 * sin(iTime * 3.0)), 1.0); }\n");
                    const std::wstring redFlash = put(L"p18_red.glsl", "void mainImage(out vec4 o, in vec2 p){ float k = step(0.5, fract(iTime * 4.0)); o = vec4(k, 0.0, 0.0, 1.0) + vec4(0.0, 0.2, 0.2, 0.0) * (1.0 - k); }\n");
                    const std::wstring broken = put(L"p18_nan.glsl", "void mainImage(out vec4 o, in vec2 p){\n    vec2 u = p / iResolution.xy;\n    float v = sqrt(u.x - 0.5);\n    o = vec4(v, u.y, 0.3, 1.0);\n}\n");
                    const std::wstring dark = put(L"p18_black.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.0, 0.0, 0.0, 1.0); }\n");
                    const std::wstring noisy = put(L"p18_noise.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(vec3(fract(sin(dot(p, vec2(12.9898, 78.233)) + iTime * 1000.0) * 43758.5453)), 1.0); }\n");
                    const std::wstring fine = put(L"p18_fine.glsl", "void mainImage(out vec4 o, in vec2 p){ float k = mod(floor(p.x) + floor(p.y), 2.0); o = vec4(vec3(k), 1.0); }\n");
                    std::wstring e;
                    CheckReport r;
                    CheckOptions all;
                    if (!runChecks(flashing, exeDir, cs, all, quiet, r, e) || !find(r, "flash") || r.maxFlashes < 4) problem = bad("flash " + std::to_string(r.maxFlashes) + " " + U8(e));
                    else if (!runChecks(gentle, exeDir, cs, all, quiet, r, e) || find(r, "flash") || find(r, "red_flash") || find(r, "pattern")) problem = bad("gentle shader flagged");
                    else if (!runChecks(redFlash, exeDir, cs, all, quiet, r, e) || !find(r, "red_flash")) problem = bad("red flash missed");
                    else if (!runChecks(broken, exeDir, cs, all, quiet, r, e) || !find(r, "nan") || r.suspectLines.empty() || r.mapFile.empty() || !fileExists(r.mapFile)) problem = bad("nan " + std::to_string(r.issues.size()));
                    else if (std::none_of(r.suspectLines.begin(), r.suspectLines.end(), [](const std::wstring& l) { return l.find(L"3") != std::wstring::npos; })) problem = bad("suspect line");  // la racine carree est a la ligne 3
                    else if (!runChecks(dark, exeDir, cs, all, quiet, r, e) || !find(r, "black") || find(r, "black")->severity < 1) problem = bad("black frames");
                    else if (!runChecks(noisy, exeDir, cs, all, quiet, r, e) || !find(r, "flicker")) problem = bad("flicker");
                    else if (!runChecks(fine, exeDir, cs, all, quiet, r, e) || !find(r, "aliasing")) problem = bad("aliasing");
                    else if (!runChecks(put(L"p18_stripes.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(vec3(step(0.5, fract(p.x / 8.0))), 1.0); }\n"), exeDir, cs, all, quiet, r, e) || !find(r, "pattern")) problem = bad("stripe pattern");
                    else {  // portabilite : demi-precision de l'horloge (shader qui lit iTime, duree au-dela de 17 s), rapports ecrits
                        Settings longRun = cs;
                        longRun.duration = 30;
                        Settings farRun = cs;  // tres loin de zero : l'horloge arrondie en demi-precision se voit
                        farRun.start = 1000;
                        farRun.duration = 3;
                        if (!runChecks(gentle, exeDir, longRun, all, quiet, r, e) || !find(r, "mediump") || find(r, "mediump")->severity != 0) problem = bad("mediump note");
                        else if (!runChecks(gentle, exeDir, farRun, all, quiet, r, e) || !find(r, "mediump_sim")) problem = bad("mediump simulation");
                        else if (std::fabs(roundToHalf(1000.3) - 1000.5) > 1e-9 || std::fabs(roundToHalf(0.1) - 0.0999755859375) > 1e-12) problem = bad("half rounding");
                        else {
                            runChecks(redFlash, exeDir, cs, all, quiet, r, e);
                            const std::wstring stem = outDir + sep + L"p18_report";
                            std::string js, tx;
                            if (!writeCheckReport(r, stem) || !readFile(stem + L".check.json", js) || !readFile(stem + L".check.txt", tx) || js.find("\"red_flash\"") == std::string::npos || tx.empty())
                                problem = bad("report files");
                        }
                    }
                    check("selftest.fmt_check", problem);
                }
                // avertissement avant le rendu : le journal le dit, le rendu a lieu
                if (formatAvailable(ffmpeg, "h264", "png")) {
                    std::wstring problem;
                    const std::wstring flashing = put(L"p18_flash.glsl", "void mainImage(out vec4 o, in vec2 p){ float k = step(0.5, fract(iTime * 5.0)); o = vec4(vec3(k), 1.0); }\n");
                    Settings st = seqSettings(1);
                    st.precheck = true;
                    std::wstring e;
                    std::string logText;
                    if (!renderOne(flashing, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else if (logText.find("! ") == std::string::npos) problem = bad("no warning in the log");
                    else if (!platform::dirExists(outputFile(flashing, st, false).substr(0, outputFile(flashing, st, false).find_last_of(L"\\/")))) problem = bad("no output");
                    check("selftest.fmt_precheck", problem);
                }
                {  // comparaison : identiques, petit ecart trouve, image de difference, video, tailles differentes
                    std::wstring problem;
                    const std::string code = "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; o = vec4(u.x, u.y, 0.5 + 0.5 * sin(iTime), 1.0); }\n";
                    const std::wstring a = put(L"p18_cmp_a.glsl", code), b = put(L"p18_cmp_b.glsl", code);
                    const std::wstring c = put(L"p18_cmp_c.glsl", "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; o = vec4(u.x, u.y, 0.5 + 0.5 * sin(iTime) + 0.02, 1.0); }\n");
                    const std::wstring wide = put(L"p18_cmp_w.glsl", "// width=200\n" + code);
                    Settings sp = base("h264", "png", 8);
                    sp.width = 160;
                    sp.height = 90;
                    sp.duration = 4;
                    CompareOptions co;
                    CompareResult cr;
                    std::wstring e;
                    if (!compareShaders(a, b, exeDir, sp, co, quiet, cr, e) || !cr.identical || cr.different || cr.frames != 12) problem = bad("identical " + U8(e));
                    else if (!compareShaders(a, c, exeDir, sp, co, quiet, cr, e) || cr.identical || !cr.different || cr.maxAbs < 3 || cr.maxAbs > 8 || cr.diffFile.empty() || !fileExists(cr.diffFile))
                        problem = bad("small change " + std::to_string(cr.maxAbs));
                    else {
                        CompareOptions loose = co;
                        loose.tolerance = 10;
                        if (!compareShaders(a, c, exeDir, sp, loose, quiet, cr, e) || cr.different) problem = bad("tolerance");
                    }
                    if (problem.empty() && formatAvailable(ffmpeg, "h264", "mp4")) {  // video cote a cote : 1 s, deux fois la largeur
                        CompareOptions v = co;
                        v.video = "sbs";
                        v.videoSeconds = 1;
                        if (!compareShaders(a, c, exeDir, sp, v, quiet, cr, e) || cr.videoFile.empty()) problem = bad("video " + U8(e));
                        else {
                            const std::string info = probeInfo(ffmpeg, cr.videoFile);
                            const double d = probeDuration(info);
                            if (info.find("320x90") == std::string::npos || d < 0.9 || d > 1.2) problem = bad("video size or duration " + std::to_string(d));
                        }
                    }
                    if (problem.empty() && (compareShaders(a, wide, exeDir, sp, co, quiet, cr, e) || e.empty())) problem = bad("size mismatch accepted");
                    check("selftest.fmt_compare", problem);
                }
                {  // images de reference : enregistrees, verifiees, un changement est vu, une reference absente est signalee
                    std::wstring problem;
                    const std::wstring g = put(L"p18_gold.glsl", "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; o = vec4(u.x, u.y, 0.5 + 0.5 * sin(iTime), 1.0); }\n");
                    Settings sp = base("h264", "png", 8);
                    sp.width = 160;
                    sp.height = 90;
                    sp.duration = 4;
                    sp.outdir = outDir + sep + L"golden_case";
                    platform::makeDir(sp.outdir);
                    GoldenResult gr;
                    std::wstring e;
                    if (!goldenRecord(g, exeDir, sp, 5, quiet, gr, e) || !fileExists(gr.dir + L"/golden.txt") || !fileExists(gr.dir + L"/frame_4.png")) problem = bad("record " + U8(e));
                    else if (!goldenVerify(g, exeDir, sp, 1.0, quiet, gr, e) || !gr.ok || gr.frames != 5) problem = bad("verify " + U8(e) + " " + std::to_string(gr.worstMean));
                    else {
                        put(L"p18_gold.glsl", "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; o = vec4(u.x, u.y, 0.5 + 0.5 * sin(iTime) + 0.05, 1.0); }\n");
                        if (!goldenVerify(g, exeDir, sp, 1.0, quiet, gr, e) || gr.ok || gr.worstMean < 3) problem = bad("change not seen " + std::to_string(gr.worstMean));
                        else if (goldenVerify(put(L"p18_nogold.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(1.0); }\n"), exeDir, sp, 1.0, quiet, gr, e) || e.empty()) problem = bad("missing reference accepted");
                    }
                    check("selftest.fmt_golden", problem);
                }
                {  // estimation : bornes autour de la vitesse mesuree, alerte de lenteur avec propositions, mesure reelle sur un petit rendu
                    std::wstring problem;
                    Settings big = base("h264", "mp4", 8);
                    big.width = 1920;
                    big.height = 1080;
                    big.fps = 60;
                    big.ss = 3;
                    big.blur = 4;
                    Estimate est;
                    est.frames = 600;
                    est.videoSeconds = 10;
                    const double slowSpeeds[3] = {0.2, 0.25, 0.3};
                    estimateFromSpeeds(big, slowSpeeds, 3, est);
                    if (!est.slow || est.suggestions.size() != 3 || !(est.secLow < est.secMid && est.secMid < est.secHigh) || std::fabs(est.secMid - 600 / 0.25) > 600) problem = bad("slow alert");
                    else {
                        const double fastSpeeds[3] = {500, 600, 700};
                        Estimate quick;
                        quick.frames = 600;
                        quick.videoSeconds = 10;
                        estimateFromSpeeds(big, fastSpeeds, 3, quick);
                        if (quick.slow || !quick.suggestions.empty()) problem = bad("false alert");
                    }
                    if (problem.empty() && formatAvailable(ffmpeg, "h264", "mp4")) {
                        const std::wstring circle = put(L"p18_est.glsl", "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy - 0.5; o = vec4(vec3(smoothstep(0.3, 0.29, length(u))), 1.0); }\n");
                        Settings sp = base("h264", "mp4", 8);
                        sp.duration = 20;
                        sp.fps = 10;
                        std::wstring e;
                        if (!estimateRender(circle, exeDir, sp, quiet, est, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                        else if (est.frames != 200 || est.fpsMean <= 0 || est.secLow > est.secMid || est.secMid > est.secHigh || std::fabs(est.secMid - 200.0 / est.fpsMean) > 0.01) problem = bad("estimate bounds");
                        else if (platform::dirExists(exeDir + L"estimate_tmp")) problem = bad("temporary folder left");
                    }
                    check("selftest.fmt_estimate", problem);
                }
            }
        }
    }
    // Publication : calculs purs, poids cible, maitre en cache, source embarquee et verification, recadrage, export HTML
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
        auto renderOne = [&](const std::wstring& shaderPath, const Settings& st, std::wstring& e, std::string& logText) {
            Hooks H;
            H.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
            return convertFile(shaderPath, exeDir, st, H, e) == Result::Ok;
        };
        {  // calculs : tailles, debit, correction, recadrage, cibles, licence, paquet de source
            std::wstring problem;
            double mb = 0;
            BitratePlan plan;
            int cw = 0, ch = 0;
            std::vector<Target> targets;
            LicenseInfo lic = detectLicense("// Title: Plasma - by Someone\n// License: CC BY-NC-SA 3.0\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(1.0); }\n");
            LicenseInfo none = detectLicense("void mainImage(out vec4 o, in vec2 p){ o = vec4(1.0); }\n");
            SourceBundle b, b2;
            b.appVersion = "1.10.0";
            b.shaderName = "x.glsl";
            b.files = {{"x.glsl", std::string("void mainImage(out vec4 o, in vec2 p){ o = vec4(0.1, 0.2, 0.3, 1.0); }\n\0binaire", 70)}, {"x.bufferA.glsl", "// a\n"}};
            b.settingsIni = "fps=30\nduration=2\n";
            if (!parseSizeMB("8MB", mb) || std::fabs(mb - 8) > 1e-9 || !parseSizeMB("800KB", mb) || std::fabs(mb - 0.8) > 1e-9 || !parseSizeMB("1.5GB", mb) || std::fabs(mb - 1500) > 1e-6 ||
                parseSizeMB("beaucoup", mb))
                problem = bad("parseSizeMB");
            else if (!planBitrate(8, 10, 128, plan) || plan.videoKbps < 5000 || plan.videoKbps > 6500 || planBitrate(0.01, 600, 128, plan))
                problem = bad("planBitrate " + std::to_string(plan.videoKbps));
            else if (correctedBitrate(1000, 1200000, 1000000) >= 1000 || correctedBitrate(1000, 800000, 1000000) <= 1000)
                problem = bad("correctedBitrate");
            else if (!cropSize("9:16", 1920, 1080, cw, ch) || cw != 606 || ch != 1080 || cropSize("banane", 1920, 1080, cw, ch))
                problem = bad("cropSize " + std::to_string(cw) + "x" + std::to_string(ch));
            else if (!parseTargets("[mine]\nlabel=Ma cible\nwidth=720\nheight=1280\nmaxduration=30\nmaxsize=8\ncodec=h264\ncontainer=mp4\n", targets) || targets.size() != 1 ||
                     targets[0].maxMB != 8 || targets[0].height != 1280)
                problem = bad("parseTargets");
            else if (!findTarget(publishTargets(exeDir), "tiktok") && !findTarget(publishTargets(exeDir), "youtube-shorts"))
                problem = bad("built-in targets");
            else if (!lic.found || !lic.nonCommercial || lic.assumed || none.found == lic.found)
                problem = bad("detectLicense " + lic.id);
            else if (!bundleFromJson(bundleToJson(b), b2) || b2.files != b.files || b2.settingsIni != b.settingsIni || b2.appVersion != b.appVersion)
                problem = bad("bundle round trip");
            else {
                Settings s;
                applyTarget(s, targets[0], {"fps"});
                if (s.height != 1280 || s.targetSizeMB != 8 || checkTarget(targets[0], s, 45, 720, 1280).size() != 1) problem = bad("applyTarget / checkTarget");
            }
            check("selftest.fmt_publish_units", problem);
        }
        if (formatAvailable(ffmpeg, "h264", "mp4")) {
            // un bruit change a chaque image : le poids ne peut pas etre atteint par hasard
            const std::wstring noisy = put(L"p19_noise.glsl",
                                           "float h(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }\n"
                                           "void mainImage(out vec4 o, in vec2 p){ o = vec4(vec3(h(p + floor(iTime * 10.0))), 1.0); }\n");
            {  // poids cible : le fichier tient dans le poids demande, sans etre vide
                std::wstring problem, e;
                std::string logText;
                Settings st = base("h264", "mp4", 8);
                st.width = 320;
                st.height = 180;
                st.duration = 2;
                st.fps = 20;
                st.targetSizeMB = 0.25;
                st.name = L"p19_target";
                if (!renderOne(noisy, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    uint64_t sz = 0;
                    platform::fileSize(outputFile(noisy, st, false), sz);
                    if (sz == 0 || sz > 250000 || sz < 40000) problem = bad("size " + std::to_string(sz));
                }
                check("selftest.fmt_targetsize", problem);
            }
            {  // cible de publication : la taille, le codec et le poids de la plate-forme sont respectes par le rendu
                std::wstring problem, e;
                std::string logText;
                Settings st = base("h265", "mkv", 8);
                st.target = "discord-8mb";
                st.duration = 1;
                st.fps = 10;
                st.name = L"p19_discord";
                if (!renderOne(noisy, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    Settings applied = st;  // le moteur applique la cible : le nom du fichier suit son conteneur
                    const std::vector<Target> all = publishTargets(exeDir);
                    if (const Target* tg = findTarget(all, st.target)) applyTarget(applied, *tg, {});
                    const std::wstring video = outputFile(noisy, applied, false);
                    uint64_t sz = 0;
                    platform::fileSize(video, sz);
                    const std::string info = probeInfo(ffmpeg, video);
                    if (info.find("1280x720") == std::string::npos || info.find("h264") == std::string::npos) problem = bad("format " + info.substr(0, 160));
                    else if (sz == 0 || sz > 8000000) problem = bad("size " + std::to_string(sz));
                    else if (video.size() < 4 || video.substr(video.size() - 4) != L".mp4") problem = bad("container");
                }
                check("selftest.fmt_target", problem);
            }
            {  // licence, credit et apercu anime : l'en-tete du shader est lu, la licence NC est signalee, le credit est incruste, le GIF est tire de la video
                std::wstring problem, e;
                std::string logText;
                const std::wstring lic = put(L"p19_license.glsl", "// Shadertoy: Plasma - Someone - https://www.shadertoy.com/view/abcdef\n// License: CC BY-NC-SA 3.0\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(p / iResolution.xy, 0.5, 1.0); }\n");
                Settings st = base("h264", "mp4", 8);
                st.autoCredit = true;
                st.animPreview = "gif";
                st.duration = 1;
                st.name = L"p19_license_out";
                if (!renderOne(lic, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::wstring video = outputFile(lic, st, false);
                    const std::wstring gif = video.substr(0, video.rfind(L'.')) + L"_preview.gif";
                    uint64_t gifSize = 0;
                    platform::fileSize(gif, gifSize);
                    if (logText.find(U8(TXF("log.credit_auto", L"")).substr(0, 8)) == std::string::npos || logText.find("Someone") == std::string::npos) problem = bad("credit not written");
                    else if (logText.find(U8(TX("log.license_nc")).substr(0, 12)) == std::string::npos) problem = bad("non-commercial warning");
                    else if (gifSize < 100 || probeInfo(ffmpeg, gif).find("gif") == std::string::npos) problem = bad("animated preview");
                }
                check("selftest.fmt_license", problem);
            }
            {  // maitre en cache : le deuxieme format ne relance pas le rendu GPU, le contenu concorde
                std::wstring problem, e;
                std::string logText1, logText2;
                const std::wstring grad = put(L"p19_master.glsl", "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; o = vec4(u, 0.5 + 0.5 * sin(iTime * 3.0), 1.0); }\n");
                Settings a = base("h264", "mp4", 8);
                a.masterCache = true;
                a.name = L"p19_m1";
                Settings c = base("h264", "mkv", 8);
                c.masterCache = true;
                c.name = L"p19_m2";
                if (!renderOne(grad, a, e, logText1) || !renderOne(grad, c, e, logText2)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else if (logText2.find(U8(TXF("log.master_reuse", L"")).substr(0, 12)) == std::string::npos) problem = bad("master not reused");
                else {
                    const std::string px1 = [&] {
                        const std::wstring raw = outDir + sep + L"m1.raw";
                        runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + outputFile(grad, a, false) + L"\" -frames:v 1 -f rawvideo -pix_fmt rgb24 \"" + raw + L"\"", nullptr);
                        std::string s;
                        readFile(raw, s);
                        return s;
                    }();
                    const std::string px2 = [&] {
                        const std::wstring raw = outDir + sep + L"m2.raw";
                        runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + outputFile(grad, c, false) + L"\" -frames:v 1 -f rawvideo -pix_fmt rgb24 \"" + raw + L"\"", nullptr);
                        std::string s;
                        readFile(raw, s);
                        return s;
                    }();
                    long long sum = 0;
                    for (size_t i = 0; i < px1.size() && i < px2.size(); i++) sum += std::abs((int)(uint8_t)px1[i] - (int)(uint8_t)px2[i]);
                    if (px1.size() < 64 * 64 * 3 || px1.size() != px2.size() || sum / (double)px1.size() > 3) problem = bad("content differs");
                }
                check("selftest.fmt_master", problem);
            }
            {  // source embarquee : relue dans la video, identique ; la verification passe puis voit un fichier abime
                std::wstring problem, e;
                std::string logText;
                const std::string src = "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.9, 0.3, 0.1, 1.0); }\n";
                const std::wstring shaderPath = put(L"p19_embed.glsl", src);
                Settings st = base("h264", "mp4", 8);
                st.embed = true;
                st.report = "json";
                st.name = L"p19_embed_out";
                if (!renderOne(shaderPath, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::wstring video = outputFile(shaderPath, st, false);
                    SourceBundle got;
                    VerifyResult vr;
                    std::wstring ve;
                    if (!readBundleFromVideo(ffmpeg, video, got, ve) || got.files.empty() || got.files[0].second != src) problem = bad("embedded source " + U8(ve));
                    else if (!verifyVideo(ffmpeg, video, vr, ve) || !vr.problems.empty() || vr.sha256.size() != 64 || vr.frames < 4) problem = bad("verify " + U8(ve) + " " + std::to_string(vr.problems.size()));
                    else {
                        std::string bytes;
                        readFile(video, bytes);
                        if (bytes.size() > 200) {
                            bytes[bytes.size() / 2] ^= 0x55;
                            bytes.push_back('x');
                        }
                        const std::wstring broken = outDir + sep + L"p19_embed_broken.mp4";
                        writeFileAtomic(broken, bytes);
                        std::string rtext;
                        const std::wstring stem = video.substr(0, video.rfind(L'.'));
                        if (readFile(stem + L".report.json", rtext)) writeFileAtomic(outDir + sep + L"p19_embed_broken.report.json", rtext);
                        VerifyResult vb;
                        verifyVideo(ffmpeg, broken, vb, ve);
                        if (vb.problems.empty()) problem = bad("damaged file accepted");
                    }
                }
                check("selftest.fmt_embed", problem);
            }
            {  // recadrage 9:16 d'un rendu 320x180 : image de 100x180 (largeur paire), contenu pris au centre
                std::wstring problem, e;
                std::string logText;
                const std::wstring ramp = put(L"p19_crop.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(p.x / iResolution.x, 0.0, 0.0, 1.0); }\n");
                Settings st = base("h264", "mp4", 8);
                st.width = 320;
                st.height = 180;
                st.crop = "9:16";
                st.name = L"p19_crop_out";
                if (!renderOne(ramp, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::wstring video = outputFile(ramp, st, false);
                    const std::string info = probeInfo(ffmpeg, video);
                    const std::string px = [&] {
                        const std::wstring raw = outDir + sep + L"crop.raw";
                        runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + video + L"\" -frames:v 1 -f rawvideo -pix_fmt rgb24 \"" + raw + L"\"", nullptr);
                        std::string s;
                        readFile(raw, s);
                        return s;
                    }();
                    if (info.find("100x180") == std::string::npos) problem = bad("size " + info.substr(0, 200));
                    else if (px.size() < 3 * 90 * 90) problem = bad("no frame");
                    else {
                        const int left = (uint8_t)px[0], right = (uint8_t)px[99 * 3];  // rampe rendue sur 320 pixels : colonnes 110 et 209 du centre
                        if (std::abs(left - 88) > 12 || std::abs(right - 167) > 12) problem = bad("red " + std::to_string(left) + " / " + std::to_string(right));
                    }
                }
                check("selftest.fmt_crop", problem);
            }
        }
        {  // export HTML : une page autonome qui contient le shader, et un refus clair pour ce que la page ne sait pas faire
            std::wstring problem, e;
            const std::wstring okShader = put(L"p19_web.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(p / iResolution.xy, 0.5 + 0.5 * sin(iTime), 1.0); }\n");
            const std::wstring vrShader = put(L"p19_web_vr.glsl", "void mainVR(out vec4 o, in vec2 p, in vec3 ro, in vec3 rd){ o = vec4(rd, 1.0); }\n");
            const std::wstring page = outDir + sep + L"p19_web.html";
            Settings st = base("h264", "mp4", 8);
            std::string html;
            if (!exportHtml(okShader, exeDir, st, page, e)) problem = bad("export " + U8(e));
            else if (!readFile(page, html) || html.find("mainImage") == std::string::npos || html.find("webgl2") == std::string::npos) problem = bad("page content");
            else if (exportHtml(vrShader, exeDir, st, outDir + sep + L"p19_web_vr.html", e) || e.empty()) problem = bad("mainVR accepted");
            else if (exportHtml(put(L"p19_web_beat.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(iBeat, iBass, 0.0, 1.0); }\n"), exeDir, st, outDir + sep + L"p19_web_beat.html", e) || e.empty()) problem = bad("rhythm variables accepted");
            check("selftest.fmt_html", problem);
        }
    }
    // Son et rythme : analyse d'une piste de clics a tempo connu (120 temps par minute, premier clic a 0,13 s, accent tous les quatre temps,
    // trois sections de huit secondes), variables du shader, expressions lissees, marqueurs, montage cale sur la musique, corrections a la main
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
        auto check = [&](const char* key, const std::wstring& problem, const std::wstring& extra = L"") {
            total++;
            report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX(key) + extra + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
            if (!problem.empty()) fails++;
        };
        auto bad = [&](const std::string& what) { return TXF("selftest.fmt_bad", W(what).c_str()); };
        auto renderOne = [&](const std::wstring& shaderPath, const Settings& st, std::wstring& e, std::string& logText) {
            Hooks H;
            H.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
            return convertFile(shaderPath, exeDir, st, H, e) == Result::Ok;
        };
        auto preview = [&](const std::wstring& shaderPath, double time, std::vector<uint8_t>& got, std::wstring& e) {
            Settings st = base("h264", "mp4", 8);
            st.width = 16;
            st.height = 16;
            Hooks H;
            H.log = [](const std::wstring&) {};
            H.previewTime = time;
            H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { got = std::move(px); };
            return convertFile(shaderPath, exeDir, st, H, e) == Result::Ok && got.size() == 16 * 16 * 4;
        };
        const std::wstring wav = outDir + sep + L"p20_click.wav";
        const std::string wavUtf8 = U8(wav);
        auto pad = [](double t) { return t >= 8 && t < 16 ? 0.30 : 0.04; };
        platform::removeFile(wav + L".beats.json");
        const bool made = writeClickTrack(wav, 24.0, 120.0, 0.13, 4, pad);
        auto nearErr = [](const std::vector<double>& v, double t) { return std::fabs(nearestOf(v, t) - t); };
        Rhythm rh;
        std::wstring e;
        bool analysed = made && rh.load(ffmpeg, wav, 0, 4, e);
        {  // tempo, temps forts, mesures et sections de la piste : decalage maximal accepte 30 ms
            std::wstring problem;
            double maxErr = 0;
            if (!made) problem = bad("no click track");
            else if (!analysed) problem = e.empty() ? bad("analysis") : e;
            else {
                const RhythmGrid& g = rh.grid();
                int matched = 0, count = 0;
                for (double t = 0.13; t < 23.9; t += 0.5) {
                    count++;
                    const double d = nearErr(g.beats, t);
                    maxErr = std::max(maxErr, d);
                    if (d < 0.03) matched++;
                }
                double barErr = 0;
                for (double t = 2.13; t < 23.9; t += 2.0) barErr = std::max(barErr, nearErr(g.bars, t));
                if (std::fabs(g.bpm - 120.0) > 0.5) problem = bad("bpm " + std::to_string(g.bpm));
                else if (matched != count) problem = bad("beats " + std::to_string(matched) + "/" + std::to_string(count) + " max " + std::to_string(maxErr * 1000) + " ms");
                else if (barErr > 0.03 || g.bars.size() < 11) problem = bad("bars: first beat of the bar off by " + std::to_string(barErr * 1000) + " ms");
                else if (g.sections.size() != 3 || std::fabs(g.sections[1] - 8.13) > 0.45 || std::fabs(g.sections[2] - 16.13) > 0.45) problem = bad("sections " + std::to_string(g.sections.size()));
                else if (g.onsets.size() < 40) problem = bad("onsets " + std::to_string(g.onsets.size()));
                else if (!fileExists(wav + L".beats.json")) problem = bad("no .beats.json");
            }
            check("selftest.fmt_rhythm", problem, wfmt(L" (%.0f ms)", maxErr * 1000));
        }
        {  // fiche relue (meme grille), valeurs du shader (phase, attaque, compte), tempo force, determinisme
            std::wstring problem;
            Rhythm again, forced;
            if (!analysed) problem = bad("no analysis");
            else if (!again.load(ffmpeg, wav, 0, 4, e) || !again.fromCache() || again.grid().beats != rh.grid().beats || again.grid().sections != rh.grid().sections)
                problem = bad("cache read " + U8(e));
            else {
                const RhythmValues mid = again.at(3.38), click = again.at(3.16), twice = rh.at(3.38);
                if (mid.beat < 0.4 || mid.beat > 0.6 || mid.beatCount != 7 || std::fabs(mid.bpm - 120.0f) > 1.5f) problem = bad("beat " + std::to_string(mid.beat) + " count " + std::to_string(mid.beatCount));
                else if (click.onset < 0.4f || mid.onset > 0.35f) problem = bad("onset " + std::to_string(click.onset) + " / " + std::to_string(mid.onset));
                else if (memcmp(&mid, &twice, sizeof(mid)) != 0) problem = bad("not deterministic");
                else if (!forced.load(ffmpeg, wav, 60.0, 4, e) || std::fabs(forced.grid().bpm - 60.0) > 0.5) problem = bad("forced tempo " + std::to_string(forced.grid().bpm));
                else {
                    double off = 0;
                    for (size_t k = 2; k + 2 < forced.grid().beats.size(); k++) off = std::max(off, std::min(std::fabs(std::fmod(forced.grid().beats[k] - 0.13 + 1000.0, 0.5)),
                                                                                                    0.5 - std::fabs(std::fmod(forced.grid().beats[k] - 0.13 + 1000.0, 0.5))));
                    if (off > 0.03) problem = bad("forced tempo phase " + std::to_string(off * 1000) + " ms");
                }
            }
            check("selftest.fmt_rhythm_values", problem);
        }
        {  // corrections a la main : un temps supprime, un temps ajoute, relus dans la fiche, effaces
            std::wstring problem;
            Rhythm a, b, c;
            if (!a.load(ffmpeg, wav, 0, 4, e)) problem = e;
            else {
                const size_t n0 = a.grid().beats.size();
                a.toggleBeat(5.13, 0.1);
                const bool removed = a.grid().beats.size() == n0 - 1 && nearErr(a.grid().beats, 5.13) > 0.2;
                if (!removed) problem = bad("remove");
                else if (!b.load(ffmpeg, wav, 0, 4, e) || !b.fromCache() || b.grid().beats.size() != n0 - 1 || b.edits().removed.size() != 1) problem = bad("edits not read back");
                else {
                    b.toggleBeat(5.38, 0.1);
                    if (b.grid().beats.size() != n0 || nearErr(b.grid().beats, 5.38) > 0.006) problem = bad("add");
                    else {
                        b.clearEdits();
                        if (!c.load(ffmpeg, wav, 0, 4, e) || !c.edits().empty() || c.grid().beats != rh.grid().beats) problem = bad("clear");
                    }
                }
            }
            check("selftest.fmt_rhythm_edit", problem);
        }
        {  // expressions : iBass * 0.5 + 0.2, seuil, montee et descente lissees (fonction du temps seulement), erreurs de saisie
            std::wstring problem;
            Expr ex;
            std::string pe;
            ExprEnv env;
            env.v[EV_BASS] = 0.4;
            env.v[EV_ENERGY] = 0.9;
            if (!ex.parse("iBass*0.5+0.2", pe) || std::fabs(ex.eval(env) - 0.4) > 1e-9 || !ex.usesRhythm()) problem = bad("expr basic");
            else if (!ex.parse("max(iBass, 0.5) - smoothstep(0, 1, iEnergy) * 2 ^ 2 / 4", pe) || std::fabs(ex.eval(env) - (0.5 - (0.9 * 0.9 * (3 - 1.8)) * 4 / 4)) > 1e-9) problem = bad("expr functions");
            else if (ex.parse("iBass*", pe) || ex.parse("foo(1)", pe) || ex.parse("iBass + zzz", pe) || ex.parse("", pe)) problem = bad("expr errors");
            else {
                const std::string code = "// audio=" + wavUtf8 + "\n// uniform float e = iEnergy attack=0.01 release=1.0 [0..1]\n// uniform float g = iBass threshold=0.9 [0..1]\n"
                                         "// uniform float k = iBass*0.5+0.2 [0..1]\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(e, g, k, 1.0); }\n";
                Settings st = base("h264", "mp4", 8);
                parseLines(st, code, true);
                ParamSet ps;
                std::wstring le;
                double a[4], b[4], c[4];
                if (!ps.load(code, st, L"", le) || !ps.needsRhythm() || !ps.setAudio(ffmpeg, wav, le)) problem = bad("params " + U8(le));
                else {
                    ps.valueAt("e", 16.6, a);
                    ps.valueAt("e", 3.0, c);
                    ps.valueAt("e", 16.6, b);
                    const double raw = ps.rhythm()->at(16.6).energy;
                    double gate[4], kk[4];
                    ps.valueAt("g", 3.5, gate);
                    ps.valueAt("k", 4.15, kk);
                    const double wantK = std::min(1.0, ps.rhythm()->at(4.15).bass * 0.5 + 0.2);
                    if (a[0] != b[0]) problem = bad("smoothing not a pure function of time");
                    else if (a[0] < raw + 0.05) problem = bad("release " + std::to_string(a[0]) + " raw " + std::to_string(raw));
                    else if (gate[0] != 0) problem = bad("threshold " + std::to_string(gate[0]));
                    else if (std::fabs(kk[0] - wantK) > 1e-6 || kk[0] <= 0.21) problem = bad("expression " + std::to_string(kk[0]) + " want " + std::to_string(wantK));
                    else {  // une autre expression change la signature du rendu par parties
                        ParamSet other;
                        std::string code2 = code;
                        code2.replace(code2.find("iBass*0.5+0.2"), 13, "iBass*0.6+0.2");
                        if (!other.load(code2, st, L"", le) || other.signature() == ps.signature()) problem = bad("signature");
                    }
                }
            }
            check("selftest.fmt_rhythm_expr", problem);
        }
        if (analysed) {  // le shader lit iBeat, iOnset, iBeatCount sans rien declarer, au temps exact de l'image ; une declaration de l'auteur n'est pas doublee
            std::wstring problem;
            const std::wstring sh = put(L"p20_vars.glsl", "// audio=" + wavUtf8 + "\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(iBeat, iOnset, float(iBeatCount) / 255.0, 1.0); }\n");
            const std::wstring own = put(L"p20_own.glsl", "// audio=" + wavUtf8 + "\nconst float iBass = 0.25;\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(iBeat, 0.0, iBass, 1.0); }\n");
            const std::wstring expr = put(L"p20_expr.glsl", "// audio=" + wavUtf8 + "\n// uniform float k = iBass*0.5+0.2 [0..1]\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(k, 0.0, 0.0, 1.0); }\n");
            std::vector<uint8_t> got, got2, got3;
            for (double t : {3.38, 3.16}) {
                got.clear();
                const RhythmValues v = rh.at(t);
                if (!preview(sh, t, got, e)) {
                    problem = e.empty() ? TX("selftest.render_failed") : e;
                    break;
                }
                if (std::abs((int)got[2] - (int)std::lround(v.beat * 255.0f)) > 2 || std::abs((int)got[1] - (int)std::lround(v.onset * 255.0f)) > 2 || std::abs((int)got[0] - v.beatCount) > 1) {
                    problem = bad("t=" + std::to_string(t) + " rgb " + std::to_string(got[2]) + "," + std::to_string(got[1]) + "," + std::to_string(got[0]) + " want " +
                                  std::to_string(v.beat * 255.0f) + "," + std::to_string(v.onset * 255.0f) + "," + std::to_string(v.beatCount));
                    break;
                }
            }
            if (problem.empty()) {
                if (!preview(own, 3.38, got2, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else if (std::abs((int)got2[0] - 64) > 2) problem = bad("own declaration " + std::to_string(got2[0]));
                else if (!preview(expr, 4.15, got3, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else if (std::abs((int)got3[2] - (int)std::lround(std::min(1.0, rh.at(4.15).bass * 0.5 + 0.2) * 255.0)) > 2) problem = bad("expression in the shader " + std::to_string(got3[2]));
            }
            check("selftest.fmt_rhythm_shader", problem);
        }
        if (analysed && formatAvailable(ffmpeg, "h264", "mp4")) {  // marqueurs : chapitres du fichier (une section chacun) et fichiers de reperes, relus par ffmpeg
            std::wstring problem;
            const std::wstring sh = put(L"p20_mark.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(p / iResolution.xy, 0.5, 1.0); }\n");
            Settings st = base("h264", "mp4", 8);
            st.duration = 24;
            st.audio = wav;
            st.markers = "all";
            st.name = L"p20_marked";
            std::string logText;
            if (!renderOne(sh, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                const std::wstring video = outputFile(sh, st, false);
                const std::string info = probeInfo(ffmpeg, video);
                std::vector<double> starts;
                for (size_t p = info.find("Chapter #"); p != std::string::npos; p = info.find("Chapter #", p + 1)) {
                    const size_t q = info.find("start ", p);
                    if (q != std::string::npos) starts.push_back(atof(info.c_str() + q + 6));
                }
                std::string csv, txt;
                const std::wstring stem = video.substr(0, video.find_last_of(L'.'));
                readFile(stem + L".markers.csv", csv);
                readFile(stem + L".markers.txt", txt);
                auto count = [&](const std::string& s, const std::string& what) {
                    int n = 0;
                    for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1)) n++;
                    return n;
                };
                if (starts.size() != 3 || std::fabs(starts[0]) > 0.01 || std::fabs(starts[1] - 8.13) > 0.45 || std::fabs(starts[2] - 16.13) > 0.45) problem = bad("chapters " + std::to_string(starts.size()));
                else if (count(csv, "\nbeat,") < 40 || count(csv, "\nbar,") < 10 || count(csv, "\nsection,") != 3 || txt.empty()) problem = bad("marker files");
                else if (info.find("Audio:") == std::string::npos) problem = bad("no audio in the video");
            }
            check("selftest.fmt_rhythm_markers", problem);
        }
        {  // montage cale sur la musique : calcul des durees, puis un montage de trois clips dont les coupes tombent sur les mesures
            std::wstring problem;
            std::vector<double> grid;
            for (double t = 0.13 + 2.0; t < 30; t += 2.0) grid.push_back(t);
            const std::vector<double> a = snapMontageDurations({3.3, 3.3, 3.3}, {0, 0, 0}, grid);
            const std::vector<double> b = snapMontageDurations({3, 3}, {0, 0.5}, grid);
            const std::vector<double> none = snapMontageDurations({3, 3}, {0, 0}, {});
            if (a.size() != 3 || std::fabs(a[0] - 4.13) > 1e-6 || std::fabs(a[1] - 4.0) > 1e-6 || std::fabs(a[2] - 4.0) > 1e-6) problem = bad("snap cuts");
            else if (b.size() != 2 || std::fabs((b[0] - 0.25) - 2.13) > 1e-6 || std::fabs(b[0] + b[1] - 0.5 - 4.13) > 1e-6) problem = bad("snap crossfade");
            else if (none.size() != 2 || std::fabs(none[0] - 3) > 1e-9 || std::fabs(none[1] - 3) > 1e-9) problem = bad("snap without grid");
            else if (analysed && formatAvailable(ffmpeg, "h264", "mp4")) {
                put(L"p20_a.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(1.0, 0.0, 0.0, 1.0); }\n");
                put(L"p20_b.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.0, 1.0, 0.0, 1.0); }\n");
                put(L"p20_c.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.0, 0.0, 1.0, 1.0); }\n");
                const std::wstring montage = put(L"p20_sync.montage", "set width=64\nset height=64\nset fps=10\nset codec=h264\nset container=mp4\nset crf=18\nset preset=ultrafast\nset audio=" + wavUtf8 +
                                                                        "\nset beatsync=bar\nduration=3.3\np20_a.glsl\np20_b.glsl 3.3 cut\np20_c.glsl 3.3 cut\n");
                Settings st = base("h264", "mp4", 8);
                st.name = L"p20_sync";
                std::string logText;
                if (!renderOne(montage, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const double d = probeDuration(probeInfo(ffmpeg, outputFile(montage, st, false)));
                    if (d < 11.95 || d > 12.35) problem = bad("montage duration " + std::to_string(d) + " (12.13 wanted)");
                }
            }
            check("selftest.fmt_rhythm_beatsync", problem);
        }
    }
    // Calcul reparti : parties rendues en parallele par plusieurs contextes OpenGL, panne et partie lente, ferme sur un dossier partage
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
        auto allFrames = [&](const std::wstring& video) {  // toutes les images decodees (le meme resultat = la meme video)
            const std::wstring raw = outDir + sep + L"p21.raw";
            platform::removeFile(raw);
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -i \"" + video + L"\" -f rawvideo -pix_fmt rgb24 \"" + raw + L"\"", nullptr);
            std::string bytes;
            readFile(raw, bytes);
            platform::removeFile(raw);
            return bytes;
        };
        const std::wstring heavy = put(L"p21_heavy.glsl",
                                       "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; float a = 0.0; for (int i = 0; i < 300; i++) a += sin(u.x * float(i) + iTime) * 0.004;"
                                       " o = vec4(u, clamp(0.5 + a + 0.2 * sin(float(iFrame) * 0.3), 0.0, 1.0), 1.0); }\n");
        auto settingsFor = [&](const wchar_t* name) {
            Settings st = base("h264", "mp4", 8);
            st.duration = 4;  // 40 images, quatre parties d'une seconde
            st.chunk = 1;
            st.audio = L"";
            st.name = name;
            return st;
        };
        const bool h264 = formatAvailable(ffmpeg, "h264", "mp4");
        std::string serialFrames;
        std::wstring e;
        if (h264) {  // reference : les memes parties, rendues l'une apres l'autre
            Settings st = settingsFor(L"p21_serial");
            Hooks H;
            H.log = [](const std::wstring&) {};
            if (convertFile(heavy, exeDir, st, H, e) == Result::Ok) serialFrames = allFrames(outputFile(heavy, st, false));
        }
        if (h264) {  // trois contextes : la video est celle du rendu en serie
            std::wstring problem;
            Settings st = settingsFor(L"p21_parallel");
            st.parallel = 3;
            std::string logText;
            Hooks H;
            H.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
            if (serialFrames.empty()) problem = bad("no reference");
            else if (convertFile(heavy, exeDir, st, H, e) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
            else if (allFrames(outputFile(heavy, st, false)) != serialFrames) problem = bad("parallel video differs from the serial one");
            else if (platform::multiContext() && logText.find(U8(TXF("log.parts_start", 3, 4LL, 10LL))) == std::string::npos) problem = bad("contexts not used");
            else if (!platform::multiContext() && logText.find(U8(TX("log.parallel_single_context"))) == std::string::npos) problem = bad("no message about the single context");
            else if (platform::dirExists(outputFile(heavy, st, false).substr(0, outputFile(heavy, st, false).find_last_of(L'.')) + L".parts")) problem = bad("parts folder left");
            check("selftest.fmt_parallel", problem);
        }
        if (h264 && platform::multiContext()) {  // un contexte tombe en panne : il est retire avec un message, le travail continue sur les autres
            std::wstring problem;
            Settings st = settingsFor(L"p21_fail");
            st.parallel = 3;
            std::string logText;
            std::atomic<int> hits{0};
            Hooks H;
            H.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
            H.injectFailure = [&](int, long long) { return hits++ == 0; };  // le premier contexte qui prend une partie tombe en panne (lequel, cela depend de la vitesse de demarrage de chacun : machine lente, instrumentee)
            if (convertFile(heavy, exeDir, st, H, e) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
            else if (allFrames(outputFile(heavy, st, false)) != serialFrames) problem = bad("video differs after a context failure");
            else if (logText.find(U8(TX("log.parts_injected"))) == std::string::npos || logText.find(U8(TXF("log.parts_retired", 1, TX("log.parts_injected").c_str()))) == std::string::npos && logText.find(U8(TXF("log.parts_retired", 2, TX("log.parts_injected").c_str()))) == std::string::npos && logText.find(U8(TXF("log.parts_retired", 3, TX("log.parts_injected").c_str()))) == std::string::npos) problem = bad("no message about the removed context");
            check("selftest.fmt_parallel_fail", problem);
        }
        if (h264 && platform::multiContext() && kTimingSensitiveOk) {  // une partie lente est rendue aussi par un contexte libre : la plus rapide gagne, la video est la meme
            std::wstring problem;
            Settings st = settingsFor(L"p21_slow");
            st.parallel = 2;
            std::string logText;
            std::atomic<int> hits{0};
            Hooks H;
            H.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
            H.speculateAfter = 0.3;
            H.injectDelay = [&](int, long long chunk) { return chunk == 1 && hits++ == 0 ? 6000 : 0; };  // le premier rendu de la partie 2 dure six secondes de plus
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = convertFile(heavy, exeDir, st, H, e) == Result::Ok;
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (!ok) problem = e.empty() ? TX("selftest.render_failed") : e;
            else if (allFrames(outputFile(heavy, st, false)) != serialFrames) problem = bad("video differs after redistributing a part");
            else if (logText.find(U8(TXF("log.parts_redistributed", 2LL))) == std::string::npos) problem = bad("part not redistributed");
            else if (secs > 5.0) problem = bad("the slow part was waited for: " + std::to_string(secs) + " s");
            check("selftest.fmt_parallel_slow", problem);
        }
        {  // ferme : fichiers de verrou, verrou perime repris, parties finies jamais refaites, coherence, etat (sans GPU : une partie est un petit fichier)
            std::wstring problem;
            const std::wstring farm = outDir + sep + L"p21_farm_unit";
            removeTree(farm);
            PartsPlan plan;
            plan.nChunks = 4;
            plan.chunkFrames = 10;
            plan.totalFrames = 40;
            plan.farmDir = farm;
            plan.partsDir = farm + L"/parts";
            plan.ext = L".mkv";
            plan.workers = 1;
            plan.lockTimeout = 1.0;
            plan.jobInfo = {{"signature", "abc"}, {"version", "1.0.0"}, {"ffmpeg", "ffmpeg version X"}, {"encoder", "libx264"}, {"format", "mkv"}};
            platform::makeDir(farm);
            platform::makeDir(farm + L"/parts");
            platform::makeDir(farm + L"/locks");
            put(L"p21_farm_unit/parts/00000.mkv", "deja la");                      // partie terminee avant : jamais refaite
            platform::createExclusive(farm + L"/locks/00001.lock", "machine=eteinte-1\n");  // un ordinateur qui s'est eteint avec son verrou
            std::vector<long long> rendered;
            std::mutex rm;
            const RenderPartFn fake = [&](int, long long chunk, long long, long long, const std::wstring& tmp, const Hooks&, std::wstring&) -> Result {
                platform::sleepMs(30);
                writeFileAtomic(tmp, "partie " + std::to_string(chunk));
                std::lock_guard<std::mutex> l(rm);
                rendered.push_back(chunk);
                return Result::Ok;
            };
            Hooks H;
            std::string logText;
            H.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
            PartsStats ps;
            const auto t0 = std::chrono::steady_clock::now();
            std::wstring pe;
            const Result r = runParts(plan, H, fake, pe, ps);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::sort(rendered.begin(), rendered.end());
            if (r != Result::Ok) problem = bad("farm run: " + U8(pe));
            else if (rendered != std::vector<long long>{1, 2, 3}) problem = bad("rendered parts " + std::to_string(rendered.size()));
            else if (secs < 0.9) problem = bad("the stale lock was taken over too early");
            else if (ps.resumed != 1 || !ps.assembler || logText.find(U8(TXF("log.farm_takeover", 1LL, 1)).substr(0, 12)) == std::string::npos) problem = bad("recovery");
            else {
                std::vector<std::wstring> lines;
                std::vector<platform::DirEntry> locks;
                platform::listDir(farm + L"/locks", locks);
                if (!locks.empty()) problem = bad("locks left");
                else if (!farmStatus(farm, plan.lockTimeout, lines) || lines.size() < 3 || lines[2] != TXF("farm.status_counts", 4, 0, 0)) problem = bad("status");
                else {
                    farmAssembled(plan, ps, L"video.mp4", [](const std::wstring&) {});
                    PartsStats again;
                    PartsPlan other = plan;
                    other.jobInfo = {{"signature", "zzz"}, {"version", "1.0.0"}, {"ffmpeg", "ffmpeg version X"}, {"encoder", "libx264"}, {"format", "mkv"}};
                    std::wstring e1, e2, e3, e4;
                    PartsPlan v = plan, f = plan, c = plan;
                    v.jobInfo[1].second = "2.0.0";
                    f.jobInfo[2].second = "ffmpeg version Y";
                    c.jobInfo[3].second = "h264_nvenc";
                    PartsStats s1, s2, s3, s4;
                    const Result r1 = runParts(other, H, fake, e1, s1), r2 = runParts(v, H, fake, e2, s2), r3 = runParts(f, H, fake, e3, s3), r4 = runParts(c, H, fake, e4, s4);
                    // le travail est deja assemble : la coherence se controle avant tout, un autre travail ne passe pas
                    if (r1 != Result::Failed || e1.find(L"zzz") == std::wstring::npos) problem = bad("another job accepted: " + U8(e1));
                    else if (r2 != Result::Failed || e2.find(L"2.0.0") == std::wstring::npos) problem = bad("another version accepted");
                    else if (r3 != Result::Failed || e3.find(L"ffmpeg version Y") == std::wstring::npos) problem = bad("another ffmpeg accepted");
                    else if (r4 != Result::Failed || e4.find(L"h264_nvenc") == std::wstring::npos) problem = bad("another encoder accepted");
                    else if (runParts(plan, H, fake, pe, again) != Result::Ok || !again.alreadyAssembled) problem = bad("an assembled job is started again");
                    else {
                        std::string rep;
                        if (!readFile(farm + L"/farm-report.json", rep) || rep.find("\"parts\": 4") == std::string::npos) problem = bad("farm report");
                    }
                }
            }
            check("selftest.fmt_farm_unit", problem);
        }
        if (h264) {  // ferme : deux processus du programme rendent la meme video sur un meme dossier, le resultat est celui d'un rendu en serie
            std::wstring problem;
            const std::wstring farm = outDir + sep + L"p21_farm";
            removeTree(farm);
            const std::wstring exe = exeDir + platform::exeFileName(L"ShaderToMP4");
            put(L"p21_farm.glsl", "void mainImage(out vec4 o, in vec2 p){ vec2 u = p / iResolution.xy; float a = 0.0; for (int i = 0; i < 900; i++) a += sin(u.x * float(i) + iTime) * 0.002;"
                                  " o = vec4(u, clamp(0.5 + a + 0.2 * sin(float(iFrame) * 0.3), 0.0, 1.0), 1.0); }\n");
            const std::wstring shader = outDir + sep + L"p21_farm.glsl";
            auto args = [&](const wchar_t* name, bool useFarm) {
                std::vector<std::wstring> a = {exe, L"--render", shader, L"--width=96", L"--height=96", L"--fps=10", L"--duration=12", L"--chunk=1.5", L"--codec=h264", L"--container=mp4",
                                               L"--crf=18", L"--preset=ultrafast", L"--audio=", L"--title=", L"--credit=", L"--exists=overwrite", L"--markers=off", L"--embed=0",
                                               L"--outdir=" + outDir, std::wstring(L"--name=") + name};
                if (useFarm) {
                    a.push_back(L"--farm=" + farm);
                    a.push_back(L"--farmtimeout=20");
                }
                return a;
            };
            auto serialProc = platform::Process::start(args(L"p21_farm_serial", false), false, true);
            std::string sink;
            char buf[4096];
            size_t got;
            int rcSerial = -1;
            if (serialProc) {
                while ((got = serialProc->readOutput(buf, sizeof(buf))) > 0) sink.append(buf, got);
                rcSerial = serialProc->wait();
            }
            auto one = platform::Process::start(args(L"p21_farm_out", true), false, true);
            auto two = platform::Process::start(args(L"p21_farm_out", true), false, true);
            int rc1 = -1, rc2 = -1;
            std::string out1, out2;
            std::thread t2([&] {
                if (!two) return;
                char b[4096];
                size_t g;
                while ((g = two->readOutput(b, sizeof(b))) > 0) out2.append(b, g);
                rc2 = two->wait();
            });
            if (one) {
                while ((got = one->readOutput(buf, sizeof(buf))) > 0) out1.append(buf, got);
                rc1 = one->wait();
            }
            t2.join();
            Settings key = base("h264", "mp4", 8);
            key.outdir = outDir;
            key.name = L"p21_farm_out";
            Settings keySerial = key;
            keySerial.name = L"p21_farm_serial";
            std::string rep;
            readFile(farm + L"/farm-report.json", rep);
            int machines = 0;
            for (size_t q = rep.find("\"machine\""); q != std::string::npos; q = rep.find("\"machine\"", q + 1)) machines++;
            if (!serialProc || !one || !two || rcSerial != 0 || rc1 != 0 || rc2 != 0) problem = bad("child processes " + std::to_string(rcSerial) + "," + std::to_string(rc1) + "," + std::to_string(rc2));
            else if (!fileExists(outputFile(shader, key, false)) || allFrames(outputFile(shader, key, false)) != allFrames(outputFile(shader, keySerial, false))) problem = bad("farm video differs from the serial one");
            else if (machines < 1 || rep.find("\"parts\": 8") == std::string::npos) problem = bad("farm report " + std::to_string(machines));
            else if (!fileExists(farm + L"/assembled.txt")) problem = bad("no assembled.txt");
            check("selftest.fmt_farm", problem);
            report += L"        " + TXF("selftest.fmt_farm_machines", machines) + L"\r\n";
        }
    }
    // Mode effet : un shader applique a des videos, images, sequences et dossiers ; transitions ; incrustation par cle
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
        auto gen = [&](const std::wstring& args) { return runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y " + args, nullptr) == 0; };
        auto frames = [&](const std::wstring& video, int w, int h) {  // toutes les images en RGB 8 bits
            const std::wstring raw = outDir + sep + L"p23.raw";
            platform::removeFile(raw);
            gen(L"-i \"" + video + L"\" -vf scale=" + std::to_wstring(w) + L":" + std::to_wstring(h) + L" -f rawvideo -pix_fmt rgb24 \"" + raw + L"\"");
            std::string bytes;
            readFile(raw, bytes);
            platform::removeFile(raw);
            return bytes;
        };
        auto meanDiff = [](const std::string& a, size_t offA, const std::string& b, size_t offB, size_t n) {
            if (offA + n > a.size() || offB + n > b.size() || n == 0) return 255.0;
            double sum = 0;
            for (size_t i = 0; i < n; i++) sum += std::abs((int)(uint8_t)a[offA + i] - (int)(uint8_t)b[offB + i]);
            return sum / (double)n;
        };
        auto highVolume = [&](const std::wstring& video, const wchar_t* from, const wchar_t* dur) {  // niveau moyen (dB) au-dessus de 600 Hz dans un passage
            std::string o;
            runProcess(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -ss " + from + L" -t " + dur + L" -i \"" + video + L"\" -vn -af highpass=f=600,highpass=f=600,volumedetect -f null -",
                       [&](const std::wstring& l) { o += U8(l) + "\n"; });
            const size_t m = o.find("mean_volume: ");
            return m == std::string::npos ? -200.0 : atof(o.c_str() + m + 13);
        };
        const bool h264 = formatAvailable(ffmpeg, "h264", "mp4");
        const std::wstring neutral = put(L"p23_neutral.glsl", "void mainImage(out vec4 o, in vec2 p){ o = texture(iChannel0, p / iResolution.xy); }\n");
        const std::wstring tags = L" -colorspace bt709 -color_primaries bt709 -color_trc bt709";
        const std::wstring srcVideo = outDir + sep + L"p23_src.mp4";
        // source : barres animees 192x108 a 30 images / s, 2 s, avec un son de 300 Hz puis de 900 Hz
        const bool haveSrc = h264 && gen(L"-f lavfi -i testsrc2=s=192x108:r=30:d=2 -f lavfi -i \"aevalsrc='if(lt(t,1),0.5*sin(2*PI*300*t),0.5*sin(2*PI*900*t))':s=44100:d=2\" -c:v libx264 -pix_fmt yuv420p -crf 8 -preset ultrafast" + tags + L" -c:a aac -shortest \"" + srcVideo + L"\"");
        auto base23 = [&](const wchar_t* name) {
            Settings st = base("h264", "mp4", 8);
            st.duration = 0;
            st.crf = 10;
            st.name = name;
            st.source = srcVideo;
            return st;
        };
        auto render23 = [&](const std::wstring& shader, const Settings& st, std::wstring& e, std::string& logText) {
            Hooks H;
            H.log = [&](const std::wstring& l) { logText += U8(l) + "\n"; };
            return convertFile(shader, exeDir, st, H, e) == Result::Ok;
        };
        std::string srcRaw;
        if (haveSrc) srcRaw = frames(srcVideo, 192, 108);
        const size_t px = 192 * 108 * 3;
        if (haveSrc) {  // identite : taille, cadence, duree et son viennent de la source, l'image est la meme a l'arrondi pres
            std::wstring problem, e;
            std::string logText;
            Settings st = base23(L"p23_id");
            SourceInfo info;
            if (srcRaw.size() != px * 60) problem = bad("source frames " + std::to_string(srcRaw.size() / px));
            else if (!render23(neutral, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                const std::wstring out = outputFile(neutral, st, false);
                const std::string got = frames(out, 192, 108);
                if (!probeSource(ffmpeg, out, 0, info) || info.width != 192 || info.height != 108 || std::fabs(info.fps - 30.0) > 0.01 || std::fabs(info.duration - 2.0) > 0.12) problem = bad("size / rate / duration of the output");
                else if (!info.hasAudio) problem = bad("the sound of the source is missing");
                else if (got.size() != srcRaw.size()) problem = bad("frames " + std::to_string(got.size() / px));
                else if (meanDiff(got, 0, srcRaw, 0, got.size()) > 3.0) problem = bad("mean difference " + std::to_string(meanDiff(got, 0, srcRaw, 0, got.size())));
                else if (highVolume(out, L"1.2", L"0.7") < highVolume(out, L"0.1", L"0.7") + 15) problem = bad("the sound is not the one of the source");
            }
            check("selftest.fmt_source", problem);
        }
        if (haveSrc) {  // a l'envers : images et son
            std::wstring problem, e;
            std::string logText;
            Settings st = base23(L"p23_rev");
            st.reverse = true;
            if (!render23(neutral, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                const std::wstring out = outputFile(neutral, st, false);
                const std::string got = frames(out, 192, 108);
                if (got.size() != srcRaw.size()) problem = bad("frames " + std::to_string(got.size() / px));
                else {
                    double worst = 0;
                    for (size_t i = 0; i < 60; i++) worst = std::max(worst, meanDiff(got, i * px, srcRaw, (59 - i) * px, px));
                    if (worst > 4.0) problem = bad("a frame differs from the reversed source by " + std::to_string(worst));
                    else if (highVolume(out, L"0.1", L"0.7") < highVolume(out, L"1.2", L"0.7") + 15) problem = bad("the sound is not reversed");
                }
            }
            check("selftest.fmt_source_reverse", problem);
        }
        if (haveSrc) {  // cadence changee sans interpolation (une image sur deux), plage de la source, iFrame = image de la source
            std::wstring problem, e;
            std::string logText;
            Settings st = base23(L"p23_fps");
            st.fps = 15;
            st.pinned.insert("fps");
            Settings rg = base23(L"p23_range");
            rg.start = 0.5;
            rg.duration = 0.5;
            rg.pinned.insert("duration");
            if (!render23(neutral, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                const std::string got = frames(outputFile(neutral, st, false), 192, 108);
                if (got.size() != px * 30) problem = bad("fps frames " + std::to_string(got.size() / px));
                else
                    for (size_t i = 0; i < 30 && problem.empty(); i++)
                        if (meanDiff(got, i * px, srcRaw, 2 * i * px, px) > 4.0) problem = bad("frame " + std::to_string(i) + " is not the source frame " + std::to_string(2 * i));
            }
            if (problem.empty()) {
                if (!render23(neutral, rg, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::string got = frames(outputFile(neutral, rg, false), 192, 108);
                    if (got.size() != px * 15) problem = bad("range frames " + std::to_string(got.size() / px));
                    else
                        for (size_t i = 0; i < 15 && problem.empty(); i++)
                            if (meanDiff(got, i * px, srcRaw, (15 + i) * px, px) > 4.0) problem = bad("range frame " + std::to_string(i));
                }
            }
            if (problem.empty()) {  // iFrame : le numero de l'image de la source (30 a l'instant 1 s, quelle que soit la cadence de sortie)
                const std::wstring fr = put(L"p23_frame.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(float(iFrame) / 255.0, 0.0, 0.0, 1.0); }\n");
                Settings ps = base23(L"p23_iframe");
                ps.width = 16;
                ps.height = 16;
                ps.fps = 15;
                ps.pinned = {"width", "height", "fps"};
                Hooks H;
                H.log = [](const std::wstring&) {};
                H.previewTime = 1.0;
                std::vector<uint8_t> got;
                H.previewFrame = [&](int, int, std::vector<uint8_t>&& px2) { got = std::move(px2); };
                if (convertFile(fr, exeDir, ps, H, e) != Result::Ok || got.size() != 16 * 16 * 4) problem = e.empty() ? TX("selftest.render_failed") : e;
                else if (std::abs((int)got[2] - 30) > 1) problem = bad("iFrame " + std::to_string(got[2]) + " instead of 30");
            }
            check("selftest.fmt_source_time", problem);
        }
        if (h264) {  // sous-titres et chapitres de la source gardes selon le conteneur ; sequence d'images et image fixe
            std::wstring problem, e;
            std::string logText;
            const std::wstring subs = outDir + sep + L"p23_subs.srt", chap = outDir + sep + L"p23_chap.txt", rich = outDir + sep + L"p23_rich.mp4";
            put(L"p23_subs.srt", "1\n00:00:00,200 --> 00:00:01,000\nBonjour\n");
            put(L"p23_chap.txt", ";FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=1000\ntitle=Un\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=1000\nEND=2000\ntitle=Deux\n");
            if (!haveSrc || !gen(L"-i \"" + srcVideo + L"\" -i \"" + subs + L"\" -i \"" + chap + L"\" -map 0 -map 1 -map_chapters 2 -c copy -c:s mov_text \"" + rich + L"\"")) problem = bad("source with subtitles");
            else
                for (const char* cont : {"mp4", "mkv"}) {
                    Settings st = base23(cont[1] == 'p' ? L"p23_rich_mp4" : L"p23_rich_mkv");
                    st.source = rich;
                    st.container = cont;
                    if (!render23(neutral, st, e, logText)) {
                        problem = e.empty() ? TX("selftest.render_failed") : e;
                        break;
                    }
                    SourceInfo info;
                    if (!probeSource(ffmpeg, outputFile(neutral, st, false), 0, info) || info.chapters != 2 || info.subCodecs.empty() || !info.hasAudio) {
                        problem = bad(std::string(cont) + ": chapters " + std::to_string(info.chapters) + ", subtitle streams " + std::to_string(info.subCodecs.size()));
                        break;
                    }
                }
            if (problem.empty()) {  // sequence de dix images a 10 par seconde, et image fixe tenue
                const std::wstring seqDir = outDir + sep + L"p23_seq";
                removeTree(seqDir);
                platform::makeDir(seqDir);
                const std::wstring still = outDir + sep + L"p23_still.png";
                if (!gen(L"-f lavfi -i testsrc2=s=96x54:r=10:d=1 \"" + seqDir + sep + L"f_%03d.png\"") || !gen(L"-f lavfi -i color=c=0x3080c0:s=64x36 -frames:v 1 \"" + still + L"\"")) problem = bad("generation");
                else {
                    Settings st = base23(L"p23_seqout");
                    st.source = seqDir + sep + L"f_%03d.png";
                    st.sourceFps = 10;
                    if (!render23(neutral, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else {
                        SourceInfo info;
                        if (!probeSource(ffmpeg, outputFile(neutral, st, false), 0, info) || info.width != 96 || info.height != 54 || std::fabs(info.fps - 10.0) > 0.01 || std::fabs(info.duration - 1.0) > 0.1) problem = bad("sequence output");
                    }
                    Settings fixed = base23(L"p23_stillout");
                    fixed.source = still;
                    fixed.duration = 0.5;
                    fixed.fps = 10;
                    fixed.pinned.insert("fps");
                    if (problem.empty()) {
                        if (!render23(neutral, fixed, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                        else {
                            const std::string got = frames(outputFile(neutral, fixed, false), 64, 36);
                            if (got.size() != 64 * 36 * 3 * 5 || std::abs((int)(uint8_t)got[0] - 0x30) > 6 || std::abs((int)(uint8_t)got[1] - 0x80) > 6 || std::abs((int)(uint8_t)got[2] - 0xc0) > 6) problem = bad("still image output");
                        }
                    }
                }
            }
            check("selftest.fmt_source_files", problem);
        }
        if (haveSrc) {  // dossier : un rendu par fichier, compte rendu, reprise (les fichiers faits sont sautes)
            std::wstring problem, e;
            std::string logText;
            const std::wstring dir = outDir + sep + L"p23_batch_in", out = outDir + sep + L"p23_batch_out";
            removeTree(dir);
            removeTree(out);
            platform::makeDir(dir);
            platform::makeDir(out);
            {
                std::string bytes;
                readFile(srcVideo, bytes);
                writeFileAtomic(dir + sep + L"un.mp4", bytes);
            }
            gen(L"-f lavfi -i color=c=red:s=64x64:r=10:d=1 -c:v libx264 -pix_fmt yuv420p \"" + dir + sep + L"deux.mp4\"");
            gen(L"-f lavfi -i color=c=green:s=48x48 -frames:v 1 \"" + dir + sep + L"trois.png\"");
            Settings st = base23(L"p23_batch");
            st.source = dir;
            st.outdir = out;
            st.duration = 0;
            st.name = L"";
            if (!render23(neutral, st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                int outputs = 0;
                for (const wchar_t* n : {L"p23_neutral_un.mp4", L"p23_neutral_deux.mp4", L"p23_neutral_trois.mp4"})
                    if (fileExists(out + sep + n)) outputs++;
                std::string json;
                readFile(out + sep + L"p23_neutral_batch.json", json);
                if (outputs != 3) problem = bad("outputs " + std::to_string(outputs));
                else if (json.find("\"ok\": 3") == std::string::npos) problem = bad("batch report");
                else {
                    std::string again;
                    std::wstring e2;
                    if (!render23(neutral, st, e2, again)) problem = e2.empty() ? TX("selftest.render_failed") : e2;
                    else {
                        readFile(out + sep + L"p23_neutral_batch.json", json);
                        if (json.find("\"skipped\": 3") == std::string::npos) problem = bad("the second run did not skip the finished files");
                    }
                }
            }
            check("selftest.fmt_source_batch", problem);
        }
        if (haveSrc) {  // transitions : au debut on voit A, a la fin B, pour chacune des transitions livrees ; duree et son d'une transition
            std::wstring problem, e;
            std::string logText;
            std::wstring tdir = exeDir + L"transitions";
            if (!platform::dirExists(tdir)) tdir = exeDir + L"../transitions";
            const std::wstring red = outDir + sep + L"p23_red.mp4", blue = outDir + sep + L"p23_blue.mp4";
            const bool made = gen(L"-f lavfi -i color=c=0xc02020:s=64x64:r=30:d=2 -f lavfi -i \"sine=frequency=300:duration=2\" -c:v libx264 -pix_fmt yuv420p" + tags + L" -c:a aac -shortest \"" + red + L"\"") &&
                              gen(L"-f lavfi -i color=c=0x2020c0:s=64x64:r=30:d=2 -f lavfi -i \"sine=frequency=900:duration=2\" -c:v libx264 -pix_fmt yuv420p" + tags + L" -c:a aac -shortest \"" + blue + L"\"");
            std::vector<platform::DirEntry> list;
            std::vector<std::wstring> names;
            if (platform::listDir(tdir, list))
                for (const auto& en : list)
                    if (!en.isDir && en.name.size() > 5 && en.name.compare(en.name.size() - 5, 5, L".glsl") == 0) names.push_back(en.name);
            std::sort(names.begin(), names.end());
            if (!made) problem = bad("clips");
            else if (names.size() < 20) problem = bad("transitions found: " + std::to_string(names.size()));
            else {
                for (const std::wstring& n : names) {
                    Settings st = base23(L"p23_trans");
                    st.source = red;
                    st.source2 = blue;
                    st.width = st.height = 16;
                    st.pinned = {"width", "height"};
                    for (double when : {0.4, 2.8}) {  // la transition commence a 1 s et dure 1 s : 0,4 s = A seul, 2,8 s = B seul
                        Hooks H;
                        H.log = [](const std::wstring&) {};
                        H.previewTime = when;
                        std::vector<uint8_t> got;
                        H.previewFrame = [&](int, int, std::vector<uint8_t>&& px2) { got = std::move(px2); };
                        if (convertFile(tdir + sep + n, exeDir, st, H, e) != Result::Ok || got.size() != 16 * 16 * 4) {
                            problem = bad(U8(n) + ": " + (e.empty() ? std::string("no image") : U8(e).substr(0, 120)));
                            break;
                        }
                        const size_t c = (8 * 16 + 8) * 4;  // pixel du centre (BGRA)
                        const bool wantRed = when < 1.0;
                        const int r = got[c + 2], b = got[c];
                        if (wantRed ? !(r > 150 && b < 90) : !(b > 150 && r < 90)) {
                            problem = bad(U8(n) + " at " + std::to_string(when) + " s: r=" + std::to_string(r) + " b=" + std::to_string(b));
                            break;
                        }
                    }
                    if (!problem.empty()) break;
                }
                if (problem.empty()) {  // duree et son : A (300 Hz) puis B (900 Hz), 2 + 2 - 1 = 3 s
                    Settings st = base23(L"p23_fadeout");
                    st.source = red;
                    st.source2 = blue;
                    if (!render23(tdir + sep + L"fade.glsl", st, e, logText)) problem = e.empty() ? TX("selftest.render_failed") : e;
                    else {
                        const std::wstring out = outputFile(tdir + sep + L"fade.glsl", st, false);
                        SourceInfo info;
                        if (!probeSource(ffmpeg, out, 0, info) || std::fabs(info.duration - 3.0) > 0.12 || !info.hasAudio) problem = bad("transition output duration " + std::to_string(info.duration));
                        else if (highVolume(out, L"2.3", L"0.6") < highVolume(out, L"0.1", L"0.6") + 15) problem = bad("the sound does not go from A to B");
                    }
                }
            }
            check("selftest.fmt_source_transition", problem);
        }
        if (h264) {  // incrustation par cle : fond vert transparent, sujet rouge opaque
            std::wstring problem, e;
            std::string logText;
            std::wstring edir = exeDir + L"examples/effects";
            if (!platform::dirExists(edir)) edir = exeDir + L"../examples/effects";
            const std::wstring key = outDir + sep + L"p23_key.png";
            if (!gen(L"-f lavfi -i \"color=c=0x00ff00:s=64x64,drawbox=x=16:y=16:w=32:h=32:color=red:t=fill\" -frames:v 1 \"" + key + L"\"")) problem = bad("generation");
            else {
                Settings st = base23(L"p23_keyout");
                st.source = key;
                st.alpha = true;
                st.pinned.insert("alpha");
                Hooks H;
                H.snapshot = true;
                H.log = [](const std::wstring&) {};
                const std::wstring shader = edir + sep + L"chromakey.glsl";
                if (convertFile(shader, exeDir, st, H, e) != Result::Ok) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    const std::wstring png = outputPath(shader, st.outdir, L".png"), raw = outDir + sep + L"p23_key.raw";
                    gen(L"-i \"" + png + L"\" -f rawvideo -pix_fmt rgba \"" + raw + L"\"");
                    std::string px2;
                    readFile(raw, px2);
                    auto at = [&](int x, int y, int c) { return px2.size() >= (size_t)64 * 64 * 4 ? (int)(uint8_t)px2[((size_t)y * 64 + x) * 4 + c] : -1; };
                    if (at(4, 4, 3) > 25) problem = bad("the green background is not transparent (alpha " + std::to_string(at(4, 4, 3)) + ")");
                    else if (at(32, 32, 3) < 230 || at(32, 32, 0) < 150) problem = bad("the subject is not opaque and red (alpha " + std::to_string(at(32, 32, 3)) + ")");
                }
            }
            check("selftest.fmt_source_key", problem);
        }
    }
    // Sorties multicouches : OpenEXR ecrit par le programme, passes de sortie, alpha droit / premultiplie, TIFF flottant, DPX, carte de profondeur
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
        auto gen = [&](const std::wstring& args) { return runProcess(L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y " + args, nullptr) == 0; };
        auto render24 = [&](const std::wstring& shader, const Settings& st, std::wstring& e) {
            Hooks H;
            H.log = [](const std::wstring&) {};
            return convertFile(shader, exeDir, st, H, e) == Result::Ok;
        };
        auto frameFile = [&](const std::wstring& shader, const Settings& st, int n) {
            const std::wstring first = outputFile(shader, st, false);
            return wfmt(sequencePattern(first).c_str(), n);
        };
        auto closeTo = [](double a, double b, double tol) { return std::fabs(a - b) <= tol; };
        auto setup = [&](const wchar_t* name) {
            Settings st = base("h264", "exr", 8);
            st.width = 32;
            st.height = 24;
            st.fps = 4;
            st.duration = 1;
            st.name = name;
            return st;
        };

        // codec : demi-flottant (les 65536 valeurs, arrondis) et compression zlib (aller-retour, gain sur des donnees repetitives)
        {
            std::wstring problem;
            for (uint32_t h = 0; h < 65536 && problem.empty(); h++) {
                if (((h >> 10) & 31) == 31 && (h & 0x3ff)) continue;  // NaN : la charge utile n'est pas conservee
                if (exr::floatToHalf(exr::halfToFloat((uint16_t)h)) != h) problem = bad("half round trip " + std::to_string(h));
            }
            if (problem.empty() && (exr::floatToHalf(1.0f) != 0x3c00 || exr::floatToHalf(65519.0f) != 0x7bff || exr::floatToHalf(65520.0f) != 0x7c00 || exr::floatToHalf(1.0f + 1.0f / 2048.0f) != 0x3c00 ||
                                    exr::floatToHalf(1.0f + 3.0f / 2048.0f) != 0x3c02 || exr::floatToHalf(-0.0f) != 0x8000))
                problem = bad("half rounding");
            if (problem.empty()) {
                std::string raw, mixed;
                for (int i = 0; i < 70000; i++) raw += (char)((i / 37) & 255);   // repetitif
                uint32_t seed = 12345;
                for (int i = 0; i < 70000; i++) {
                    seed = seed * 1664525u + 1013904223u;
                    mixed += (char)(seed >> 24);                                 // sans structure
                }
                std::string back;
                const std::string z1 = exr::zlibCompress(raw), z2 = exr::zlibCompress(mixed);
                if (!exr::zlibDecompress(z1, raw.size(), back) || back != raw) problem = bad("zlib round trip (repetitive)");
                else if (!exr::zlibDecompress(z2, mixed.size(), back) || back != mixed) problem = bad("zlib round trip (random)");
                else if (z1.size() * 5 > raw.size()) problem = bad("zlib does not compress repetitive data (" + std::to_string(z1.size()) + ")");
                else {  // un flux zlib d'une autre origine (blocs a codes dynamiques) est relu : produit par ffmpeg (PNG, IDAT)
                    const std::wstring png = outDir + sep + L"p24_dyn.png", rawf = outDir + sep + L"p24_dyn.raw";
                    if (gen(L"-f lavfi -i testsrc2=s=64x48:d=0.04:r=25 -frames:v 1 -update 1 \"" + png + L"\"")) {
                        std::string pb;
                        readFile(png, pb);
                        std::string idat;  // concatenation des blocs IDAT
                        for (size_t q = 8; q + 12 <= pb.size();) {
                            const uint32_t len = ((uint8_t)pb[q] << 24) | ((uint8_t)pb[q + 1] << 16) | ((uint8_t)pb[q + 2] << 8) | (uint8_t)pb[q + 3];
                            if (pb.compare(q + 4, 4, "IDAT") == 0) idat.append(pb, q + 8, len);
                            q += 12 + len;
                        }
                        std::string inflated;
                        // 64 x 48, RGB 8 bits : 48 lignes de 1 + 64 * 3 octets
                        if (idat.empty() || !exr::zlibDecompress(idat, 48 * (1 + 64 * 3), inflated)) problem = bad("zlib stream written by ffmpeg");
                    }
                    platform::removeFile(png);
                    platform::removeFile(rawf);
                }
            }
            check("selftest.fmt_exr_codec", problem);
        }

        // couches : couleur HDR, profondeur, normale, alpha ; trois compressions et deux precisions relues par le programme, couche principale relue par ffmpeg
        std::wstring layersShader, layersName;
        {
            std::wstring problem;
            layersShader = put(L"p24.glsl", "// aov depth=bufferB.r\n// aov normal=bufferB.gba\n// aov alpha\nvoid mainImage(out vec4 o, in vec2 p){ vec2 uv = p / iResolution.xy; o = vec4(uv.x * 2.0, uv.y, 1.5, 1.0); }\n");
            put(L"p24.bufferB.glsl", "void mainImage(out vec4 o, in vec2 p){ vec2 uv = p / iResolution.xy; o = vec4(uv.x * 10.0 + float(iFrame), 0.25, 0.5, uv.y); }\n");
            struct Variant {
                const wchar_t* name;
                const char* comp;
                const char* prec;
            };
            const Variant variants[] = {{L"p24_none", "none", "half"}, {L"p24_zips", "zips", "half"}, {L"p24_zip", "zip", "half"}, {L"p24_zipf", "zip", "float"}};
            uint64_t sizeNone = 0, sizeZip = 0;
            exr::Image ref;
            for (const Variant& v : variants) {
                if (!problem.empty()) break;
                Settings st = setup(v.name);
                st.exrCompression = v.comp;
                st.exrPrecision = v.prec;
                std::wstring e;
                if (!render24(layersShader, st, e)) {
                    problem = e.empty() ? TX("selftest.render_failed") : e;
                    break;
                }
                const bool full = std::string(v.prec) == "float";
                const double tolC = full ? 1e-5 : 2.5e-3;  // demi-flottant : 11 bits de mantisse, valeurs jusqu'a 2
                for (int n : {0, 3}) {
                    exr::Image img;
                    const std::wstring file = frameFile(layersShader, st, n);
                    if (!exr::read(file, img, e)) {
                        problem = e;
                        break;
                    }
                    if (img.width != 32 || img.height != 24) {
                        problem = bad("size " + std::to_string(img.width) + "x" + std::to_string(img.height));
                        break;
                    }
                    const char* names[] = {"R", "G", "B", "A", "depth.Z", "normal.X", "normal.Y", "normal.Z"};
                    for (const char* nm : names)
                        if (!img.find(nm)) problem = bad(std::string("missing channel ") + nm);
                    if (!problem.empty()) break;
                    for (int y = 0; y < 24 && problem.empty(); y++)
                        for (int x = 0; x < 32; x++) {
                            const size_t i = (size_t)y * 32 + x;
                            const double ux = (x + 0.5) / 32.0, uy = (24 - 1 - y + 0.5) / 24.0;  // la ligne 0 du fichier est le haut de l'image
                            if (!closeTo(img.find("R")->pixels[i], ux * 2.0, tolC) || !closeTo(img.find("G")->pixels[i], uy, tolC) || !closeTo(img.find("B")->pixels[i], 1.5, tolC) ||
                                !closeTo(img.find("A")->pixels[i], 1.0, 1e-6) || !closeTo(img.find("depth.Z")->pixels[i], ux * 10.0 + n, 1e-4) ||
                                !closeTo(img.find("normal.X")->pixels[i], 0.25, tolC) || !closeTo(img.find("normal.Y")->pixels[i], 0.5, tolC) || !closeTo(img.find("normal.Z")->pixels[i], uy, tolC)) {
                                problem = bad(std::string(v.comp) + "/" + v.prec + " frame " + std::to_string(n) + " pixel " + std::to_string(x) + "," + std::to_string(y));
                                break;
                            }
                        }
                    if (problem.empty() && n == 3 && std::string(v.comp) == "zips") ref = img;
                    if (problem.empty() && n == 3 && std::string(v.comp) == "zip" && std::string(v.prec) == "half") {  // zip et zips relus identiques
                        const exr::Image& a = ref;
                        for (size_t k = 0; k < a.channels.size() && problem.empty(); k++)
                            if (a.channels[k].pixels != img.channels[k].pixels) problem = bad("zip and zips differ on " + a.channels[k].name);
                    }
                    if (!problem.empty()) break;
                    uint64_t sz = 0;
                    platform::fileSize(file, sz);
                    if (n == 0 && std::string(v.comp) == "none") sizeNone = sz;
                    if (n == 0 && std::string(v.comp) == "zip" && std::string(v.prec) == "half") sizeZip = sz;
                    if (n == 0 && std::string(v.prec) == "half" && img.find("depth.Z")) {  // la profondeur reste en 32 bits meme en demi-flottant
                        if (!img.find("depth.Z")->full || img.find("R")->full) problem = bad("channel precisions");
                    }
                }
            }
            if (problem.empty() && !(sizeZip > 0 && sizeNone > 0 && sizeZip < sizeNone)) problem = bad("zip is not smaller than none (" + std::to_string(sizeZip) + " / " + std::to_string(sizeNone) + ")");
            if (problem.empty()) {  // la couche principale est relue par ffmpeg : planes G, B, R, A en flottant 32 bits
                Settings st = setup(L"p24_zip");
                st.exrCompression = "zip";
                const std::wstring file = frameFile(layersShader, st, 1), raw = outDir + sep + L"p24_ff.raw";
                platform::removeFile(raw);
                gen(L"-i \"" + file + L"\" -frames:v 1 -f rawvideo -pix_fmt gbrapf32le \"" + raw + L"\"");
                std::string px;
                readFile(raw, px);
                platform::removeFile(raw);
                const size_t plane = 32 * 24 * 4;
                if (px.size() != plane * 4) problem = bad("ffmpeg re-read size " + std::to_string(px.size()));
                else {
                    float r, b;
                    memcpy(&r, px.data() + 2 * plane + ((size_t)12 * 32 + 16) * 4, 4);
                    memcpy(&b, px.data() + 1 * plane + ((size_t)12 * 32 + 16) * 4, 4);
                    float g;
                    memcpy(&g, px.data() + 0 * plane + ((size_t)12 * 32 + 16) * 4, 4);
                    // ffmpeg borne les valeurs a 1 a la lecture d'un EXR : on compare la couleur dans [0, 1] et la valeur bornee
                    if (!closeTo(g, (24 - 1 - 12 + 0.5) / 24.0, 3e-3) || !closeTo(r, std::min(1.0, 2.0 * 16.5 / 32.0), 3e-3) || !closeTo(b, 1.0, 3e-3)) problem = bad("ffmpeg re-read values " + std::to_string(r) + " " + std::to_string(g) + " " + std::to_string(b));
                }
            }
            check("selftest.fmt_exr_layers", problem);
        }

        // carte de profondeur 16 bits : bornes donnees ou lues dans le fichier
        {
            std::wstring problem;
            Settings st = setup(L"p24_zip");
            const std::wstring file = frameFile(layersShader, st, 2), png = outDir + sep + L"p24_depth.png", raw = outDir + sep + L"p24_depth.raw";
            std::wstring e;
            if (exrDepthMap(file, png, 0, 0, false, e)) problem = bad("a depth map without bounds must be refused");
            else if (!exrDepthMap(file, png, 0.0, 20.0, false, e)) problem = e;
            else {
                gen(L"-i \"" + png + L"\" -f rawvideo -pix_fmt gray16le \"" + raw + L"\"");
                std::string px;
                readFile(raw, px);
                if (px.size() != 32 * 24 * 2) problem = bad("depth map size " + std::to_string(px.size()));
                else {
                    uint16_t v;
                    memcpy(&v, px.data() + ((size_t)5 * 32 + 16) * 2, 2);
                    const double want = (10.0 * 16.5 / 32.0 + 2.0) / 20.0 * 65535.0;
                    if (!closeTo(v, want, 3.0)) problem = bad("depth value " + std::to_string(v) + " != " + std::to_string(want));
                }
            }
            if (problem.empty()) {  // profondeur normalisee a l'ecriture (depthrange) : les bornes sont dans le fichier, la conversion n'en demande pas d'autres
                Settings sn = setup(L"p24_norm");
                sn.depthRange = "0:20";
                if (!render24(layersShader, sn, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
                else {
                    exr::Image img;
                    const std::wstring nf = frameFile(layersShader, sn, 2);
                    bool declared = false;
                    if (exr::read(nf, img, e))
                        for (const auto& n : img.numbers)
                            if (n.first == "depthFar" && closeTo(n.second, 20.0, 1e-6)) declared = true;
                    if (!declared) problem = bad("depthNear / depthFar not declared");
                    else if (!closeTo(img.find("depth.Z")->pixels[5 * 32 + 16], (10.0 * 16.5 / 32.0 + 2.0) / 20.0, 1e-4)) problem = bad("normalised depth value");
                    else if (!exrDepthMap(nf, png, 0, 0, true, e)) problem = e;
                    else {
                        gen(L"-i \"" + png + L"\" -f rawvideo -pix_fmt gray16le \"" + raw + L"\"");
                        std::string px;
                        readFile(raw, px);
                        uint16_t v = 0;
                        if (px.size() >= ((size_t)5 * 32 + 16) * 2 + 2) memcpy(&v, px.data() + ((size_t)5 * 32 + 16) * 2, 2);
                        const double want = (1.0 - (10.0 * 16.5 / 32.0 + 2.0) / 20.0) * 65535.0;  // le plus proche en blanc
                        if (!closeTo(v, want, 3.0)) problem = bad("inverted depth value " + std::to_string(v));
                    }
                }
            }
            platform::removeFile(png);
            platform::removeFile(raw);
            check("selftest.fmt_depthmap", problem);
        }

        // alpha droit et premultiplie : EXR, TIFF flottant et PNG 16 bits
        {
            std::wstring problem, e;
            const std::wstring shader = put(L"p24_alpha.glsl", "// alpha=1\nvoid mainImage(out vec4 o, in vec2 p){ vec2 uv = p / iResolution.xy; o = vec4(1.0, 0.5, 0.25, uv.x); }\n");
            for (int mode = 0; mode < 2 && problem.empty(); mode++) {
                Settings st = setup(mode == 0 ? L"p24_straight" : L"p24_premult");
                st.alphaMode = mode == 0 ? "straight" : "premult";
                st.exrPrecision = "float";
                if (!render24(shader, st, e)) {
                    problem = e.empty() ? TX("selftest.render_failed") : e;
                    break;
                }
                exr::Image img;
                if (!exr::read(frameFile(shader, st, 0), img, e)) {
                    problem = e;
                    break;
                }
                std::string declared;
                for (const auto& t : img.text)
                    if (t.first == "alphaMode") declared = t.second;
                for (int x = 0; x < 32 && problem.empty(); x += 7) {
                    const double a = (x + 0.5) / 32.0, r = img.find("R")->pixels[(size_t)10 * 32 + x], g = img.find("G")->pixels[(size_t)10 * 32 + x];
                    if (!closeTo(img.find("A")->pixels[(size_t)10 * 32 + x], a, 1e-5) || !closeTo(r, mode == 0 ? 1.0 : a, 1e-4) || !closeTo(g, mode == 0 ? 0.5 : 0.5 * a, 1e-4))
                        problem = bad(std::string(mode == 0 ? "straight" : "premult") + " exr pixel " + std::to_string(x));
                }
                if (problem.empty() && declared != (mode == 0 ? "straight" : "premultiplied")) problem = bad("alphaMode attribute '" + declared + "'");
                if (!problem.empty()) break;
                Settings sp = base("h264", "png", 10);  // PNG 16 bits : la couleur est multipliee par l'alpha
                sp.width = 32;
                sp.height = 24;
                sp.fps = 2;
                sp.duration = 0.5;
                sp.name = mode == 0 ? L"p24_pstraight" : L"p24_ppremult";
                sp.alphaMode = st.alphaMode;
                if (!render24(shader, sp, e)) {
                    problem = e.empty() ? TX("selftest.render_failed") : e;
                    break;
                }
                const std::wstring raw = outDir + sep + L"p24_png.raw";
                platform::removeFile(raw);
                gen(L"-i \"" + frameFile(shader, sp, 0) + L"\" -f rawvideo -pix_fmt rgba64le \"" + raw + L"\"");
                std::string px;
                readFile(raw, px);
                platform::removeFile(raw);
                if (px.size() != (size_t)32 * 24 * 8) problem = bad("png re-read size");
                else {
                    uint16_t c[4];
                    memcpy(c, px.data() + ((size_t)10 * 32 + 24) * 8, 8);
                    const double a = c[3] / 65535.0;
                    if (!closeTo(c[0] / 65535.0, mode == 0 ? 1.0 : a, 0.004) || !closeTo(a, 24.5 / 32.0, 0.004)) problem = bad(std::string(mode == 0 ? "straight" : "premult") + " png pixel " + std::to_string(c[0]) + "," + std::to_string(c[3]));
                }
            }
            check("selftest.fmt_exr_alpha", problem);
        }

        // vecteurs de mouvement estimes : une texture qui avance de 4 pixels par image
        {
            std::wstring problem, e;
            const std::wstring shader = put(L"p24_motion.glsl", "// aov motion\nvoid mainImage(out vec4 o, in vec2 p){ vec2 c = floor((p - vec2(4.0 * float(iFrame), 0.0)) / 2.0); float v = fract(sin(dot(c, vec2(12.9898, 78.233))) * 43758.5453); o = vec4(vec3(v), 1.0); }\n");
            Settings st = setup(L"p24_motion");
            st.width = 96;
            st.height = 32;
            if (!render24(shader, st, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                exr::Image f0, f2;
                if (!exr::read(frameFile(shader, st, 0), f0, e) || !exr::read(frameFile(shader, st, 2), f2, e)) problem = e;
                else {
                    bool zero0 = true;
                    for (float v : f0.find("motion.X")->pixels) if (v != 0.0f) zero0 = false;
                    const double mx = f2.find("motion.X")->pixels[(size_t)16 * 96 + 28], my = f2.find("motion.Y")->pixels[(size_t)16 * 96 + 28];  // un point au milieu de l'image 2
                    if (!zero0) problem = bad("the first frame must have no motion");
                    else if (!closeTo(mx, 4.0, 1.0) || !closeTo(my, 0.0, 1.0)) problem = bad("estimated motion " + std::to_string(mx) + "," + std::to_string(my) + " instead of 4,0");
                }
            }
            check("selftest.fmt_exr_motion", problem);
        }

        // flou de mouvement vectoriel : un trait qui avance de 8 pixels par image, flou le long de son deplacement, lumiere conservee
        {
            std::wstring problem, e;
            const std::wstring shader = put(L"p24_vb.glsl", "// aov motion=bufferB.rg\nvoid mainImage(out vec4 o, in vec2 p){ float cx = 20.0 + 8.0 * float(iFrame); float v = abs(p.x - cx) < 2.0 ? 1.0 : 0.0; o = vec4(vec3(v), 1.0); }\n");
            put(L"p24_vb.bufferB.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(8.0, 0.0, 0.0, 1.0); }\n");
            double sums[2] = {0, 0}, behind[2] = {0, 0};
            for (int blurred = 0; blurred < 2 && problem.empty(); blurred++) {
                Settings st = setup(blurred ? L"p24_vb1" : L"p24_vb0");
                st.width = 64;
                st.height = 8;
                st.exrPrecision = "float";
                st.vectorBlur = blurred ? 1.0 : 0.0;
                exr::Image img;
                if (!render24(shader, st, e) || !exr::read(frameFile(shader, st, 1), img, e)) {
                    problem = e.empty() ? TX("selftest.render_failed") : e;
                    break;
                }
                for (int x = 0; x < 64; x++) sums[blurred] += img.find("R")->pixels[(size_t)3 * 64 + x];
                behind[blurred] = img.find("R")->pixels[(size_t)3 * 64 + 22];  // derriere le trait (qui est en 26..30 a l'image 1)
            }
            if (problem.empty() && !(behind[0] < 1e-6 && behind[1] > 0.1)) problem = bad("no trail behind the stripe (" + std::to_string(behind[0]) + " / " + std::to_string(behind[1]) + ")");
            else if (problem.empty() && !closeTo(sums[1], sums[0], sums[0] * 0.1)) problem = bad("the blur does not keep the light (" + std::to_string(sums[0]) + " -> " + std::to_string(sums[1]) + ")");
            check("selftest.fmt_exr_vectorblur", problem);
        }

        // sur-echantillonnage : moyenne exacte de 2 x 2 pixels
        {
            std::wstring problem, e;
            const std::wstring shader = put(L"p24_ss.glsl", "void mainImage(out vec4 o, in vec2 p){ float c = mod(floor(p.x) + floor(p.y), 2.0); o = vec4(c, c, c, 1.0); }\n");
            Settings st = setup(L"p24_ss");
            st.ss = 2;
            if (!render24(shader, st, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                exr::Image img;
                if (!exr::read(frameFile(shader, st, 0), img, e)) problem = e;
                else if (img.width != 32 || img.height != 24) problem = bad("size " + std::to_string(img.width) + "x" + std::to_string(img.height));
                else if (!closeTo(img.find("R")->pixels[7 * 32 + 9], 0.5, 1e-3)) problem = bad("supersampled value");
            }
            check("selftest.fmt_exr_ss", problem);
        }

        // TIFF en virgule flottante 32 bits : valeurs au-dela de 1, alpha declare
        {
            std::wstring problem, e;
            const std::wstring shader = put(L"p24_tiff.glsl", "void mainImage(out vec4 o, in vec2 p){ vec2 uv = p / iResolution.xy; o = vec4(uv.x * 3.0, 0.5, 2.0, uv.y); }\n");
            for (int mode = 0; mode < 2 && problem.empty(); mode++) {
                Settings st = setup(mode == 0 ? L"p24_tf3" : L"p24_tf4");
                st.container = "tiff";
                st.tiffFloat = true;
                st.alpha = mode == 1;
                st.alphaMode = "premult";
                if (!render24(shader, st, e)) {
                    problem = e.empty() ? TX("selftest.render_failed") : e;
                    break;
                }
                tifffloat::Image ti;
                const std::wstring tf = wfmt(sequencePattern(outputFile(shader, st, false)).c_str(), 0);
                if (!tifffloat::read(tf, ti, e)) problem = e;
                else if (ti.width != 32 || ti.height != 24 || ti.samples != (mode == 0 ? 3 : 4)) problem = bad("tiff layout");
                else {
                    const size_t i = ((size_t)6 * 32 + 20) * (size_t)ti.samples;
                    const double ux = 20.5 / 32.0, uy = (24 - 1 - 6 + 0.5) / 24.0;
                    const double a = mode == 1 ? uy : 1.0;  // alpha = uv.y ; premultiplie : la couleur est multipliee
                    const double kk = mode == 1 ? a : 1.0;
                    if (!closeTo(ti.pixels[i], ux * 3.0 * kk, 1e-4) || !closeTo(ti.pixels[i + 1], 0.5 * kk, 1e-4) || !closeTo(ti.pixels[i + 2], 2.0 * kk, 1e-4) || (mode == 1 && (!closeTo(ti.pixels[i + 3], uy, 1e-4) || !ti.premultiplied)))
                        problem = bad("tiff float values (mode " + std::to_string(mode) + ")");
                    else if (mode == 0 && ti.pixels[i] <= 1.0f && ux * 3.0 > 1.0) problem = bad("tiff values were clamped");
                }
            }
            check("selftest.fmt_tiff_float", problem);
        }

        // DPX 10 bits : valeurs, espace de couleur et cadence dans l'en-tete
        {
            std::wstring problem, e;
            const std::wstring shader = put(L"p24_dpx.glsl", "void mainImage(out vec4 o, in vec2 p){ o = vec4(0.8, 0.4, 0.2, 1.0); }\n");
            Settings st = base("h264", "dpx", 10);
            st.width = 32;
            st.height = 24;
            st.fps = 24;
            st.duration = 0.25;
            st.name = L"p24_dpx";
            if (!render24(shader, st, e)) problem = e.empty() ? TX("selftest.render_failed") : e;
            else {
                const std::wstring file = frameFile(shader, st, 3), raw = outDir + sep + L"p24_dpx.raw";
                platform::removeFile(raw);
                gen(L"-i \"" + file + L"\" -frames:v 1 -f rawvideo -pix_fmt gbrp10le \"" + raw + L"\"");
                std::string px, hdr;
                readFile(raw, px);
                readFile(file, hdr);
                platform::removeFile(raw);
                const size_t plane = 32 * 24 * 2;
                if (px.size() != plane * 3 || hdr.size() < 2048) problem = bad("dpx re-read size " + std::to_string(px.size()));
                else {
                    uint16_t g, b, r;
                    memcpy(&g, px.data() + 10 * 2, 2);
                    memcpy(&b, px.data() + plane + 10 * 2, 2);
                    memcpy(&r, px.data() + 2 * plane + 10 * 2, 2);
                    if (!closeTo(r, 0.8 * 1023, 2.0) || !closeTo(g, 0.4 * 1023, 2.0) || !closeTo(b, 0.2 * 1023, 2.0)) problem = bad("dpx values " + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b));
                    else if ((uint8_t)hdr[803] != 10) problem = bad("dpx bit depth " + std::to_string((int)(uint8_t)hdr[803]));
                    else if ((uint8_t)hdr[801] != 6 || (uint8_t)hdr[802] != 6) problem = bad("dpx colour space " + std::to_string((int)(uint8_t)hdr[801]));
                    else if (!closeTo(dpxFrameRate(file), 24.0, 1e-6)) problem = bad("dpx frame rate " + std::to_string(dpxFrameRate(file)));
                }
            }
            check("selftest.fmt_dpx", problem);
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
    // Courbes d'Hermite des parametres : pentes imposees (`t:v@pente`) et pentes lissees, exactes sur une droite et sur le lissage de base
    {
        total++;
        std::wstring problem, err;
        auto load = [&](const std::string& decl, const std::string& spec, ParamSet& ps) {
            Settings st;
            st.paramSpecs.push_back(spec);
            return ps.load("// uniform " + decl + "\nvoid mainImage(out vec4 o, in vec2 p){ o = vec4(1.0); }\n", st, L"", err);
        };
        auto at = [&](ParamSet& ps, double t) {
            double v[4];
            ps.valueAt("v", t, v);
            return v[0];
        };
        ParamSet flat, line, autoSlope, vec, bad, same;
        if (!load("float v = 1 [0..10]", "v=hermite 0:0@0 ; 2:4@0", flat) || !load("float v = 1 [0..10]", "v=hermite 0:0@2 ; 2:4@2", line) ||
            !load("float v = 1 [0..10]", "v=hermite 0:0 ; 1:1 ; 2:2", autoSlope) || !load("vec2 v = 0,0 [0..10]", "v=hermite 0:0,0@0,2 ; 2:4,4@0,2", vec) ||
            !load("float v = 1 [0..10]", "v=smooth 0:0 ; 2:4", same))
            problem = err.empty() ? TX("selftest.render_failed") : err;
        else if (std::fabs(at(flat, 1.0) - 2.0) > 1e-9 || std::fabs(at(flat, 0.5) - at(same, 0.5)) > 1e-9 || std::fabs(at(flat, 1.7) - at(same, 1.7)) > 1e-9)
            problem = TXF("selftest.fmt_bad", W("hermite with zero slopes must equal smooth").c_str());
        else if (std::fabs(at(line, 0.5) - 1.0) > 1e-9 || std::fabs(at(line, 1.5) - 3.0) > 1e-9)
            problem = TXF("selftest.fmt_bad", W("hermite with the slope of a line must give the line").c_str());
        else if (std::fabs(at(autoSlope, 0.3) - 0.3) > 1e-9 || std::fabs(at(autoSlope, 1.6) - 1.6) > 1e-9)
            problem = TXF("selftest.fmt_bad", W("smoothed slopes of collinear keys must give the line").c_str());
        else {
            double v[4];
            vec.valueAt("v", 1.0, v);
            if (std::fabs(v[0] - 2.0) > 1e-9 || std::fabs(v[1] - 2.0) > 1e-9) problem = TXF("selftest.fmt_bad", W("hermite on a vector").c_str());
            else if (load("float v = 1", "v=hermite 0:0@1,2 ; 2:4", bad) || load("float v = 1", "v=hermite 0:0@x ; 2:4", bad) || load("vec2 v = 0,0", "v=hermite 0:0,0@1 ; 2:1,1", bad))
                problem = TXF("selftest.fmt_bad", W("invalid slopes accepted").c_str());
            else if (flat.signature() == line.signature()) problem = TXF("selftest.fmt_bad", W("the slopes must be part of the signature").c_str());
        }
        report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX("selftest.fmt_param_hermite") + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
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
    // Licence Pro : signature, fichiers de licence, filigrane, interface
    {
        const std::wstring sep(1, platform::pathSeparator());
        auto check = [&](const char* key, const std::wstring& problem) {
            total++;
            report += (problem.empty() ? L"PASS    " : L"FAIL    ") + TX(key) + (problem.empty() ? L"" : L": " + problem) + L"\r\n";
            if (!problem.empty()) fails++;
        };
        auto bad = [&](const std::string& what) { return TXF("selftest.fmt_bad", W(what).c_str()); };
        auto toHex = [](const uint8_t* p, size_t n) {
            std::string s;
            char b[3];
            for (size_t i = 0; i < n; i++) {
                snprintf(b, sizeof(b), "%02x", p[i]);
                s += b;
            }
            return s;
        };
        auto fromHex = [](const std::string& s) {
            std::vector<uint8_t> v;
            for (size_t i = 0; i + 1 < s.size(); i += 2) v.push_back((uint8_t)strtoul(s.substr(i, 2).c_str(), nullptr, 16));
            return v;
        };

        // Signature : SHA-512 (FIPS 180) et Ed25519 (RFC 8032) contre les vecteurs publies
        {
            std::wstring problem;
            uint8_t h[64];
            ed25519::sha512((const uint8_t*)"abc", 3, h);
            if (toHex(h, 64) != "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f")
                problem = bad("SHA-512 abc");
            const std::string thousand(1000, 'a');  // franchit la limite d'un bloc (128 octets) plusieurs fois
            ed25519::sha512((const uint8_t*)thousand.data(), thousand.size(), h);
            if (problem.empty() && toHex(h, 8) != "67ba5535a46e3f86") problem = bad("SHA-512 of 1000 bytes");
            struct V {
                const char *seed, *pub, *msg, *sig;
            };
            const V vectors[] = {
                {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
                 "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
                {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
                 "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"}};
            for (const V& v : vectors) {
                if (!problem.empty()) break;
                const std::vector<uint8_t> seed = fromHex(v.seed), msg = fromHex(v.msg);
                uint8_t pub[32], sec[64], sig[64];
                ed25519::keypairFromSeed(seed.data(), pub, sec);
                ed25519::sign(msg.data(), msg.size(), sec, sig);
                if (toHex(pub, 32) != v.pub) problem = bad("Ed25519 public key");
                else if (toHex(sig, 64) != v.sig) problem = bad("Ed25519 signature");
                else if (!ed25519::verify(msg.data(), msg.size(), sig, pub)) problem = bad("Ed25519 verification");
                else {
                    std::vector<uint8_t> other = msg;
                    other.push_back(1);
                    uint8_t bent[64];
                    memcpy(bent, sig, 64);
                    bent[40] ^= 1;
                    if (ed25519::verify(other.data(), other.size(), sig, pub) || ed25519::verify(msg.data(), msg.size(), bent, pub)) problem = bad("Ed25519 accepts an altered message or signature");
                }
            }
            check("selftest.lic_crypto", problem);
        }

        // Fichiers de licence : cle d'essai propre a l'auto-test (la cle secrete du programme n'est dans aucun depot)
        uint8_t testPub[32], testSec[64], otherPub[32], otherSec[64];
        {
            uint8_t seed[64];
            ed25519::sha512((const uint8_t*)"ShaderToMP4 selftest key", 24, seed);
            ed25519::keypairFromSeed(seed, testPub, testSec);
            ed25519::sha512((const uint8_t*)"ShaderToMP4 other key", 21, seed);
            ed25519::keypairFromSeed(seed, otherPub, otherSec);
        }
        auto makeLicense = [&](const std::string& holder, const std::string& edition, int major, const std::string& expires, const uint8_t* sec) {
            std::string t = "ShaderToMP4-License=1\nholder=" + holder + "\nedition=" + edition + "\nmajor=" + std::to_string(major) + "\nexpires=" + expires + "\nid=TEST-0001\n";
            uint8_t sig[64];
            ed25519::sign((const uint8_t*)t.data(), t.size(), sec, sig);
            return t + "signature=" + toHex(sig, 64) + "\n";
        };
        const long long today = daysFromCivil(2026, 10, 22);
        {
            std::wstring problem;
            auto state = [&](const std::string& text) { return checkLicenseText(text, testPub, APP_VER_MAJOR, today).state; };
            const std::string good = makeLicense("Tester", "pro", APP_VER_MAJOR, "never", testSec);
            const ProLicense li = checkLicenseText(good, testPub, APP_VER_MAJOR, today);
            std::string crlf;
            for (char c : good) crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
            std::string altered = good, majorEdit = good, truncated = good.substr(0, good.size() - 24);
            altered.replace(altered.find("Tester"), 6, "Hacker");
            const std::string curMajor = "major=" + std::to_string(APP_VER_MAJOR);  // la version du programme, pas un numero ecrit en dur
            majorEdit.replace(majorEdit.find(curMajor), curMajor.size(), "major=" + std::to_string(APP_VER_MAJOR + 1));
            const struct {
                const char* what;
                std::string text;
                ProLicenseState want;
            } cases[] = {
                {"valid", good, ProLicenseState::Valid},
                {"valid with CRLF line ends", crlf, ProLicenseState::Valid},
                {"valid on its last day", makeLicense("Tester", "pro", APP_VER_MAJOR, "2026-10-22", testSec), ProLicenseState::Valid},
                {"expired the day before", makeLicense("Tester", "pro", APP_VER_MAJOR, "2026-10-21", testSec), ProLicenseState::Expired},
                {"expired long ago", makeLicense("Tester", "pro", APP_VER_MAJOR, "2020-01-01", testSec), ProLicenseState::Expired},
                {"holder altered", altered, ProLicenseState::Invalid},
                {"major edited without re-signing", majorEdit, ProLicenseState::Invalid},
                {"truncated", truncated, ProLicenseState::Invalid},
                {"signature removed", good.substr(0, good.find("signature=")), ProLicenseState::Invalid},
                {"empty", std::string(), ProLicenseState::Invalid},
                {"signed by another key", makeLicense("Tester", "pro", APP_VER_MAJOR, "never", otherSec), ProLicenseState::Invalid},
                {"other edition", makeLicense("Tester", "free", APP_VER_MAJOR, "never", testSec), ProLicenseState::Invalid},
                {"unreadable expiry date", makeLicense("Tester", "pro", APP_VER_MAJOR, "someday", testSec), ProLicenseState::Invalid},
                {"other major version", makeLicense("Tester", "pro", APP_VER_MAJOR + 1, "never", testSec), ProLicenseState::OtherMajor},
                {"older major version", makeLicense("Tester", "pro", APP_VER_MAJOR - 1, "never", testSec), ProLicenseState::OtherMajor},
            };
            for (const auto& c : cases)
                if (problem.empty() && state(c.text) != c.want) problem = bad(std::string("license case: ") + c.what);
            if (problem.empty() && (li.holder != "Tester" || li.edition != "pro" || li.major != APP_VER_MAJOR || li.id != "TEST-0001")) problem = bad("license fields");
            // deposees dans un dossier, comme a cote du programme : cle d'essai valide, cle du programme refuse, dossier vide = aucune licence
            const std::wstring licDir = outDir + sep + L"lic";
            platform::makeDir(licDir);
            const std::wstring licDirSep = licDir + sep;  // comme exeDir : le dossier se termine par un separateur
            const std::wstring file = licDirSep + licenseFileName();
            if (problem.empty()) {
                if (loadLicense(licDirSep, testPub).state != ProLicenseState::None) problem = bad("an empty folder must give no license");
                if (FILE* f = platform::openFile(file, "wb")) {
                    fwrite(good.data(), 1, good.size(), f);
                    fclose(f);
                }
                if (loadLicense(licDirSep, testPub).state != ProLicenseState::Valid) problem = bad("license file in a folder");
                else if (loadLicense(licDirSep).state != ProLicenseState::Invalid) problem = bad("a license not signed by the program key must be refused");
            }
            check("selftest.lic_files", problem);
        }

        // Filigrane : couleur seulement, jamais les passes de donnees ; present seulement avec une fonction Pro et sans licence valide
        {
            std::wstring problem;
            const std::wstring shader = outDir + sep + L"lic_wm.glsl";
            if (FILE* f = platform::openFile(shader, "wb")) {
                const char* body = "void mainImage(out vec4 o, in vec2 p){ o = vec4(vec3(0.3), 1.0); }\n";
                fwrite(body, 1, strlen(body), f);
                fclose(f);
            }
            auto preview = [&](bool proFeature, const ProLicense& li, std::vector<uint8_t>& px) {
                setLicenseForTest(li);
                Settings st;
                st.width = st.height = 64;
                Hooks H;
                H.log = [](const std::wstring&) {};
                H.previewTime = 1.0;
                H.previewFrame = [&](int, int, std::vector<uint8_t>&& got) { px = std::move(got); };
                if (proFeature) H.proTestFeatures.push_back("selftest");
                std::wstring e;
                return convertFile(shader, exeDir, st, H, e) == Result::Ok && px.size() == 64 * 64 * 4;
            };
            ProLicense none, valid, expired, invalid;
            valid.state = ProLicenseState::Valid;
            expired.state = ProLicenseState::Expired;
            invalid.state = ProLicenseState::Invalid;
            std::vector<uint8_t> plain, marked, licensed, lapsed, forged, neutral;
            if (!preview(false, none, plain) || !preview(true, none, marked) || !preview(true, valid, licensed) || !preview(true, expired, lapsed) ||
                !preview(true, invalid, forged) || !preview(false, expired, neutral)) {
                problem = TX("selftest.render_failed");
            } else {
                size_t changed = 0, outside = 0;
                int minX = 64, minY = 64;
                for (int y = 0; y < 64; y++)
                    for (int x = 0; x < 64; x++) {
                        const size_t i = ((size_t)y * 64 + x) * 4;
                        if (memcmp(&plain[i], &marked[i], 3) == 0) continue;
                        changed++;
                        minX = std::min(minX, x);
                        minY = std::min(minY, y);
                        if (x < 32 || y < 32) outside++;  // le filigrane tient dans le quart bas droit
                    }
                const bool uniform = [&] {
                    for (size_t i = 4; i < plain.size(); i += 4)
                        if (memcmp(&plain[i], &plain[0], 3) != 0) return false;
                    return true;
                }();
                if (!uniform) problem = bad("the reference image must be uniform");
                else if (changed < 20) problem = bad("no watermark with a Pro feature and no license (" + std::to_string(changed) + " pixels)");
                else if (outside != 0) problem = bad("watermark outside the bottom right corner");
                else if (licensed != plain) problem = bad("watermark present with a valid license");
                else if (lapsed != marked) problem = bad("an expired license must give the same result as no license");
                else if (forged != marked) problem = bad("an invalid license must give the same result as no license");
                else if (neutral != plain) problem = bad("watermark on an output without a Pro feature");
            }
            // passes de donnees (profondeur, normale) inchangees ; la couleur seule porte le filigrane
            if (problem.empty()) {
                const std::wstring layers = outDir + sep + L"lic_layers.glsl";
                if (FILE* f = platform::openFile(layers, "wb")) {
                    const char* body = "// aov depth=bufferB.r\n// aov normal=bufferB.gba\nvoid mainImage(out vec4 o, in vec2 p){ vec2 uv = p / iResolution.xy; o = vec4(uv.x, uv.y, 0.5, 1.0); }\n";
                    fwrite(body, 1, strlen(body), f);
                    fclose(f);
                }
                if (FILE* f = platform::openFile(outDir + sep + L"lic_layers.bufferB.glsl", "wb")) {
                    const char* body = "void mainImage(out vec4 o, in vec2 p){ vec2 uv = p / iResolution.xy; o = vec4(uv.x * 10.0, 0.25, 0.5, uv.y); }\n";
                    fwrite(body, 1, strlen(body), f);
                    fclose(f);
                }
                auto exrFrame = [&](const wchar_t* name, bool proFeature, const ProLicense& li, exr::Image& img) {
                    setLicenseForTest(li);
                    Settings st = base("h264", "exr", 8);
                    st.width = 48;
                    st.height = 48;
                    st.fps = 2;
                    st.duration = 1;
                    st.name = name;
                    st.exrPrecision = "float";
                    st.exrCompression = "zip";
                    Hooks H;
                    H.log = [](const std::wstring&) {};
                    if (proFeature) H.proTestFeatures.push_back("selftest");
                    std::wstring e;
                    if (convertFile(layers, exeDir, st, H, e) != Result::Ok) return false;
                    return exr::read(wfmt(sequencePattern(outputFile(layers, st, false)).c_str(), 0), img, e);
                };
                exr::Image withMark, without;
                if (!exrFrame(L"lic_exr_wm", true, none, withMark) || !exrFrame(L"lic_exr_ok", true, valid, without)) {
                    problem = TX("selftest.render_failed");
                } else {
                    const char* data[] = {"depth.Z", "normal.X", "normal.Y", "normal.Z"};
                    for (const char* nm : data) {
                        const auto *a = withMark.find(nm), *b = without.find(nm);
                        if (!a || !b || a->pixels != b->pixels) problem = bad(std::string("data pass changed by the watermark: ") + nm);
                    }
                    size_t colorChanged = 0;
                    for (const char* nm : {"R", "G", "B"}) {
                        const auto *a = withMark.find(nm), *b = without.find(nm);
                        if (!a || !b || a->pixels.size() != b->pixels.size()) {
                            problem = bad("color channels");
                            break;
                        }
                        for (size_t i = 0; i < a->pixels.size(); i++)
                            if (a->pixels[i] != b->pixels[i]) colorChanged++;
                    }
                    if (problem.empty() && colorChanged < 20) problem = bad("the EXR color must carry the watermark");
                }
            }
            initLicense(exeDir);  // rend l'etat reel de ce poste
            check("selftest.lic_watermark", problem);
        }

        // Interface : bouton d'achat et mentions d'essai seulement sans licence valide ; liens d'achat
        {
            std::wstring problem;
            ProLicense none, valid, expired;
            valid.state = ProLicenseState::Valid;
            valid.holder = "Tester";
            valid.edition = "pro";
            valid.major = APP_VER_MAJOR;
            valid.expires = "never";
            expired.state = ProLicenseState::Expired;
            expired.expires = "2020-01-01";
            const std::wstring buyLink = APP_PURCHASE_URL_WSTR;
            setLicenseForTest(none);
            if (!showTrialUi() || isRegistered()) problem = bad("trial interface must be shown without a license");
            const std::wstring trialInfo = licenseInfoText(none, exeDir);
            if (problem.empty() && trialInfo.find(buyLink) == std::wstring::npos) problem = bad("purchase link missing from the trial text");
            if (problem.empty() && licenseInfoText(expired, exeDir).find(buyLink) == std::wstring::npos) problem = bad("purchase link missing with an expired license");
            setLicenseForTest(valid);
            if (problem.empty() && (showTrialUi() || !isRegistered())) problem = bad("trial interface must disappear with a valid license");
            const std::wstring regInfo = licenseInfoText(valid, exeDir);
            if (problem.empty() && (regInfo.find(buyLink) != std::wstring::npos || licenseStatusText(valid) == licenseStatusText(none) || regInfo.find(L"Tester") == std::wstring::npos))
                problem = bad("registered text");
            if (problem.empty()) {
                const auto& amounts = purchaseAmounts();
                if (amounts != std::vector<int>{5, 10, 15}) problem = bad("purchase amounts");
                for (int a : amounts)
                    if (problem.empty() && purchaseUrl(a) != buyLink + L"/" + std::to_wstring(a) + L"EUR") problem = bad("purchase link for " + std::to_string(a));
                if (problem.empty() && purchaseUrl(7) != buyLink + L"/5EUR") problem = bad("an unlisted amount must give the first amount");
                if (problem.empty() && purchaseUrl(15) != L"https://www.paypal.me/sandefjorddevel/15EUR") problem = bad("PayPal link");
            }
            initLicense(exeDir);
            check("selftest.lic_ui", problem);
        }
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
            // Images de reference par pilote : tests/ref/<pilote>/<nom>.ref d'abord (nvidia, amd, intel, llvmpipe...), sinon tests/<nom>.ref ; tolerance de chaque
            // cas dans tests/tolerances.txt (« nom ecart justification », 2 par defaut) ; au-dela de la moitie de la tolerance, une ligne DRIFT previent
            const std::string driver = [] {  // pilote d'apres GL_RENDERER : llvmpipe (Mesa, CI), warp (Mesa GLon12 sur le rasteriseur logiciel de Direct3D), nvidia, amd, intel...
                const char* r = (const char*)glGetString(0x1F01);
                std::string n = r ? r : "";
                for (char& c : n) c = (char)tolower((unsigned char)c);
                auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
                if (has("llvmpipe")) return std::string("llvmpipe");
                if (has("d3d12") && (has("basic render") || has("warp"))) return std::string("warp");  // nom-technique
                if (has("d3d12")) return std::string("d3d12");  // nom-technique
                if (has("nvidia")) return std::string("nvidia");  // nom-technique
                if (has("radeon") || has("amd")) return std::string("amd");  // nom-technique
                if (has("intel")) return std::string("intel");  // nom-technique
                if (has("gdi generic")) return std::string("gdi");  // nom-technique
                return std::string("other");  // nom-technique
            }();
            std::map<std::wstring, double> tolerance;
            {
                std::string text;
                if (readFile(dir + L"tolerances.txt", text)) {
                    size_t pos = 0;
                    while (pos < text.size()) {
                        size_t eol = text.find('\n', pos);
                        if (eol == std::string::npos) eol = text.size();
                        const std::string line = text.substr(pos, eol - pos);
                        pos = eol + 1;
                        char nm[128];
                        double v;
                        if (line.empty() || line[0] == '#') continue;
                        if (sscanf(line.c_str(), "%127s %lf", nm, &v) == 2 && v > 0) tolerance[W(nm)] = v;
                    }
                }
            }
            const std::wstring refDir = dir + L"ref/" + W(driver) + L"/";
            for (const auto& en : shaders) {
                total++;
                const std::wstring file = en.name;
                const std::wstring stem = file.substr(0, file.find_last_of(L'.'));
                const std::wstring baseRef = dir + stem + L".ref", driverRef = refDir + stem + L".ref";
                const std::wstring ref = update || fileExists(driverRef) ? driverRef : baseRef;
                const double tol = tolerance.count(stem) ? tolerance[stem] : 2.0;
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
                        } else {
                            platform::makeDir(dir + L"ref");
                            platform::makeDir(refDir.substr(0, refDir.size() - 1));
                            if (FILE* f = platform::openFile(ref, "wb")) {
                                fwrite(got.data(), 1, got.size(), f);
                                fclose(f);
                                line = L"UPDATED " + file + L": " + TX("selftest.ref_written");
                            }
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
                        const bool drift = mad <= tol && mad > 0.5 * tol && mad > 0.5;  // proche de la limite : alerte, pas encore un echec
                        line = (mad > tol ? L"FAIL    " : drift ? L"DRIFT   " : L"PASS    ") + file + L" (" +
                               (mad <= tol ? TXF("selftest.diff_ok", mad) : TXF("selftest.diff_bad", mad, tol)) + L")" + (drift ? L" " + TXF("selftest.drift", tol) : L"") + L" [" + W(driver) + (ref == driverRef ? L"]" : L" -> reference commune]");
                        if (mad > tol) fails++;
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
        {  // galerie d'exemples : soixante shaders au moins, dix categories, vignette et deux langues pour chacun, tous rendus en une image sans erreur
            total++;
            std::wstring problem;
            const std::vector<GalleryItem> items = listGallery(exeDir);
            std::map<std::wstring, int> perCategory;
            int rendered = 0;
            if (items.size() < 60) problem = L"gallery: " + std::to_wstring(items.size()) + L" shaders";
            for (const GalleryItem& it : items) {
                if (!problem.empty()) break;
                perCategory[it.category]++;
                if (it.thumb.empty()) problem = it.name + L": no thumbnail";
                else if (it.text[0].empty() || it.text[1].empty() || it.text[0] == it.text[1]) problem = it.name + L": the explanation is missing in a language";  // nom-technique
                else {
                    std::string src;
                    readFile(it.path, src);
                    if (src.find("SPDX-License-Identifier: GPL-3.0-or-later") == std::string::npos) problem = it.name + L": no licence header";  // nom-technique
                }
                if (!problem.empty()) break;
                Settings st = base;
                std::vector<uint8_t> got;
                Hooks H;
                H.log = [](const std::wstring&) {};
                H.previewTime = 1.0;
                H.previewFrame = [&](int, int, std::vector<uint8_t>&& px2) { got = std::move(px2); };
                std::wstring e;
                if (convertFile(it.path, exeDir, st, H, e) != Result::Ok || got.size() != 64 * 64 * 4) problem = it.category + L"/" + it.name + L": " + (e.empty() ? TX("selftest.render_failed") : e.substr(0, 200));
                else rendered++;
            }
            if (problem.empty())
                for (const std::wstring& c : galleryCategories())
                    if (perCategory[c] < 6) problem = L"category " + c + L": " + std::to_wstring(perCategory[c]) + L" shaders";
            if (problem.empty()) {
                report += L"PASS    " + TX("selftest.gallery") + L": " + std::to_wstring(rendered) + L" shaders\r\n";
            } else {
                fails++;
                report += L"FAIL    " + TX("selftest.gallery") + L": " + problem + L"\r\n";
            }
        }
        {  // explication des erreurs : chaque regle reconnait des lignes de NVIDIA, Mesa et AMD ; une vraie erreur de compilation est suivie de son explication
            total++;
            std::string problem;
            struct Case { const wchar_t* line; const char* id; };
            const Case cases[] = {
                {L"0(12) : error C1008: undefined variable \"foo\"", "undeclared"},  // nom-technique
                {L"0:12(5): error: 'foo' undeclared identifier", "undeclared"},  // nom-technique
                {L"ERROR: 0:12: 'foo' : undeclared identifier", "undeclared"},  // nom-technique
                {L"0:3(9): error: 'iChannel7' undeclared", "channel"},
                {L"ERROR: 0:5: 'texture2D' : no matching overloaded function found", "texture"},  // nom-technique
                {L"0(8) : error C7011: implicit cast from \"vec3\" to \"float\"", "type"},  // nom-technique
                {L"ERROR: 0:7: '=' : cannot convert from 'const float' to 'highp 3-component vector of float'", "type"},  // nom-technique
                {L"ERROR: 0:9: 'length' : no matching overloaded function found", "overload"},  // nom-technique
                {L"ERROR: 0:9: 'constructor' : not enough data provided for construction", "ctor"},  // nom-technique
                {L"ERROR: 0:4: '' : syntax error: unexpected RIGHT_BRACE", "syntax"},  // nom-technique
                {L"ERROR: 0:1: 'mainImage' : no matching overloaded function found", "main"},  // nom-technique
                {L"ERROR: 0:6: 'x' : redefinition", "redefined"},
                {L"ERROR: 0:6: 'a' : l-value required (can't modify a const)", "assign"},  // nom-technique
                {L"ERROR: 0:6: 'z' : invalid vector field selection", "swizzle"},  // nom-technique
                {L"ERROR: 0:6: '/' : divide by zero", "divzero"},  // nom-technique
            };
            for (const Case& c : cases) {
                ErrorHelp h;
                if (!explainGlslError(c.line, h) || h.id != c.id || h.help.empty() || h.help[0] == L'[' || h.fix.empty() || h.fix[0] == L'[') {
                    problem = std::string("rule ") + c.id + ": " + U8(c.line).substr(0, 80);
                    break;
                }
            }
            ErrorHelp none;
            if (problem.empty() && explainGlslError(L"ERROR: 0:6: 'x' : something unheard of", none)) problem = "an unknown message must stay without explanation";  // nom-technique
            if (problem.empty()) {  // de bout en bout : la compilation d'un vrai shader faux donne l'erreur localisee suivie de l'explication, le fichier reste intact
                const std::wstring bad = dir + L"explain_tmp.glsl";
                const std::string code = "void mainImage(out vec4 c, in vec2 p) { c = vec4(undeclaredThing, 0.0, 0.0, 1.0); }\n";
                if (FILE* f = platform::openFile(bad, "wb")) {
                    fwrite(code.data(), 1, code.size(), f);
                    fclose(f);
                }
                Hooks H;
                H.log = [](const std::wstring&) {};
                H.previewTime = 0.0;
                H.previewFrame = [](int, int, std::vector<uint8_t>&&) {};
                std::wstring e;
                const Result r = convertFile(bad, exeDir, base, H, e);
                std::string after;
                readFile(bad, after);
                if (r == Result::Ok) problem = "a wrong shader must not render";
                else if (e.find(L"undeclaredThing") == std::wstring::npos || e.find(L"→") == std::wstring::npos) problem = "no explanation after the compile error: " + U8(e).substr(0, 160);
                else if (after != code) problem = "the shader file was modified";
                platform::removeFile(bad);
            }
            if (problem.empty()) {
                report += L"PASS    " + TX("selftest.explain") + L"\r\n";
            } else {
                fails++;
                report += L"FAIL    " + TX("selftest.explain") + L": " + W(problem) + L"\r\n";
            }
        }
        {  // update : comparaison des versions, description de version, somme retrouvee, adresses non securisees refusees (l'essai avec un vrai serveur local est dans la documentation)
            total++;
            std::string problem;
            if (compareVersions("2.1.1", "2.2.0") >= 0 || compareVersions("v2.10.0", "2.9.9") <= 0 || compareVersions("2.2", "2.2.0") != 0 || compareVersions("3.0.0-rc1", "2.9.9") <= 0) problem = "version comparison";
            const std::string hex(64, 'a');
            const std::string release =
                "{\"tag_name\":\"v9.0.1\",\"html_url\":\"https://example.org/r\",\"assets\":[{\"name\":\"SHA256SUMS.txt\",\"browser_download_url\":\"https://example.org/s\"},"
                "{\"name\":\"ShaderToMP4-9.0.1-win64.zip\",\"browser_download_url\":\"https://example.org/z\"},"
                "{\"name\":\"ShaderToMP4-9.0.1-win64-setup.exe\",\"browser_download_url\":\"https://example.org/i\"}]}";
            UpdateInfo info;
            if (problem.empty() && (!parseRelease(release, "9.0.0", info) || !info.newer || info.latest != "9.0.1" || info.assetName != L"ShaderToMP4-9.0.1-win64-setup.exe" || info.sumsUrl != L"https://example.org/s"))
                problem = "release description";
            UpdateInfo same;
            if (problem.empty() && (!parseRelease(release, "9.0.1", same) || same.newer)) problem = "same version";
            UpdateInfo junk;
            if (problem.empty() && (parseRelease("<html>", "9.0.0", junk) || parseRelease("{\"x\":1}", "9.0.0", junk))) problem = "junk answer";
            if (problem.empty() && findChecksum(hex + "  ShaderToMP4-9.0.1-win64-setup.exe\n" + std::string(64, 'b') + " *other.zip\n", L"ShaderToMP4-9.0.1-win64-setup.exe") != hex) problem = "checksum line";
            if (problem.empty() && (!findChecksum(hex + "  a.zip\n", L"b.zip").empty() || !findChecksum("short  b.zip\n", L"b.zip").empty())) problem = "checksum missing";
            if (problem.empty() && (!updateUrlAllowed(L"https://api.github.com/x") || !updateUrlAllowed(L"http://127.0.0.1:8765/x") || !updateUrlAllowed(L"http://localhost/x") || updateUrlAllowed(L"http://example.com/x") ||
                                    updateUrlAllowed(L"http://127.0.0.1.evil.org/x") || updateUrlAllowed(L"ftp://x") || updateUrlAllowed(L"file:///c:/x")))
                problem = "address filter";
            if (problem.empty()) {  // une adresse non securisee n'est meme pas contactee, un telechargement sans somme est refuse
                const UpdateInfo bad = checkForUpdate(L"http://example.com/latest");
                UpdateInfo nosum = info;
                nosum.sha256.clear();
                std::wstring saved, e;
                if (bad.ok || bad.err.empty()) problem = "insecure address";
                else if (downloadUpdate(nosum, dir + L"updates_test", nullptr, saved, e) || e.empty()) problem = "download without checksum";
            }
            if (problem.empty()) report += L"PASS    " + TX("selftest.update") + L"\r\n";
            else {
                fails++;
                report += L"FAIL    " + TX("selftest.update") + L": " + W(problem) + L"\r\n";
            }
        }
#if defined(_WIN32) && !defined(__SANITIZE_ADDRESS__)  // sous le detecteur de memoire, c'est lui qui prend la main sur un plantage : ce cas n'a pas de sens
        {  // rapport de plantage local : le programme plante a dessein (--crash-test) et le rapport est lu, sans chemin ni nom de shader
            total++;
            std::string problem;
            const std::wstring rep = exeDir + L"crash-report.txt";
            platform::removeFile(rep);
            runProcess(L"\"" + exeDir + platform::exeFileName(L"ShaderToMP4") + L"\" --crash-test", [](const std::wstring&) {});
            std::string text;
            if (!readFile(rep, text)) problem = "no report written";
            else if (text.find(std::string("Version: ") + APP_VER_STR) == std::string::npos || text.find("0xC0000005") == std::string::npos || text.find("access violation") == std::string::npos ||
                     text.find("Stack:") == std::string::npos || text.find("Windows: ") == std::string::npos || text.find("Memory: ") == std::string::npos || text.find("test: crash-test") == std::string::npos ||
                     text.find("opening <path>") == std::string::npos)
                problem = "report content: " + text.substr(0, 200);
            else if (text.find("Jean") != std::string::npos || text.find("secret_shader") != std::string::npos || text.find("C:\\Users") != std::string::npos || text.find(U8(exeDir)) != std::string::npos)
                problem = "the report holds a personal path";
            platform::removeFile(rep);
            if (problem.empty()) {  // remplacement des chemins
                if (scrubPaths("a C:\\Users\\Jean Dupont\\x.glsl:12: error") != "a <path>:12: error" || scrubPaths("see D:/videos/out.mp4.") != "see <path>." || scrubPaths("no path here: 12:30") != "no path here: 12:30" ||
                    scrubPaths("\\\\server\\share\\a.glsl") != "<path>")
                    problem = "path scrubbing: " + scrubPaths("a C:\\Users\\Jean Dupont\\x.glsl:12: error");
            }
            if (problem.empty()) report += L"PASS    " + TX("selftest.crash") + L"\r\n";
            else {
                fails++;
                report += L"FAIL    " + TX("selftest.crash") + L": " + W(problem) + L"\r\n";
            }
        }
#endif
#ifdef _WIN32
        {  // fuzzing des lecteurs (JSON, montage, ini, cube, EXR, TIFF, fiche des temps, expressions...) : le programme se relance en mode --fuzz, un plantage est un code de sortie
            total++;
            std::string problem, text;
            const int rc = runProcess(L"\"" + exeDir + platform::exeFileName(L"ShaderToMP4") + L"\" --fuzz=all --fuzz-count=500 --fuzz-seed=3", [&](const std::wstring& l) { text += U8(l) + " "; });
            if (rc != 0) problem = "exit code " + std::to_string(rc) + " " + text.substr(0, 200);
            else if (text.find("no crash") == std::string::npos) problem = "no summary: " + text.substr(0, 200);
            if (problem.empty()) report += L"PASS    " + TX("selftest.fuzz") + L": " + W(text.substr(text.find("fuzz: "))) + L"\r\n";
            else {
                fails++;
                report += L"FAIL    " + TX("selftest.fuzz") + L": " + W(problem) + L"\r\n";
            }
        }
#endif
        if (const ProExt* px = proExt(); px && px->selfTest) px->selfTest(exeDir, dir, report, total, fails);  // cas de l'edition Pro, s'il y en a
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
