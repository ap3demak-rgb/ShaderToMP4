#include "selftest.h"
#include "gl.h"
#include "render.h"
#include "util.h"

int runSelfTest(const std::wstring& exeDir, bool update, bool quiet) {
    std::wstring dir = exeDir + L"tests\\";
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) dir = exeDir + L"..\\tests\\";
    std::wstring report;
    int total = 0, fails = 0;
    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        report = L"ERREUR : " + err + L"\r\n";
        fails = 1;
    } else {
        Settings base;
        base.width = 64;
        base.height = 64;
        base.ss = 1;
        base.blur = 1;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"*.glsl").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) {
            report = L"Aucun shader dans " + dir + L"\r\n";
            fails = 1;
        } else {
            do {
                total++;
                const std::wstring file = fd.cFileName;
                const std::wstring ref = dir + file.substr(0, file.find_last_of(L'.')) + L".ref";
                std::vector<uint8_t> got;
                Hooks H;
                H.log = [](const std::wstring&) {};
                H.previewTime = 1.0;
                H.previewFrame = [&](int, int, std::vector<uint8_t>&& px) { got = std::move(px); };
                std::wstring e;
                std::wstring line;
                if (convertFile(dir + file, exeDir, base, H, e) != Result::Ok || got.empty()) {
                    line = L"ECHEC  " + file + L" : " + (e.empty() ? L"rendu impossible" : e);
                    fails++;
                } else {
                    std::string want;
                    const bool have = readFile(ref, want);
                    if (update || !have) {
                        if (!update) {
                            line = L"ECHEC  " + file + L" : reference absente (lance --selftest --update)";
                            fails++;
                        } else if (FILE* f = _wfopen(ref.c_str(), L"wb")) {
                            fwrite(got.data(), 1, got.size(), f);
                            fclose(f);
                            line = L"MAJ    " + file + L" : reference ecrite";
                        }
                    } else if (want.size() != got.size()) {
                        line = L"ECHEC  " + file + L" : taille de reference differente";
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
                        line = wfmt(mad <= 2.0 ? L"OK     %ls (ecart moyen %.2f/255)" : L"ECHEC  %ls (ecart moyen %.2f/255, max 2)",
                                    file.c_str(), mad);
                        if (mad > 2.0) fails++;
                    }
                }
                report += line + L"\r\n";
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        destroyGLContext(gl);
    }
    report += wfmt(L"\r\n%d test(s), %d echec(s)\r\n", total, fails);
    if (FILE* f = _wfopen((dir + L"report.txt").c_str(), L"wb")) {
        std::string u = U8(report);
        fwrite(u.data(), 1, u.size(), f);
        fclose(f);
    }
    if (!quiet) MessageBoxW(nullptr, report.c_str(), L"ShaderToMP4 - auto-test", fails ? MB_ICONWARNING : MB_ICONINFORMATION);
    return fails ? 1 : 0;
}
