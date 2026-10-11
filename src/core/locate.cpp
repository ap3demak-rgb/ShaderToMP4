// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "locate.h"
#include "util.h"
#include "platform/platform.h"
#include <cwctype>
#include <regex>

int glslErrorLine(const std::wstring& line) {
    static const std::wregex nv(L"^\\s*(?:ERROR:\\s*)?\\d+\\((\\d+)\\)");    // 0(12) : error C1008
    static const std::wregex mesa(L"^\\s*(?:ERROR:\\s*)?\\d+:(\\d+)[:(]");   // 0:12(5): error:   /   ERROR: 0:12: ...
    std::wsmatch m;
    if (std::regex_search(line, m, nv) || std::regex_search(line, m, mesa)) return (int)wcstol(m[1].str().c_str(), nullptr, 10);
    return 0;
}

std::wstring locateGlslErrors(const std::wstring& err, const std::wstring& file, int fileLines, const std::wstring& commonFile) {
    std::wstring out;
    size_t pos = 0;
    bool first = true;
    while (pos <= err.size()) {
        size_t e = err.find(L'\n', pos);
        if (e == std::wstring::npos) e = err.size();
        std::wstring line = err.substr(pos, e - pos);
        pos = e + 1;
        const int n = first ? 0 : glslErrorLine(line);  // la premiere ligne est le titre de l'erreur
        first = false;
        if (n > 0) {
            const std::wstring& target = (!commonFile.empty() && n > fileLines) ? commonFile : file;
            line = target + L":" + std::to_wstring(n) + L": " + line;
        }
        out += line;
        if (e < err.size()) out += L'\n';
    }
    return out;
}

bool parseLocation(const std::wstring& logLine, std::wstring& path, int& line) {
    for (size_t i = 1; i + 2 < logLine.size(); i++) {
        if (logLine[i] != L':' || !iswdigit(logLine[i + 1])) continue;
        size_t j = i + 1;
        while (j < logLine.size() && iswdigit(logLine[j])) j++;
        if (j >= logLine.size() || logLine[j] != L':') continue;
        std::wstring p = logLine.substr(0, i);
        const size_t a = p.find_first_not_of(L" \t");
        if (a == std::wstring::npos) continue;
        p = p.substr(a);
        if (!fileExists(p)) continue;
        path = p;
        line = (int)wcstol(logLine.substr(i + 1, j - i - 1).c_str(), nullptr, 10);
        return true;
    }
    return false;
}

void openAtLine(const std::wstring& path, int line) {
    const std::wstring ln = std::to_wstring(std::max(1, line));
    auto launch = [&](const std::vector<std::wstring>& argv) {
        auto p = platform::Process::start(argv, false, false);
        if (!p) return false;
        p.release();  // l'editeur continue apres nous : on ne le tue pas (le destructeur le ferait)
        return true;
    };
    const std::wstring npp = platform::findInPath(platform::exeFileName(L"notepad++"));
    if (!npp.empty() && launch({npp, L"-n" + ln, path})) return;
    const std::wstring subl = platform::findInPath(platform::exeFileName(L"subl"));
    if (!subl.empty() && launch({subl, path + L":" + ln})) return;
#ifdef _WIN32
    const std::wstring code = platform::findInPath(L"code.cmd");
    if (!code.empty() && launch({L"cmd.exe", L"/c", code, L"-g", path + L":" + ln})) return;
#else
    const std::wstring code = platform::findInPath(L"code");
    if (!code.empty() && launch({code, L"-g", path + L":" + ln})) return;
#endif
    platform::openWithDefault(path);
}
