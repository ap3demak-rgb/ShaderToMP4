// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "passes.h"
#include "i18n.h"
#include "locate.h"
#include "util.h"

namespace {
const char* const kNames[4] = {"bufferA", "bufferB", "bufferC", "bufferD"};
const wchar_t* const kLabels[4] = {L"Buffer A", L"Buffer B", L"Buffer C", L"Buffer D"};  // nom-technique
}  // namespace

void BufferPasses::discover(const std::wstring& shaderPath, const std::function<bool(const std::wstring&, std::string&)>& read,
                            const Hooks& H) {
    const size_t dot = shaderPath.find_last_of(L'.');
    const size_t slash = shaderPath.find_last_of(L"\\/");
    const std::wstring stem = (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) ? shaderPath : shaderPath.substr(0, dot);
    std::vector<std::wstring> exts = {dot == std::wstring::npos ? L".glsl" : shaderPath.substr(dot), L".glsl", L".txt"};
    auto find = [&](const std::string& name, std::string& code, std::wstring& path) {
        for (const auto& e : exts) {
            const std::wstring p = stem + L"." + W(name) + e;
            if (fileExists(p) && read(p, code)) {
                path = p;
                return true;
            }
        }
        return false;
    };
    find("common", common_, commonPath_);
    if (common_.empty()) commonPath_.clear();
    for (int i = 0; i < 4; i++) {
        if (!find(kNames[i], bufs_[i].code, bufs_[i].path)) continue;
        used_[i] = true;
        parseLines(bufs_[i].st, bufs_[i].code, true);  // canaux propres a la passe
        H.log(L"  " + TXF("log.pass_found", kLabels[i]));
    }
    if (find("cubemapA", cube_.code, cube_.path)) {
        cubeUsed_ = true;
        parseLines(cube_.st, cube_.code, true);
        H.log(L"  " + TXF("log.pass_found", L"Cubemap A"));  // nom-technique
    }
    if (find("sound", sound_, soundPath_)) H.log(L"  " + TXF("log.pass_found", L"Sound"));  // nom-technique
    if (!common_.empty()) H.log(L"  " + TX("log.pass_common"));
}

std::string BufferPasses::withCommon(const std::string& code) const {
    if (common_.empty()) return code;
    return common_ + "\n#line 1\n" + code;
}

int BufferPasses::commonLines() const { return 1 + (int)std::count(common_.begin(), common_.end(), '\n'); }

std::string BufferPasses::allCode() const {
    std::string all = common_;
    for (int i = 0; i < 4; i++)
        if (used_[i]) all += "\n" + bufs_[i].code;
    if (cubeUsed_) all += "\n" + cube_.code;
    return all;
}

bool BufferPasses::build(int rw, int rh, const std::wstring& shaderDir, const std::wstring& ffmpeg, const Hooks& H, std::wstring& err,
                         double start, double fps) {
    if (cubeUsed_) {  // Cubemap A : six faces RGBA32F, deux textures (lecture de l'image precedente possible)
        std::wstring e;
        std::string types[4];
        channelTypes(cube_.st, types);
        cube_.prog = buildProgram(withCommon(cube_.code), e, false, types, true);
        if (!cube_.prog) {
            err = L"[Cubemap A] " + locateGlslErrors(e, cube_.path, 1 + (int)std::count(cube_.code.begin(), cube_.code.end(), '\n'), commonPath_);  // nom-technique
            return false;
        }
        if (!loadChannels(cube_.st, shaderDir, ffmpeg, cube_.chans, H, err, rw, rh, start, fps)) return false;
        glGenTextures(2, cube_.tex);
        ogl::GenFramebuffers(2, cube_.fbo);
        for (int k = 0; k < 2; k++) {
            glBindTexture(0x8513 /*GL_TEXTURE_CUBE_MAP*/, cube_.tex[k]);
            for (int f = 0; f < 6; f++)
                glTexImage2D(0x8515 + f, 0, ogl::K_RGBA32F, kCubeSize, kCubeSize, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(0x8513, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(0x8513, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(0x8513, GL_TEXTURE_WRAP_S, 0x812F);
            glTexParameteri(0x8513, GL_TEXTURE_WRAP_T, 0x812F);
            glTexParameteri(0x8513, 0x8072 /*WRAP_R*/, 0x812F);
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, cube_.fbo[k]);
            for (int f = 0; f < 6; f++) {
                ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, 0x8515 + f, cube_.tex[k], 0);
                if (ogl::CheckFramebufferStatus(ogl::K_FRAMEBUFFER) != ogl::K_FRAMEBUFFER_COMPLETE) {
                    err = TX("error.buffers_unavailable");
                    return false;
                }
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        if (!used_[i]) continue;
        Buf& b = bufs_[i];
        std::wstring e;
        std::string types[4];
        channelTypes(b.st, types);
        b.prog = buildProgram(withCommon(b.code), e, true, types);
        if (!b.prog) {
            err = std::wstring(L"[") + kLabels[i] + L"] " +
                  locateGlslErrors(e, b.path, 1 + (int)std::count(b.code.begin(), b.code.end(), '\n'), commonPath_);
            return false;
        }
        if (!loadChannels(b.st, shaderDir, ffmpeg, b.chans, H, err, rw, rh, start, fps)) return false;
        glGenTextures(2, b.tex);
        ogl::GenFramebuffers(2, b.fbo);
        for (int k = 0; k < 2; k++) {
            glBindTexture(GL_TEXTURE_2D, b.tex[k]);
            glTexImage2D(GL_TEXTURE_2D, 0, ogl::K_RGBA32F, rw, rh, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, b.fbo[k]);
            ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b.tex[k], 0);
            if (ogl::CheckFramebufferStatus(ogl::K_FRAMEBUFFER) != ogl::K_FRAMEBUFFER_COMPLETE) {
                err = TX("error.buffers_unavailable");
                return false;
            }
            glClearColor(0, 0, 0, 0);  // un tampon commence a zero, comme sur Shadertoy
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    return true;
}

void BufferPasses::latest(GLuint out[5]) const {
    for (int i = 0; i < 4; i++) out[i] = used_[i] ? bufs_[i].tex[bufs_[i].read] : 0;
    out[4] = cubeUsed_ ? cube_.tex[cube_.read] : 0;
}

void BufferPasses::step(long long n, double t, int rw, int rh, const std::function<void(GLuint prog, long long n)>& setUniforms) {
    glDisable(GL_BLEND);
    if (cubeUsed_) {  // Cubemap A d'abord : les tampons et l'image peuvent la lire
        const int w = 1 - cube_.read;
        glViewport(0, 0, kCubeSize, kCubeSize);
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, cube_.fbo[w]);
        ogl::UseProgram(cube_.prog);
        setUniforms(cube_.prog, n);
        ogl::Uniform3f(ogl::GetUniformLocation(cube_.prog, "iResolution"), (float)kCubeSize, (float)kCubeSize, 1.0f);
        GLuint src[5];
        latest(src);
        updateChannels(cube_.chans, t, n);
        bindChannels(cube_.chans, cube_.prog, src);
        for (int f = 0; f < 6; f++) {
            ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, 0x8515 + f, cube_.tex[w], 0);
            ogl::Uniform1i(ogl::GetUniformLocation(cube_.prog, "st_face"), f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        cube_.read = w;
    }
    glViewport(0, 0, rw, rh);
    for (int i = 0; i < 4; i++) {
        if (!used_[i]) continue;
        Buf& b = bufs_[i];
        const int w = 1 - b.read;  // on ecrit dans l'autre texture : la precedente reste lisible (par ce tampon aussi)
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, b.fbo[w]);
        ogl::UseProgram(b.prog);
        setUniforms(b.prog, n);
        GLuint src[5];
        latest(src);
        updateChannels(b.chans, t, n);
        bindChannels(b.chans, b.prog, src);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        b.read = w;
    }
}

void BufferPasses::reset() {
    if (cubeUsed_) {
        for (int k = 0; k < 2; k++) {
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, cube_.fbo[k]);
            for (int f = 0; f < 6; f++) {
                ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, 0x8515 + f, cube_.tex[k], 0);
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
            }
        }
        cube_.read = 0;
    }
    for (int i = 0; i < 4; i++) {
        if (!used_[i]) continue;
        Buf& b = bufs_[i];
        for (int k = 0; k < 2; k++) {
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, b.fbo[k]);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        b.read = 0;
    }
}

void BufferPasses::release() {
    freeChannels(cube_.chans);
    if (cube_.fbo[0]) ogl::DeleteFramebuffers(2, cube_.fbo);
    if (cube_.tex[0]) glDeleteTextures(2, cube_.tex);
    if (cube_.prog) ogl::DeleteProgram(cube_.prog);
    cube_.fbo[0] = cube_.fbo[1] = cube_.tex[0] = cube_.tex[1] = 0;
    cube_.prog = 0;
    for (int i = 0; i < 4; i++) {
        Buf& b = bufs_[i];
        freeChannels(b.chans);
        if (b.fbo[0]) ogl::DeleteFramebuffers(2, b.fbo);
        if (b.tex[0]) glDeleteTextures(2, b.tex);
        if (b.prog) ogl::DeleteProgram(b.prog);
        b.fbo[0] = b.fbo[1] = b.tex[0] = b.tex[1] = 0;
        b.prog = 0;
    }
}
