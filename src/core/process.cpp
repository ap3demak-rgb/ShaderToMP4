// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "process.h"
#include "util.h"

int runProcess(const std::wstring& cmd, const std::function<void(const std::wstring&)>& onLine) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring buf = cmd;
    BOOL ok = CreateProcessW(nullptr, &buf[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        return -1;
    }
    std::string acc;
    char tmp[4096];
    DWORD got = 0;
    while (ReadFile(rd, tmp, sizeof(tmp), &got, nullptr) && got > 0) {
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
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}
