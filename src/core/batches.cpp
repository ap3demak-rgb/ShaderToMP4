// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "batches.h"
#include "util.h"
#include "platform/platform.h"

static std::wstring batchFile(const std::wstring& exeDir) { return exeDir + L"jobs/batches.txt"; }

BatchMap loadBatches(const std::wstring& exeDir) {
    BatchMap out;
    std::string text;
    if (!readFile(batchFile(exeDir), text)) return out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const int n = atoi(line.substr(0, tab).c_str());
        const std::wstring path = W(line.substr(tab + 1));
        if (n >= 1 && n <= kBatchCount && !path.empty()) out[path] = n;
    }
    return out;
}

void saveBatches(const std::wstring& exeDir, const BatchMap& batches) {
    static std::string last;
    std::string text;
    for (const auto& kv : batches)
        if (kv.second >= 1 && kv.second <= kBatchCount) text += std::to_string(kv.second) + "\t" + U8(kv.first) + "\n";
    if (text == last) return;
    last = text;
    const std::wstring path = batchFile(exeDir);
    if (text.empty()) {
        platform::removeFile(path);
        return;
    }
    platform::makeDir(exeDir + L"jobs");
    writeFileAtomic(path, text);
}

int batchOf(const BatchMap& batches, const std::wstring& path) {
    const auto it = batches.find(path);
    return it == batches.end() ? 0 : it->second;
}

std::wstring batchLabel(const BatchMap& batches, const std::wstring& path) {
    const int n = batchOf(batches, path);
    return n > 0 ? L"[" + std::to_wstring(n) + L"] " + path : path;
}
