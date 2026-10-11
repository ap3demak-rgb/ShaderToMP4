// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "channels.h"
#include "i18n.h"
#include "process.h"
#include "util.h"
#include "platform/platform.h"
#include <cctype>
#include <cwctype>

namespace {

constexpr GLenum K_TEXTURE0 = 0x84C0;
constexpr GLenum K_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum K_REPEAT = 0x2901;
constexpr GLenum K_LINEAR_MIPMAP_LINEAR = 0x2703;

struct Pixels {
    int w = 0, h = 0;
    bool isFloat = false;
    std::vector<uint8_t> data;  // RGBA8 ou RGBA32F, premiere ligne en haut
};

// Bruit deterministe (xorshift) : la meme image a chaque rendu, pour des videos reproductibles
Pixels makeNoise(int channel) {
    Pixels p;
    p.w = p.h = 256;
    p.data.resize((size_t)p.w * p.h * 4);
    uint32_t x = 0x9E3779B9u ^ (uint32_t)(channel * 0x85EBCA6Bu);
    for (auto& b : p.data) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b = (uint8_t)(x >> 8);
    }
    for (size_t i = 3; i < p.data.size(); i += 4) p.data[i] = 255;
    return p;
}

// Grille : fond sombre, lignes tous les 32 pixels, axes plus clairs
Pixels makeGrid() {
    Pixels p;
    p.w = p.h = 256;
    p.data.resize((size_t)p.w * p.h * 4);
    for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w; x++) {
            const bool line = x % 32 == 0 || y % 32 == 0;
            const bool axis = x == 0 || y == 0;
            const uint8_t v = axis ? 230 : line ? 150 : 24;
            uint8_t* px = &p.data[((size_t)y * p.w + x) * 4];
            px[0] = px[1] = px[2] = v;
            px[3] = 255;
        }
    return p;
}

std::wstring extensionOf(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring e = path.substr(dot + 1);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

// Taille « LxH » lue dans la description du flux video donnee par ffmpeg
bool probeSize(const std::wstring& ffmpeg, const std::wstring& path, int& w, int& h) {
    std::string all;
    runProcess(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -i \"" + path + L"\"", [&](const std::wstring& l) { all += U8(l) + "\n"; });
    const size_t v = all.find("Video:");
    if (v == std::string::npos) return false;
    for (size_t i = v; i + 3 < all.size(); i++) {
        if (!isdigit((unsigned char)all[i]) || (i > 0 && (isdigit((unsigned char)all[i - 1]) || all[i - 1] == 'x'))) continue;
        int a = 0, b = 0, used = 0;
        if (sscanf(all.c_str() + i, "%dx%d%n", &a, &b, &used) == 2 && a > 0 && b > 0 && all[i - 1] == ' ') {
            const char next = all[i + used];
            if (next == ' ' || next == ',' || next == '[') {
                w = a;
                h = b;
                return true;
            }
        }
    }
    return false;
}

// Decode la premiere image par ffmpeg : RGBA 8 bits, ou RGBA flottant pour HDR / EXR
bool decodeImage(const std::wstring& ffmpeg, const std::wstring& path, Pixels& out, std::wstring& err) {
    int w = 0, h = 0;
    if (!fileExists(path)) {
        err = TXF("error.channel_missing", path.c_str());
        return false;
    }
    if (!probeSize(ffmpeg, path, w, h)) {
        err = TXF("error.channel_read", path.c_str());
        return false;
    }
    const std::wstring ext = extensionOf(path);
    const bool flt = ext == L"hdr" || ext == L"exr";
    const std::wstring fmt = flt ? L"gbrpf32le" : L"rgba";  // plans G, B, R pour le flottant : reassembles plus bas
    auto proc = platform::Process::start(
        platform::splitCommand(L"\"" + ffmpeg + L"\" -hide_banner -loglevel quiet -nostdin -i \"" + path +
                               L"\" -frames:v 1 -f rawvideo -pix_fmt " + fmt + L" -"),
        false, true);
    if (!proc) {
        err = TX("error.ffmpeg_launch");
        return false;
    }
    std::vector<uint8_t> raw;
    char buf[65536];
    size_t got;
    while ((got = proc->readOutput(buf, sizeof(buf))) > 0) raw.insert(raw.end(), (uint8_t*)buf, (uint8_t*)buf + got);
    proc->wait();
    const size_t px = (size_t)w * h;
    if (raw.size() < px * (flt ? 12 : 4)) {
        err = TXF("error.channel_read", path.c_str());
        return false;
    }
    out.w = w;
    out.h = h;
    out.isFloat = flt;
    if (!flt) {
        out.data.assign(raw.begin(), raw.begin() + px * 4);
    } else {
        out.data.resize(px * 16);
        const float* g = (const float*)raw.data();
        const float* b = g + px;
        const float* r = b + px;
        float* o = (float*)out.data.data();
        for (size_t i = 0; i < px; i++) {
            o[i * 4 + 0] = r[i];
            o[i * 4 + 1] = g[i];
            o[i * 4 + 2] = b[i];
            o[i * 4 + 3] = 1.0f;
        }
    }
    return true;
}

void flipRows(Pixels& p) {
    const size_t row = (size_t)p.w * (p.isFloat ? 16 : 4);
    std::vector<uint8_t> tmp(row);
    for (int y = 0; y < p.h / 2; y++) {
        uint8_t* a = &p.data[(size_t)y * row];
        uint8_t* b = &p.data[(size_t)(p.h - 1 - y) * row];
        memcpy(tmp.data(), a, row);
        memcpy(a, b, row);
        memcpy(b, tmp.data(), row);
    }
}

GLuint upload(const Pixels& p, const std::string& filter, const std::string& wrap) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(0x0CF5 /*GL_UNPACK_ALIGNMENT*/, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, p.isFloat ? ogl::K_RGBA32F : ogl::K_RGBA8, p.w, p.h, 0, GL_RGBA,
                 p.isFloat ? GL_FLOAT : GL_UNSIGNED_BYTE, p.data.data());
    const bool mip = filter.empty() || filter == "mipmap";
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter == "nearest" ? GL_NEAREST : mip ? K_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter == "nearest" ? GL_NEAREST : GL_LINEAR);
    const GLint mode = wrap.empty() || wrap == "repeat" ? K_REPEAT : K_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode);
    if (mip) ogl::GenerateMipmap(GL_TEXTURE_2D);
    return t;
}

}  // namespace

bool loadChannels(const Settings& s, const std::wstring& shaderDir, const std::wstring& ffmpeg, ChannelSet& out, const Hooks& H,
                  std::wstring& err, int bufW, int bufH) {
    for (int i = 0; i < 4; i++) {
        const std::wstring& spec = s.channel[i];
        if (spec.empty()) continue;
        Pixels p;
        if (spec.size() == 7 && (spec.compare(0, 6, L"buffer") == 0 || spec.compare(0, 6, L"Buffer") == 0) && towlower(spec[6]) >= L'a' &&
            towlower(spec[6]) <= L'd') {  // tampon d'une passe multiple : pas de texture a charger
            out.buffer[i] = (int)(towlower(spec[6]) - L'a');
            out.w[i] = bufW;
            out.h[i] = bufH;
            out.filter[i] = s.chFilter[i].empty() || s.chFilter[i] == "mipmap" ? "linear" : s.chFilter[i];
            out.wrap[i] = s.chWrap[i].empty() ? "clamp" : s.chWrap[i];
            continue;
        }
        if (spec == L"noise") {
            p = makeNoise(i);
        } else if (spec == L"grid") {
            p = makeGrid();
        } else {
            std::wstring path = spec;
            const bool absolute = path.size() > 1 && (path[1] == L':' || path[0] == L'/' || path[0] == L'\\');
            if (!absolute) path = shaderDir + path;
            if (!decodeImage(ffmpeg, path, p, err)) return false;
            if (s.chVflip[i]) flipRows(p);  // Shadertoy : le haut de l'image est en v = 1
            H.log(L"  " + TXF("log.channel_loaded", i, path.c_str(), p.w, p.h));
        }
        ogl::ActiveTexture(K_TEXTURE0);
        out.tex[i] = upload(p, s.chFilter[i], s.chWrap[i]);
        out.w[i] = p.w;
        out.h[i] = p.h;
    }
    return true;
}

void bindChannels(const ChannelSet& cs, GLuint prog, const GLuint* bufferTex) {
    float res[12] = {0};
    for (int i = 0; i < 4; i++) {
        ogl::ActiveTexture(K_TEXTURE0 + 1 + i);
        if (cs.buffer[i] >= 0) {
            glBindTexture(GL_TEXTURE_2D, bufferTex ? bufferTex[cs.buffer[i]] : 0);
            const GLint f = cs.filter[i] == "nearest" ? GL_NEAREST : GL_LINEAR;
            const GLint m = cs.wrap[i] == "repeat" ? K_REPEAT : K_CLAMP_TO_EDGE;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m);
        } else {
            glBindTexture(GL_TEXTURE_2D, cs.tex[i]);
        }
        ogl::Uniform1i(ogl::GetUniformLocation(prog, ("iChannel" + std::to_string(i)).c_str()), 1 + i);
        res[i * 3] = (float)cs.w[i];
        res[i * 3 + 1] = (float)cs.h[i];
        res[i * 3 + 2] = 1.0f;
    }
    ogl::ActiveTexture(K_TEXTURE0);
    ogl::Uniform3fv(ogl::GetUniformLocation(prog, "iChannelResolution"), 4, res);
    const float zeros[4] = {0, 0, 0, 0};
    ogl::Uniform1fv(ogl::GetUniformLocation(prog, "iChannelTime"), 4, zeros);
}

void freeChannels(ChannelSet& cs) {
    for (int i = 0; i < 4; i++)
        if (cs.tex[i]) {
            glDeleteTextures(1, &cs.tex[i]);
            cs.tex[i] = 0;
        }
}
