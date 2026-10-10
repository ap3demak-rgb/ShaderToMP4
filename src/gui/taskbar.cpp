// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "taskbar.h"
#include <shobjidl.h>

#ifdef _MSC_VER
#pragma comment(lib, "ole32.lib")
#endif

namespace taskbar {
static ITaskbarList3* tb = nullptr;
static HWND wnd = nullptr;

void init(HWND w) {
    wnd = w;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tb)))) {
        if (FAILED(tb->HrInit())) {
            tb->Release();
            tb = nullptr;
        }
    }
}

void progress(long long cur, long long total) {
    if (tb) tb->SetProgressValue(wnd, (ULONGLONG)std::max(0LL, cur), (ULONGLONG)std::max(1LL, total));
}
void normal() { if (tb) tb->SetProgressState(wnd, TBPF_NORMAL); }
void paused() { if (tb) tb->SetProgressState(wnd, TBPF_PAUSED); }
void error() { if (tb) tb->SetProgressState(wnd, TBPF_ERROR); }
void indeterminate() { if (tb) tb->SetProgressState(wnd, TBPF_INDETERMINATE); }
void clear() { if (tb) tb->SetProgressState(wnd, TBPF_NOPROGRESS); }

void flash(HWND w) {
    if (GetForegroundWindow() == w) return;
    FLASHWINFO fi{sizeof(fi), w, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
    FlashWindowEx(&fi);
}

void shutdown() {
    if (tb) tb->Release();
    tb = nullptr;
}
}  // namespace taskbar
