// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "selftest.h"
#include "gl.h"
#include "i18n.h"
#include "render.h"
#include "util.h"
#include "platform/platform.h"

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
        destroyGLContext(gl);
    }
    report += L"\r\n" + TXF("selftest.summary", total, fails) + L"\r\n";
    if (FILE* f = platform::openFile(dir + L"report.txt", "wb")) {
        std::string u = U8(report);
        fwrite(u.data(), 1, u.size(), f);
        fclose(f);
    }
    if (!quiet)
        platform::showMessage(APP_NAME_WSTR L" - " + TX("selftest.title"), report,
                              fails ? platform::MessageKind::Warning : platform::MessageKind::Info);
    return fails ? 1 : 0;
}
