// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// ShaderToMP4 sous Linux et macOS : ligne de commande, langue, puis interface GLFW + Dear ImGui (src/ui_imgui).
#include "core/ffmpeg_download.h"
#include "core/gl.h"
#include "core/render.h"
#include "core/i18n.h"
#include "core/selftest.h"
#include "core/util.h"
#include "platform/platform.h"

namespace {

std::string versionText() {
    std::string t;
    t += std::string(APP_NAME_STR) + " " + APP_VER_STR + "\n";
    t += std::string("Copyright (C) ") + APP_COPYRIGHT_YEAR + " " + APP_COPYRIGHT_OWNER + "\n";
    t += i18n::tr8("about.license") + "\n";
    t += i18n::tr8("about.contact") + ": " + APP_CONTACT_EMAIL + "\n";
    t += i18n::tr8("about.website") + ": " + APP_WEBSITE_URL + "\n";
    return t;
}

std::string helpText() {
    std::string t = versionText() + "\n" + i18n::tr8("cli.usage") + "\n\n";
    for (const char* k : {"cli.opt.files", "cli.opt.selftest", "cli.opt.update", "cli.opt.render", "cli.opt.quiet", "cli.opt.get_ffmpeg", "cli.opt.version",
                          "cli.opt.help"})
        t += "  " + i18n::tr8(k) + "\n";
    return t;
}

// Rendu sans fenêtre : ShaderToMP4 --render fichier.glsl... (réglages de config.ini et des commentaires // du shader)
int renderCommandLine(const std::wstring& exeDir, const std::vector<std::wstring>& files) {
    Settings base;
    std::string text;
    if (readFile(exeDir + L"config.ini", text)) parseLines(base, text, false);
    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        fprintf(stderr, "%s
", U8(TXF("log.error", err.c_str())).c_str());
        return 1;
    }
    Hooks H;
    H.log = [](const std::wstring& s) {
        puts(U8(s).c_str());
        fflush(stdout);
    };
    int fails = 0;
    for (const auto& f : files) {
        std::wstring e;
        puts(U8(f).c_str());
        if (convertFile(f, exeDir, base, H, e) != Result::Ok) {
            fprintf(stderr, "%s
", U8(TXF("log.error", e.c_str())).c_str());
            fails++;
        }
    }
    destroyGLContext(gl);
    return fails ? 1 : 0;
}

}  // namespace

#ifdef SHADERTOMP4_NO_UI
int runGui(const std::wstring&, const std::vector<std::wstring>&) {
    fputs(helpText().c_str(), stdout);
    return 0;
}
#else
int runGui(const std::wstring& exeDir, const std::vector<std::wstring>& files);
void setScreenshotRequest(const std::string& path, bool about);
bool exportLogo(int size, const std::string& path);
#endif

int main(int argc, char** argv) {
    const std::wstring exeDir = platform::appDir();
    if (!i18n::init(exeDir, "")) {
        fprintf(stderr, "The built-in language data is missing or damaged.\n");  // i18n-bootstrap
        return 1;
    }
    std::vector<std::wstring> files;
    bool render = false, selftest = false, update = false, quiet = false, getFfmpeg = false, version = false, help = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--selftest") selftest = true;
        else if (a == "--render") render = true;
        else if (a == "--update") update = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--get-ffmpeg") getFfmpeg = true;
        else if (a == "--version") version = true;
        else if (a == "--help" || a == "-h") help = true;
#ifndef SHADERTOMP4_NO_UI
        else if (a == "--export-logo" && i + 2 < argc) {
            const int size = atoi(argv[i + 1]);
            return exportLogo(size, argv[i + 2]) ? 0 : 1;
        }
        else if (a == "--screenshot" && i + 1 < argc) setScreenshotRequest(argv[++i], false);
        else if (a == "--screenshot-about" && i + 1 < argc) setScreenshotRequest(argv[++i], true);
#endif
        else files.push_back(W(a));
    }
    if (version || help) {
        fputs((help ? helpText() : versionText()).c_str(), stdout);
        return 0;
    }
    if (selftest) return runSelfTest(exeDir, update, quiet);
    if (render) return renderCommandLine(exeDir, files);
    if (getFfmpeg) {
        std::wstring e;
        bool ok = downloadFfmpeg(exeDir, [&](int pct, const std::wstring& msg) {
            if (!quiet) fprintf(stderr, "%3d %%  %s\n", pct, U8(msg).c_str());
        }, nullptr, e);
        if (!quiet) fprintf(stderr, "%s\n", U8(ok ? TX("dl.installed") : e).c_str());
        return ok ? 0 : 1;
    }
    return runGui(exeDir, files);
}
