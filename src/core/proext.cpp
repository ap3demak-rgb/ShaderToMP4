// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "proext.h"

// L'edition Pro fournit registerProExtensions() ; dans l'edition libre elle est absente : l'editeur de liens la remplace par une fonction vide (MSVC) ou
// la laisse nulle (symbole faible, GCC et Clang).
#if defined(_MSC_VER)
extern "C" void registerProExtensions();
extern "C" void registerProExtensionsNone() {}
#pragma comment(linker, "/alternatename:registerProExtensions=registerProExtensionsNone")
#else
extern "C" __attribute__((weak)) void registerProExtensions();
#endif

namespace {
ProExt gExt;
bool gRegistered = false;
}  // namespace

void registerProExt(const ProExt& ext) {
    gExt = ext;
    gRegistered = true;
}

const ProExt* proExt() { return gRegistered ? &gExt : nullptr; }

std::unique_ptr<ProScene> createProScene(const SceneRequest& rq, SceneProgram& program, std::wstring& err) {
    err.clear();
    const ProExt* px = proExt();
    if (!px || !px->makeScene) return nullptr;
    std::unique_ptr<ProScene> scene = px->makeScene(rq, err);
    if (!scene) return nullptr;
    program.version = scene->version();
    program.vertex = scene->vertexSource();
    program.fragmentPrelude = scene->fragmentPrelude();
    return scene;
}

void initProExtensions() {
    static bool done = false;
    if (done) return;
    done = true;
#if !defined(_MSC_VER)
    if (!registerProExtensions) return;
#endif
    registerProExtensions();
}
