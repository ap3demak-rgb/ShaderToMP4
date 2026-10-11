// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "player.h"
#include "channels.h"
#include "compat.h"
#include "formats.h"
#include "gl.h"
#include "i18n.h"
#include "locate.h"
#include "passes.h"
#include "render.h"
#include "util.h"
#include "platform/platform.h"

struct PreviewPlayer::Impl {
    std::thread th;
    std::atomic<bool> stopFlag{false}, play{true}, alive{false};
    std::atomic<double> curT{0.0};
    std::mutex m;
    bool hasSeek = false;
    double seekT = 0.0;
};

namespace {

using clk = std::chrono::steady_clock;

// Tout ce que le thread garde d'un shader compile : programme, tampons de passes, textures, cible de rendu
struct Rig {
    GLuint prog = 0, vao = 0, tex = 0, fbo = 0;
    BufferPasses bp;
    ChannelSet chans;
    Settings s;
    int w = 0, h = 0;
    long long simFrame = 0;
    GLint uRes = -1, uTime = -1, uDelta = -1, uFrame = -1, uRate = -1, uMouse = -1, uDate = -1, uSR = -1, uWeight = -1;
    float mouse[4] = {0, 0, 0, 0};
    platform::DateTime date{};
    float dateSec = 0;
    void release() {
        bp.release();
        freeChannels(chans);
        if (fbo) ogl::DeleteFramebuffers(1, &fbo);
        if (tex) glDeleteTextures(1, &tex);
        if (prog) ogl::DeleteProgram(prog);
        fbo = tex = prog = 0;
        bp = BufferPasses();
        chans = ChannelSet();
        simFrame = 0;
    }
};

// Contenu des fichiers du shader (principal, passes, commun) : une empreinte qui change des qu'un fichier est modifie
std::string watchedContent(const std::wstring& shaderPath) {
    std::string all;
    std::string text;
    if (readFile(shaderPath, text)) all += text;
    const size_t dot = shaderPath.find_last_of(L'.');
    const std::wstring stem = dot == std::wstring::npos ? shaderPath : shaderPath.substr(0, dot);
    const std::wstring ext = dot == std::wstring::npos ? L".glsl" : shaderPath.substr(dot);
    for (const char* n : {"common", "bufferA", "bufferB", "bufferC", "bufferD"})
        if (readFile(stem + L"." + W(n) + ext, text)) all += n + text;
    return fnvHex(all);
}

bool buildRig(Rig& r, const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, std::wstring& err) {
    Hooks quiet;
    quiet.log = [](const std::wstring&) {};
    std::string code;
    if (!readFile(shaderPath, code)) {
        err = TX("error.read_file");
        return false;
    }
    normalizeShaderText(code, quiet);
    std::vector<std::wstring> notes;
    code = adaptShaderCode(code, notes);
    if (code.find("mainImage") == std::string::npos) {
        err = TX("error.no_main_image");
        return false;
    }
    Settings s = base;
    parseLines(s, code, true);
    s.width = std::max(16, s.width & ~1);
    s.height = std::max(16, s.height & ~1);
    if (s.fps <= 0) s.fps = 60;
    if (s.duration <= 0) s.duration = 10;
    // image reduite : 640 pixels au plus sur le grand cote
    const double k = std::min(1.0, 640.0 / std::max(s.width, s.height));
    r.w = std::max(16, (int)(s.width * k));
    r.h = std::max(16, (int)(s.height * k));
    r.s = s;

    r.bp.discover(shaderPath, [&](const std::wstring& p, std::string& out) {
        if (!readFile(p, out)) return false;
        normalizeShaderText(out, quiet);
        out = adaptShaderCode(out, notes);
        return true;
    }, quiet);
    r.prog = buildProgram(r.bp.withCommon(code), err);
    if (!r.prog) {
        err = locateGlslErrors(err, shaderPath, 1 + (int)std::count(code.begin(), code.end(), '\n'), r.bp.commonPath());
        return false;
    }
    const size_t cut = shaderPath.find_last_of(L"\\/");
    const std::wstring shaderDir = cut == std::wstring::npos ? L"" : shaderPath.substr(0, cut + 1);
    const std::wstring ffmpeg = findFfmpeg(exeDir);
    ogl::GenVertexArrays(1, &r.vao);
    ogl::BindVertexArray(r.vao);
    glGenTextures(1, &r.tex);
    glBindTexture(GL_TEXTURE_2D, r.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, ogl::K_RGBA8, r.w, r.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ogl::GenFramebuffers(1, &r.fbo);
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, r.fbo);
    ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.tex, 0);
    if (ogl::CheckFramebufferStatus(ogl::K_FRAMEBUFFER) != ogl::K_FRAMEBUFFER_COMPLETE) {
        err = TX("error.framebuffer");
        return false;
    }
    if (!r.bp.build(r.w, r.h, shaderDir, ffmpeg, quiet, err)) return false;
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, r.fbo);
    glViewport(0, 0, r.w, r.h);
    ogl::UseProgram(r.prog);
    if (!loadChannels(s, shaderDir, ffmpeg, r.chans, quiet, err, r.w, r.h)) return false;
    bindChannels(r.chans, r.prog);
    r.uRes = ogl::GetUniformLocation(r.prog, "iResolution");
    r.uTime = ogl::GetUniformLocation(r.prog, "iTime");
    r.uDelta = ogl::GetUniformLocation(r.prog, "iTimeDelta");
    r.uFrame = ogl::GetUniformLocation(r.prog, "iFrame");
    r.uRate = ogl::GetUniformLocation(r.prog, "iFrameRate");
    r.uMouse = ogl::GetUniformLocation(r.prog, "iMouse");
    r.uDate = ogl::GetUniformLocation(r.prog, "iDate");
    r.uSR = ogl::GetUniformLocation(r.prog, "iSampleRate");
    r.uWeight = ogl::GetUniformLocation(r.prog, "st_weight");
    r.mouse[0] = r.mouse[1] = r.mouse[2] = r.mouse[3] = 0;
    if (s.mouseSet) {
        const float mx = (float)(s.mouseX * r.w), my = (float)(s.mouseY * r.h);
        r.mouse[0] = mx;
        r.mouse[1] = my;
        r.mouse[2] = s.mouseDown ? mx : -mx;
        r.mouse[3] = s.mouseDown ? my : -my;
    }
    r.date = platform::localTime();
    if (!s.date.empty()) {
        int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
        std::string dt = s.date;
        for (auto& c : dt)
            if (c == 'T' || c == 't' || c == '_') c = ' ';
        if (sscanf(dt.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mi, &ss) >= 3) {
            r.date.year = y; r.date.month = mo; r.date.day = d; r.date.hour = hh; r.date.minute = mi; r.date.second = ss;
        }
    }
    r.dateSec = r.date.hour * 3600.f + r.date.minute * 60.f + r.date.second;
    return true;
}

// Variables communes d'un programme (image ou tampon) pour l'image n au temps t
void setUniforms(const Rig& r, GLuint p, long long n, double t) {
    ogl::Uniform3f(ogl::GetUniformLocation(p, "iResolution"), (float)r.w, (float)r.h, 1.0f);
    ogl::Uniform1f(ogl::GetUniformLocation(p, "iTime"), (float)t);
    ogl::Uniform1f(ogl::GetUniformLocation(p, "iTimeDelta"), (float)(1.0 / r.s.fps));
    ogl::Uniform1i(ogl::GetUniformLocation(p, "iFrame"), (GLint)n);
    ogl::Uniform1f(ogl::GetUniformLocation(p, "iFrameRate"), (float)r.s.fps);
    ogl::Uniform4f(ogl::GetUniformLocation(p, "iMouse"), r.mouse[0], r.mouse[1], r.mouse[2], r.mouse[3]);
    ogl::Uniform4f(ogl::GetUniformLocation(p, "iDate"), (float)r.date.year, (float)(r.date.month - 1), (float)r.date.day,
                   r.dateSec + (float)t);
    ogl::Uniform1f(ogl::GetUniformLocation(p, "iSampleRate"), 44100.0f);
}

// Rend l'image au temps t ; send : lire les pixels et les envoyer
void renderAt(Rig& r, double t, bool send, const PreviewPlayer::FrameFn& onFrame) {
    const long long idx = std::max(0LL, std::llround((t - r.s.start) * r.s.fps));
    if (r.bp.any()) {  // les tampons avancent image par image ; reculer repart de zero
        if (idx < r.simFrame) {
            r.bp.reset();
            r.simFrame = 0;
        }
        long long from = r.simFrame;
        if (idx - from > 600) from = idx - 600;  // rattrapage borne
        auto setter = [&](GLuint p, long long n) { setUniforms(r, p, n, r.s.start + (double)n / r.s.fps); };
        for (long long n = from; n < idx; n++) r.bp.step(n, r.w, r.h, setter);
        r.bp.step(idx, r.w, r.h, setter);
        r.simFrame = idx + 1;
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, r.fbo);
        glViewport(0, 0, r.w, r.h);
    }
    ogl::UseProgram(r.prog);
    if (r.chans.anyBuffer()) {
        GLuint latest[4];
        r.bp.latest(latest);
        bindChannels(r.chans, r.prog, latest);
    }
    setUniforms(r, r.prog, idx, t);
    ogl::Uniform1f(r.uWeight, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!send) return;
    std::vector<uint8_t> px((size_t)r.w * r.h * 4), out((size_t)r.w * r.h * 4);
    glReadPixels(0, 0, r.w, r.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    const size_t row = (size_t)r.w * 4;
    for (int y = 0; y < r.h; y++) {  // OpenGL est bas->haut ; RGBA -> BGRA
        const uint8_t* src = &px[(size_t)(r.h - 1 - y) * row];
        uint8_t* dst = &out[(size_t)y * row];
        for (int x = 0; x < r.w; x++) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    onFrame(r.w, r.h, std::move(out), t);
}

void playerMain(PreviewPlayer::Impl* I, std::wstring shaderPath, std::wstring exeDir, Settings base, double startTime,
                PreviewPlayer::FrameFn onFrame, std::function<bool()> ready, std::function<void(const std::wstring&)> log) {
    GLContext gl;
    std::wstring err;
    if (!createGLContext(gl, err)) {
        log(TXF("log.preview_error", err.c_str()));
        I->alive = false;
        return;
    }
    Rig rig;
    bool built = false;
    std::string watch = watchedContent(shaderPath);
    auto rebuild = [&] {
        rig.release();
        std::wstring e;
        built = buildRig(rig, shaderPath, exeDir, base, e);
        if (!built) {
            rig.release();
            log(TXF("log.preview_error", e.c_str()));
        }
    };
    rebuild();
    double t = startTime;
    if (built) t = std::min(std::max(startTime, rig.s.start), rig.s.start + rig.s.duration);
    I->curT = t;
    bool need = true;  // une image a rendre meme en pause
    auto last = clk::now(), lastWatch = last;
    while (!I->stopFlag) {
        const auto now = clk::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (now - lastWatch > std::chrono::milliseconds(500)) {  // rechargement automatique quand un fichier change
            lastWatch = now;
            const std::string cur = watchedContent(shaderPath);
            if (cur != watch) {
                watch = cur;
                log(TX("log.preview_reload"));
                rebuild();
                need = true;
            }
        }
        {
            std::lock_guard<std::mutex> l(I->m);
            if (I->hasSeek) {
                t = I->seekT;
                I->hasSeek = false;
                need = true;
            }
        }
        if (built) {
            t = std::min(std::max(t, rig.s.start), rig.s.start + rig.s.duration);
            if (I->play) {
                t += dt;
                if (t >= rig.s.start + rig.s.duration) t = rig.s.start;  // boucle sur la plage
                need = true;
            }
            I->curT = t;
            if (need) {
                renderAt(rig, t, ready(), onFrame);
                need = false;
            }
        }
        platform::sleepMs(I->play && built ? 8 : 30);
    }
    rig.release();
    destroyGLContext(gl);
    I->alive = false;
}

}  // namespace

PreviewPlayer::PreviewPlayer() = default;
PreviewPlayer::~PreviewPlayer() { stop(); }

void PreviewPlayer::start(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, double startTime, FrameFn onFrame,
                          std::function<bool()> ready, std::function<void(const std::wstring&)> log) {
    stop();
    impl_.reset(new Impl);
    impl_->alive = true;
    Impl* I = impl_.get();
    impl_->th = std::thread(playerMain, I, shaderPath, exeDir, s, startTime, std::move(onFrame), std::move(ready), std::move(log));
}

void PreviewPlayer::stop() {
    if (!impl_) return;
    impl_->stopFlag = true;
    if (impl_->th.joinable()) impl_->th.join();
    impl_.reset();
}

bool PreviewPlayer::running() const { return impl_ && impl_->alive.load(); }
void PreviewPlayer::setPlaying(bool p) {
    if (impl_) impl_->play = p;
}
bool PreviewPlayer::playing() const { return impl_ && impl_->play.load(); }
void PreviewPlayer::seek(double t) {
    if (!impl_) return;
    std::lock_guard<std::mutex> l(impl_->m);
    impl_->seekT = t;
    impl_->hasSeek = true;
}
double PreviewPlayer::time() const { return impl_ ? impl_->curT.load() : 0.0; }
