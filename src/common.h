// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// En-tete commun : Windows (ou GL seul ailleurs), OpenGL 1.1 et bibliotheque standard (utilise comme en-tete precompile).
#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wininet.h>
#include <wincrypt.h>
#define _RICHEDIT_VER 0x0500
#include <richedit.h>
#ifndef BIF_RETURNONLYDIRS
#define BIF_RETURNONLYDIRS 0x0001
#endif
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#include <GL/gl.h>

#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#ifndef APIENTRY
#define APIENTRY
#endif
#else
#include <GL/gl.h>
#ifndef APIENTRY
#define APIENTRY
#endif
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "app_info.h"
