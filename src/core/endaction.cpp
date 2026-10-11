// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "endaction.h"
#include "i18n.h"
#include "util.h"
#include "platform/platform.h"
#include <thread>

bool isEndAction(const std::string& a) { return a == "none" || a == "folder" || a == "sleep"; }

static void suspendComputer() {
#if defined(_WIN32)
    auto p = platform::Process::start({L"rundll32.exe", L"powrprof.dll,SetSuspendState", L"0,1,0"}, false, false);
#elif defined(__APPLE__)
    auto p = platform::Process::start({L"pmset", L"sleepnow"}, false, false);
#else
    auto p = platform::Process::start({L"systemctl", L"suspend"}, false, false);
#endif
    if (p) p->wait();
}

void performEndAction(const std::string& action, const std::wstring& lastOutput, const std::function<void(const std::wstring&)>& log,
                      bool blocking) {
    if (action == "folder") {
        if (!lastOutput.empty() && fileExists(lastOutput)) platform::revealInFolder(lastOutput);
    } else if (action == "sleep") {
        log(L"  " + TX("log.endaction_sleep"));
        if (blocking) {
            platform::sleepMs(15000);
            suspendComputer();
        } else {
            std::thread([] {
                platform::sleepMs(15000);
                suspendComputer();
            }).detach();
        }
    }
}
