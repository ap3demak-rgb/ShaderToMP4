// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "cli.h"
#include "ffmpeg_download.h"
#include "formats.h"
#include "gl.h"
#include "i18n.h"
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
                          "cli.render.exit_codes"})
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
