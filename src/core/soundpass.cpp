// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "soundpass.h"
#include "gl.h"
#include "i18n.h"
#include "locate.h"
#include "platform/platform.h"
#include "util.h"
#include <cmath>

namespace {
constexpr int kSide = 512;                     // un bloc = 512 x 512 echantillons, comme sur Shadertoy
constexpr long long kBlock = (long long)kSide * kSide;

void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xFF);
}
void put16(std::string& s, uint16_t v) {
    s += (char)(v & 0xFF);
    s += (char)(v >> 8);
}
}  // namespace

SoundResult renderSoundWav(const std::string& code, const std::wstring& path, int lines, const std::wstring& commonPath, double start,
                           double seconds, const std::wstring& wavPath, const Hooks& H, std::wstring& err) {
    std::wstring e;
    const GLuint prog = buildProgram(code, e, false, nullptr, false, true);
    if (!prog) {
        err = L"[Sound] " + locateGlslErrors(e, path, lines, commonPath);  // nom-technique
        return SoundResult::Failed;
    }
    const long long total = std::max(1LL, (long long)std::ceil(seconds * kSoundRate));
    const long long first = (long long)std::floor(start * kSoundRate);  // indice du premier echantillon

    GLuint vao = 0, tex = 0, fbo = 0;
    ogl::GenVertexArrays(1, &vao);
    ogl::BindVertexArray(vao);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x8058 /*GL_RGBA8*/, kSide, kSide, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ogl::GenFramebuffers(1, &fbo);
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);
    ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    auto cleanup = [&] {
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, 0);
        ogl::UseProgram(0);
        ogl::BindVertexArray(0);
        ogl::DeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        ogl::DeleteVertexArrays(1, &vao);
        ogl::DeleteProgram(prog);
    };
    if (ogl::CheckFramebufferStatus(ogl::K_FRAMEBUFFER) != ogl::K_FRAMEBUFFER_COMPLETE) {
        cleanup();
        err = TX("error.buffers_unavailable");
        return SoundResult::Failed;
    }
    ogl::BindBuffer(ogl::K_PIXEL_PACK_BUFFER, 0);  // une lecture asynchrone du rendu principal peut avoir laisse un PBO lie
    glDisable(GL_BLEND);
    glViewport(0, 0, kSide, kSide);
    ogl::UseProgram(prog);
    ogl::Uniform1f(ogl::GetUniformLocation(prog, "iSampleRate"), (float)kSoundRate);
    ogl::Uniform3f(ogl::GetUniformLocation(prog, "iResolution"), (float)kSide, (float)kSide, 1.0f);
    const GLint uBase = ogl::GetUniformLocation(prog, "st_sampleBase");

    std::vector<uint8_t> px((size_t)kBlock * 4);
    std::string data;
    data.reserve((size_t)total * 4);
    for (long long done = 0; done < total; done += kBlock) {
        if (H.cancelled()) {
            cleanup();
            return SoundResult::Cancelled;
        }
        ogl::Uniform1i(uBase, (GLint)(first + done));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, kSide, kSide, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        const long long n = std::min(kBlock, total - done);
        for (long long i = 0; i < n; i++) {
            const uint8_t* p = &px[(size_t)i * 4];  // ligne 0 en bas : l'indice y * 512 + x suit l'ordre de lecture
            put16(data, (uint16_t)((int)(p[0] + 256 * p[1]) - 32768));
            put16(data, (uint16_t)((int)(p[2] + 256 * p[3]) - 32768));
        }
    }
    cleanup();

    std::string wav = "RIFF";
    put32(wav, (uint32_t)(36 + data.size()));
    wav += "WAVEfmt ";
    put32(wav, 16);
    put16(wav, 1);  // PCM
    put16(wav, 2);  // stereo
    put32(wav, kSoundRate);
    put32(wav, kSoundRate * 4);
    put16(wav, 4);
    put16(wav, 16);
    wav += "data";
    put32(wav, (uint32_t)data.size());
    FILE* f = platform::openFile(wavPath, "wb");
    if (!f) {
        err = TXF("error.write_failed", wavPath.c_str());
        return SoundResult::Failed;
    }
    const bool ok = fwrite(wav.data(), 1, wav.size(), f) == wav.size() && fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    if (!ok) {
        err = TXF("error.write_failed", wavPath.c_str());
        return SoundResult::Failed;
    }
    return SoundResult::Ok;
}
