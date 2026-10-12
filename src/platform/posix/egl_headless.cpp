// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "platform/posix/egl_headless.h"
#include "core/i18n.h"
#include "core/util.h"

#ifndef __APPLE__
#include <dlfcn.h>
#endif

namespace platform {
namespace egl {

#ifdef __APPLE__

bool open(std::wstring& err) {
    err = L"EGL";
    return false;
}
bool available() { return false; }
void* createContext(std::wstring&) { return nullptr; }
bool makeCurrent(void*, std::wstring&) { return false; }
void release() {}
void destroyContext(void*) {}
void* proc(const char*) { return nullptr; }

#else

namespace {

using Ptr = void*;
using Int = int;
using Bool = unsigned int;

// Constantes EGL utilisees (Khronos : egl.h, eglext.h)
constexpr Int kNone = 0x3038, kSurfaceType = 0x3033, kPbufferBit = 0x0001, kRenderableType = 0x3040, kOpenGLBit = 0x0008, kRed = 0x3024,
              kGreen = 0x3023, kBlue = 0x3022, kAlpha = 0x3021, kWidth = 0x3057, kHeight = 0x3056, kContextMajor = 0x3098, kContextMinor = 0x30FB,
              kContextProfile = 0x30FD, kCoreProfileBit = 0x0001;
constexpr unsigned kOpenGLApi = 0x30A2, kPlatformSurfacelessMesa = 0x31DD;

struct Api {
    Ptr (*getProcAddress)(const char*) = nullptr;
    Ptr (*getDisplay)(Ptr) = nullptr;
    Bool (*initialize)(Ptr, Int*, Int*) = nullptr;
    Bool (*bindAPI)(unsigned) = nullptr;
    Bool (*chooseConfig)(Ptr, const Int*, Ptr*, Int, Int*) = nullptr;
    Ptr (*createContext)(Ptr, Ptr, Ptr, const Int*) = nullptr;
    Ptr (*createPbufferSurface)(Ptr, Ptr, const Int*) = nullptr;
    Bool (*makeCurrentFn)(Ptr, Ptr, Ptr, Ptr) = nullptr;
    Bool (*destroyContextFn)(Ptr, Ptr) = nullptr;
    Bool (*destroySurfaceFn)(Ptr, Ptr) = nullptr;
    Int (*getError)() = nullptr;
};

struct State {
    Api api;
    Ptr display = nullptr, config = nullptr;
    bool ready = false;
};
State g;

struct Context {
    Ptr context = nullptr, surface = nullptr;
};

template <class F>
bool load(void* lib, const char* name, F& out) {
    out = reinterpret_cast<F>(dlsym(lib, name));
    return out != nullptr;
}

std::wstring errorText(const char* what) {
    char b[96];
    snprintf(b, sizeof(b), "EGL: %s (0x%x)", what, g.api.getError ? (unsigned)g.api.getError() : 0u);
    return W(b);
}

}  // namespace

bool open(std::wstring& err) {
    if (g.ready) return true;
    void* lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        err = L"EGL: libEGL.so.1";
        return false;
    }
    Api& a = g.api;
    if (!load(lib, "eglGetProcAddress", a.getProcAddress) || !load(lib, "eglGetDisplay", a.getDisplay) || !load(lib, "eglInitialize", a.initialize) ||
        !load(lib, "eglBindAPI", a.bindAPI) || !load(lib, "eglChooseConfig", a.chooseConfig) || !load(lib, "eglCreateContext", a.createContext) ||
        !load(lib, "eglMakeCurrent", a.makeCurrentFn) || !load(lib, "eglGetError", a.getError)) {
        err = L"EGL: libEGL";
        return false;
    }
    load(lib, "eglCreatePbufferSurface", a.createPbufferSurface);
    load(lib, "eglDestroyContext", a.destroyContextFn);
    load(lib, "eglDestroySurface", a.destroySurfaceFn);
    // Mesa sait ouvrir un affichage sans serveur (surfaceless) ; sinon l'affichage par defaut du pilote (GBM, device...)
    using PlatformDisplayFn = Ptr (*)(unsigned, Ptr, const Int*);
    PlatformDisplayFn platformDisplay = reinterpret_cast<PlatformDisplayFn>(a.getProcAddress("eglGetPlatformDisplayEXT"));
    g.display = platformDisplay ? platformDisplay(kPlatformSurfacelessMesa, nullptr, nullptr) : nullptr;
    if (!g.display) g.display = a.getDisplay(nullptr);
    Int major = 0, minor = 0;
    if (!g.display || !a.initialize(g.display, &major, &minor)) {
        err = errorText("eglInitialize");
        return false;
    }
    if (!a.bindAPI(kOpenGLApi)) {
        err = errorText("eglBindAPI(OpenGL)");
        return false;
    }
    Ptr config = nullptr;
    Int count = 0;
    const Int withPbuffer[] = {kSurfaceType, kPbufferBit, kRenderableType, kOpenGLBit, kRed, 8, kGreen, 8, kBlue, 8, kAlpha, 8, kNone};
    const Int anySurface[] = {kRenderableType, kOpenGLBit, kNone};
    if (!a.chooseConfig(g.display, withPbuffer, &config, 1, &count) || count < 1) {
        if (!a.chooseConfig(g.display, anySurface, &config, 1, &count) || count < 1) {
            err = errorText("eglChooseConfig");
            return false;
        }
    }
    g.config = config;
    g.ready = true;
    return true;
}

bool available() { return g.ready; }

void* createContext(std::wstring& err) {
    if (!g.ready) return nullptr;
    const Int ctxAttribs[] = {kContextMajor, 3, kContextMinor, 3, kContextProfile, kCoreProfileBit, kNone};
    Ptr context = g.api.createContext(g.display, g.config, nullptr, ctxAttribs);
    if (!context) {
        err = errorText("eglCreateContext 3.3 core");
        return nullptr;
    }
    auto* c = new Context;
    c->context = context;
    if (g.api.createPbufferSurface) {  // sans tampon, le contexte reste utilisable sans surface (EGL_KHR_surfaceless_context)
        const Int pb[] = {kWidth, 64, kHeight, 64, kNone};
        c->surface = g.api.createPbufferSurface(g.display, g.config, pb);
    }
    return c;
}

bool makeCurrent(void* context, std::wstring& err) {
    auto* c = static_cast<Context*>(context);
    if (!g.ready || !c) return false;
    if (!g.api.makeCurrentFn(g.display, c->surface, c->surface, c->context)) {
        err = errorText("eglMakeCurrent");
        return false;
    }
    return true;
}

void release() {
    if (g.ready) g.api.makeCurrentFn(g.display, nullptr, nullptr, nullptr);
}

void destroyContext(void* context) {
    auto* c = static_cast<Context*>(context);
    if (!c) return;
    if (g.ready && g.api.destroyContextFn) g.api.destroyContextFn(g.display, c->context);
    if (g.ready && c->surface && g.api.destroySurfaceFn) g.api.destroySurfaceFn(g.display, c->surface);
    delete c;
}

void* proc(const char* name) { return g.ready ? g.api.getProcAddress(name) : nullptr; }

#endif  // __APPLE__

}  // namespace egl
}  // namespace platform
