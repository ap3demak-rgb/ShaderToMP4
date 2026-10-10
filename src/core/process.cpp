// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "process.h"
#include "util.h"
#include "platform/platform.h"

int runProcess(const std::wstring& cmd, const std::function<void(const std::wstring&)>& onLine) {
    auto proc = platform::Process::start(platform::splitCommand(cmd), false, true);
    if (!proc) return -1;
    std::string acc;
    char tmp[4096];
    size_t got;
    while ((got = proc->readOutput(tmp, sizeof(tmp))) > 0) {
        acc.append(tmp, got);
        size_t p;
        while ((p = acc.find('\n')) != std::string::npos) {
            std::string line = trim(acc.substr(0, p));
            acc.erase(0, p + 1);
            if (!line.empty() && onLine) onLine(W(line));
        }
    }
    acc = trim(acc);
    if (!acc.empty() && onLine) onLine(W(acc));
    return proc->wait();
}
