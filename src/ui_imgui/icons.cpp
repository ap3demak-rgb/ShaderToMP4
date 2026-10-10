// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "ui_imgui/icons.h"
#include "platform/platform.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

namespace ui {

namespace {
std::map<std::pair<std::string, int>, unsigned>& cache() {
    static std::map<std::pair<std::string, int>, unsigned> c;
    return c;
}
unsigned makeTexture(int w, int h, const uint8_t* rgba, unsigned existing) {
    GLuint t = existing;
    if (!t) glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);  // GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return t;
}
}  // namespace

unsigned iconTexture(const char* name, int px) {
    auto key = std::make_pair(std::string(name), px);
    auto it = cache().find(key);
    if (it != cache().end()) return it->second;
    std::string svg;
    if (!platform::embeddedData((std::string("icons/") + name + ".svg").c_str(), svg)) return 0;
    svg.push_back('\0');  // nsvgParse modifie et exige une chaîne terminée
    NSVGimage* img = nsvgParse(&svg[0], "px", 96.0f);
    if (!img) return 0;
    unsigned tex = 0;
    const float scale = (float)px / std::max(img->width, img->height);
    const int w = std::max(1, (int)std::ceil(img->width * scale)), h = std::max(1, (int)std::ceil(img->height * scale));
    if (NSVGrasterizer* rast = nsvgCreateRasterizer()) {
        std::vector<uint8_t> pixels((size_t)w * h * 4, 0);
        nsvgRasterize(rast, img, 0, 0, scale, pixels.data(), w, h, w * 4);
        tex = makeTexture(w, h, pixels.data(), 0);
        nsvgDeleteRasterizer(rast);
    }
    nsvgDelete(img);
    cache()[key] = tex;
    return tex;
}

unsigned uploadBGRA(unsigned texture, int w, int h, const uint8_t* bgra) {
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        rgba[i * 4] = bgra[i * 4 + 2];
        rgba[i * 4 + 1] = bgra[i * 4 + 1];
        rgba[i * 4 + 2] = bgra[i * 4];
        rgba[i * 4 + 3] = 255;
    }
    return makeTexture(w, h, rgba.data(), texture);
}

void freeTextures() {
    for (auto& kv : cache()) {
        GLuint t = kv.second;
        if (t) glDeleteTextures(1, &t);
    }
    cache().clear();
}

}  // namespace ui
