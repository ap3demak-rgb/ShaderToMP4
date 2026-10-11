// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "cli.h"
#include "app_info.h"
#include "encoder.h"
#include "ffmpeg_download.h"
#include "formats.h"
#include "gl.h"
#include "i18n.h"
#include "process.h"
#include "render.h"
#include "util.h"
#include "platform/platform.h"

namespace {

std::string jsonString(const std::string& s) {
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

std::string num(double v) {
    char b[48];
    snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

}  // namespace

std::string renderHelpText() {
    std::string t = "\n" + i18n::tr8("cli.render_title") + "\n\n";
    for (const char* k : {"cli.render.opt_set", "cli.render.opt_profile", "cli.render.opt_progress", "cli.render.opt_png",
                          "cli.render.exit_codes", "cli.opt.diagnostic", "cli.opt.benchmark"})
        t += "  " + i18n::tr8(k) + "\n";
    return t;
}

int runRenderCli(const std::wstring& exeDir, const std::vector<std::wstring>& args,
                 const std::function<void(const std::string&, bool)>& write) {
    Settings base;
    std::string text;
    if (readFile(exeDir + L"config.ini", text)) parseLines(base, text, false);

    std::vector<std::wstring> files;
    std::string progress = "none";  // none | text | json
    bool snapshot = false;
    std::vector<std::pair<std::string, std::string>> overrides;
    auto fail = [&](const std::wstring& msg) {
        write(U8(TXF("log.error", msg.c_str())) + "\n", true);
        return kExitUsage;
    };
    for (const auto& a : args) {
        if (a.compare(0, 2, L"--") != 0) {
            files.push_back(a);
            continue;
        }
        const std::string arg = U8(a.substr(2));
        const size_t eq = arg.find('=');
        const std::string key = lower(eq == std::string::npos ? arg : arg.substr(0, eq));
        const std::string val = eq == std::string::npos ? "" : arg.substr(eq + 1);
        if (key == "render" || key == "quiet") continue;
        if (key == "png") {
            snapshot = true;
        } else if (key == "progress") {
            progress = lower(val);
            if (progress != "json" && progress != "text" && progress != "none") return fail(TXF("cli.bad_progress", W(val).c_str()));
        } else if (key == "profile") {
            std::string p;
            if (!readFile(W(val), p)) return fail(TXF("error.profile_read", W(val).c_str()));
            parseLines(base, p, false);
        } else if (eq != std::string::npos && isSettingKey(key)) {
            overrides.push_back({key, val});
        } else {
            return fail(TXF("cli.unknown_option", W(arg).c_str()));
        }
    }
    for (const auto& kv : overrides) applySetting(base, kv.first, kv.second);
    if (files.empty()) return fail(TX("cli.no_files"));
    for (const auto& f : files)
        if (!fileExists(f)) return fail(TXF("cli.file_missing", f.c_str()));
    if (!ffmpegPresent(exeDir)) {
        write(U8(TXF("log.error", TX("cli.no_ffmpeg").c_str())) + "\n", true);
        return kExitNoFfmpeg;
    }

    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        write(U8(TXF("log.error", err.c_str())) + "\n", true);
        return kExitNoGL;
    }
    const bool json = progress == "json";
    int ok = 0, failed = 0, index = 0, lastDecile = -1;
    for (const auto& f : files) {
        index++;
        std::wstring currentOutput;
        Hooks H;
        H.snapshot = snapshot;
        H.log = [&](const std::wstring& s) {
            if (json) write("{\"event\":\"log\",\"message\":" + jsonString(U8(s)) + "}\n", false);
            else write(U8(s) + "\n", false);
        };
        H.progress = [&](long long n, long long total, double speed, double remaining) {
            const double pct = total > 0 ? 100.0 * n / total : 0.0;
            if (json) {
                write("{\"event\":\"progress\",\"file\":" + jsonString(U8(f)) + ",\"frame\":" + std::to_string(n) + ",\"frames\":" +
                          std::to_string(total) + ",\"percent\":" + num(pct) + ",\"fps\":" + num(speed) + ",\"eta\":" + num(remaining) + "}\n",
                      false);
            } else if (progress == "text" && (int)(pct / 10) != lastDecile) {
                lastDecile = (int)(pct / 10);
                write(U8(wfmt(L"  %3.0f %%  (%lld/%lld)", pct, n, total)) + "\n", false);
            }
        };
        H.output = [&](const std::wstring& path) { currentOutput = path; };
        lastDecile = -1;
        if (json) write("{\"event\":\"start\",\"file\":" + jsonString(U8(f)) + ",\"index\":" + std::to_string(index) + ",\"count\":" +
                            std::to_string(files.size()) + "}\n", false);
        else write(U8(f) + "\n", false);
        std::wstring e;
        if (convertFile(f, exeDir, base, H, e) == Result::Ok) {
            ok++;
            if (json) write("{\"event\":\"done\",\"file\":" + jsonString(U8(f)) + ",\"output\":" + jsonString(U8(currentOutput)) + "}\n", false);
        } else {
            failed++;
            if (json) write("{\"event\":\"error\",\"file\":" + jsonString(U8(f)) + ",\"message\":" + jsonString(U8(e)) + "}\n", false);
            else write(U8(TXF("log.error", e.c_str())) + "\n", true);
        }
    }
    destroyGLContext(gl);
    if (json) write("{\"event\":\"end\",\"ok\":" + std::to_string(ok) + ",\"failed\":" + std::to_string(failed) + "}\n", false);
    return failed ? kExitRenderFailed : kExitOk;
}

// ---------------------------------------------------------------------------
//  --diagnostic
// ---------------------------------------------------------------------------
int runDiagnosticCli(const std::wstring& exeDir, const std::function<void(const std::string&, bool)>& write) {
    std::string o;
    auto line = [&](const std::string& label, const std::string& value) { o += label + ": " + value + "\n"; };
#if defined(_WIN32)
    const char* os = "Windows";
#elif defined(__APPLE__)
    const char* os = "macOS";
#else
    const char* os = "Linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char* arch = "arm64";
#else
    const char* arch = "x86_64";
#endif
    o += std::string(APP_NAME_STR) + " " + APP_VER_STR + "\n";
    line(i18n::tr8("diag.system"), std::string(os) + " " + arch);
    line(i18n::tr8("diag.threads"), std::to_string(std::thread::hardware_concurrency()));
    line(i18n::tr8("diag.language"), i18n::language());

    GLContext gl;
    std::wstring err;
    if (createGLContext(gl, err)) {
        auto gs = [](unsigned name) {
            const unsigned char* s = glGetString(name);
            return s ? std::string((const char*)s) : std::string("?");
        };
        GLint maxTex = 0;
        glGetIntegerv(0x0D33 /*GL_MAX_TEXTURE_SIZE*/, &maxTex);
        line(i18n::tr8("diag.gl_vendor"), gs(0x1F00));
        line(i18n::tr8("diag.gl_renderer"), gs(0x1F01));
        line(i18n::tr8("diag.gl_version"), gs(0x1F02));
        line(i18n::tr8("diag.glsl_version"), gs(0x8B8C));
        line(i18n::tr8("diag.max_texture"), std::to_string(maxTex));
    } else {
        line(i18n::tr8("diag.opengl"), U8(err));
    }

    const std::wstring ffmpeg = findFfmpeg(exeDir);
    std::string first;
    const int rc = runProcess(L"\"" + ffmpeg + L"\" -hide_banner -version", [&](const std::wstring& l) {
        if (first.empty()) first = U8(l);
    });
    if (rc != 0 || first.empty()) {
        line("ffmpeg", i18n::tr8("diag.not_found"));
    } else {
        line("ffmpeg", first);
        std::string codecs, containers;
        for (int i = 0; i < codecCount(); i++)
            codecs += U8(codecAt(i).label) + (codecAvailable(ffmpeg, codecAt(i).id) ? "+ " : "- ");
        for (int i = 0; i < containerCount(); i++)
            containers += U8(containerAt(i).label) + (containerAvailable(ffmpeg, containerAt(i).id) ? "+ " : "- ");
        line(i18n::tr8("diag.codecs"), codecs);
        line(i18n::tr8("diag.containers"), containers);
        Hooks H;
        H.log = [](const std::wstring&) {};
        std::string hw;
        for (const char* codec : {"h264", "h265", "av1"}) {
            Settings s;
            s.codec = codec;
            s.encoder = "auto";
            hw += std::string(codec) + "=" + resolveEncoder(s, ffmpeg, H) + " ";
        }
        line(i18n::tr8("diag.hw_encoders"), hw);
    }
    if (gl.impl) destroyGLContext(gl);
    write(o, false);
    return kExitOk;
}

// ---------------------------------------------------------------------------
//  --benchmark
// ---------------------------------------------------------------------------
int runBenchmarkCli(const std::wstring& exeDir, const std::vector<std::wstring>& args,
                    const std::function<void(const std::string&, bool)>& write) {
    double seconds = 2.0;
    bool json = false, quick = false;
    for (const auto& a : args) {
        const std::string s = U8(a);
        if (s == "--json") json = true;
        else if (s == "--quick") quick = true;  // sans le 4K (machines lentes, integration continue)
        else if (s.compare(0, 10, "--seconds=") == 0) seconds = std::max(0.5, std::min(30.0, atof(s.c_str() + 10)));
    }
    if (!ffmpegPresent(exeDir)) {
        write(U8(TXF("log.error", TX("cli.no_ffmpeg").c_str())) + "\n", true);
        return kExitNoFfmpeg;
    }
    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        write(U8(TXF("log.error", err.c_str())) + "\n", true);
        return kExitNoGL;
    }
    // Shader de mesure : plasma (un peu de calcul par pixel), ecrit dans un dossier temporaire a cote du programme
    const std::wstring dir = exeDir + L"benchmark_tmp";
    platform::makeDir(dir);
    const std::wstring shader = dir + platform::pathSeparator() + L"bench.glsl";
    const std::string src =
        "void mainImage(out vec4 o, in vec2 p){ vec2 uv = p / iResolution.xy; float t = iTime;"
        " float v = sin(uv.x * 10.0 + t) + sin(uv.y * 10.0 - t) + sin((uv.x + uv.y) * 8.0 + t * 0.5)"
        " + sin(length(uv - 0.5) * 20.0 - t * 2.0);"
        " o = vec4(0.5 + 0.5 * sin(vec3(v, v + 2.0, v + 4.0)), 1.0); }\n";
    if (FILE* f = platform::openFile(shader, "wb")) {
        fwrite(src.data(), 1, src.size(), f);
        fclose(f);
    }
    struct Case { int w, h; const char* encoder; };
    const Case cases[] = {{1280, 720, "cpu"}, {1920, 1080, "cpu"}, {1920, 1080, "auto"}, {3840, 2160, "cpu"}};
    const std::string hwPrefix = U8(TX("log.hw_encoder")).substr(0, U8(TX("log.hw_encoder")).find('%'));
    std::string text, js = "{\"version\":\"" + std::string(APP_VER_STR) + "\",\"results\":[";
    bool first = true;
    for (const Case& c : cases) {
        if (quick && c.w > 1920) continue;
        Settings s;
        s.width = c.w;
        s.height = c.h;
        s.fps = 60;
        s.duration = seconds;
        s.crf = 23;
        s.preset = "veryfast";
        s.encoder = c.encoder;
        s.outdir = dir;
        s.exists = "overwrite";
        s.chunk = 0;
        double speed = 0;
        std::string used = c.encoder;
        Hooks H;
        H.log = [&](const std::wstring& l) {
            const std::string u = U8(l);
            const size_t p = u.find(hwPrefix);
            if (!hwPrefix.empty() && p != std::string::npos) used = u.substr(p + hwPrefix.size());
        };
        H.progress = [&](long long, long long, double sp, double) { speed = sp; };
        std::wstring e;
        const bool ok = convertFile(shader, exeDir, s, H, e) == Result::Ok;
        char b[160];
        snprintf(b, sizeof(b), "%dx%d  %-14s  %8.1f %s\n", c.w, c.h, used.c_str(), ok ? speed : 0.0, ok ? "fps" : U8(e).c_str());
        text += b;
        snprintf(b, sizeof(b), "%s{\"width\":%d,\"height\":%d,\"encoder\":\"%s\",\"fps\":%.1f}", first ? "" : ",", c.w, c.h, used.c_str(), ok ? speed : 0.0);
        js += b;
        first = false;
    }
    js += "]}\n";
    removeTree(dir);
    destroyGLContext(gl);
    write(json ? js : U8(TX("diag.benchmark_title")) + "\n" + text, false);
    return kExitOk;
}
