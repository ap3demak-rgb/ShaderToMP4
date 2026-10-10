// En-tete commun : Windows, OpenGL 1.1 et bibliotheque standard (utilise comme en-tete precompile).
#pragma once
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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "version.h"
