// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "queue.h"
#include "util.h"
#include "platform/platform.h"

static const wchar_t* kQueueFile = L"queue.txt";

void saveQueue(const std::wstring& exeDir, const std::vector<std::wstring>& pending) {
    static std::string last;
    std::string text;
    for (const auto& p : pending) text += U8(p) + "\n";
    if (text == last) return;
    last = text;
    const std::wstring path = exeDir + kQueueFile;
    if (text.empty()) {
        platform::removeFile(path);
        return;
    }
    if (FILE* f = platform::openFile(path, "wb")) {
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }
}

std::vector<std::wstring> loadQueue(const std::wstring& exeDir) {
    std::vector<std::wstring> out;
    std::string text;
    if (!readFile(exeDir + kQueueFile, text)) return out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::wstring p = W(trim(text.substr(pos, e - pos)));
        pos = e + 1;
        if (!p.empty() && fileExists(p)) out.push_back(p);
    }
    return out;
}
