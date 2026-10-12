// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "player.h"
#include "channels.h"
#include "compat.h"
#include "dateinput.h"
#include "formats.h"
#include "gl.h"
#include "i18n.h"
#include "locate.h"
#include "params.h"
#include "postfx.h"
#include "vr.h"
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
    DateInput date;
    ParamSet params;           // parametres (// uniform ...)
    std::string paramCode;     // code dont les declarations sont lues (rechargement des valeurs sans recompiler)
    std::wstring paramFile;    // valeurs enregistrees par le panneau
    int vr = 0; // mainVR : 0 = non, 1 = 360 mono, 2 = 360 stereo haut / bas
    PostChain post;            // post-traitement (passe .post, effets, LUT)
    void release() {
        post.release();
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
    for (const char* n : {"common", "bufferA", "bufferB", "bufferC", "bufferD", "cubemapA", "post"})
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
    if (code.find("mainImage") == std::string::npos && code.find("mainVR") == std::string::npos) {
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
    r.simFrame = -(long long)s.warmup;  // chauffe : les images avant la premiere sont simulees aussi

    r.bp.discover(shaderPath, [&](const std::wstring& p, std::string& out) {
        if (!readFile(p, out)) return false;
        normalizeShaderText(out, quiet);
        out = adaptShaderCode(out, notes);
        return true;
    }, quiet);
    std::string chTypes[4];
    channelTypes(s, chTypes);
    if (!vrModeFor(code, s, r.vr, err)) return false;
    if (r.vr == 2) r.h &= ~1;  // stereo haut / bas : deux yeux de meme hauteur
    r.paramCode = code + "\n" + r.bp.allCode();
    r.paramFile = paramsFile(exeDir, shaderPath);
    if (!r.params.load(r.paramCode, s, r.paramFile, err)) return false;
    if (r.params.needsAudio() && !s.audio.empty()) {
        std::wstring audioErr;
        r.params.setAudio(findFfmpeg(exeDir), s.audio, audioErr);  // sans le son, le parametre garde sa valeur par defaut
    }
    r.bp.setPreamble(r.params.declarations());
    r.prog = buildProgram(r.bp.withCommon(code), err, false, chTypes, false, false, r.vr, r.params.declarations());
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
    if (!r.bp.build(r.w, r.h, shaderDir, ffmpeg, quiet, err, s.start, s.fps, &s)) return false;
    if (!r.post.build(s, r.bp, shaderDir, ffmpeg, quiet, err, r.w, r.h, r.params.declarations())) return false;
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, r.fbo);
    glViewport(0, 0, r.w, r.h);
    ogl::UseProgram(r.prog);
    if (!loadChannels(s, shaderDir, ffmpeg, r.chans, quiet, err, r.w, r.h, s.start, s.fps)) return false;
    r.bp.fixChannelSizes(r.chans);
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
    r.date = dateInputFrom(s.date);
    return true;
}

// Variables communes d'un programme (image ou tampon) pour l'image n au temps t
void setUniforms(const Rig& r, GLuint p, long long n, double t) {
    ogl::Uniform3f(ogl::GetUniformLocation(p, "iResolution"), (float)r.w, (float)(p == r.prog && r.vr == 2 ? r.h / 2 : r.h), 1.0f);
    ogl::Uniform1f(ogl::GetUniformLocation(p, "st_ipd"), (float)r.s.vrIpd);
    ogl::Uniform1f(ogl::GetUniformLocation(p, "iTime"), (float)t);
    ogl::Uniform1f(ogl::GetUniformLocation(p, "iTimeDelta"), (float)(1.0 / r.s.fps));
    ogl::Uniform1i(ogl::GetUniformLocation(p, "iFrame"), (GLint)(n + r.s.warmup));
    ogl::Uniform1f(ogl::GetUniformLocation(p, "iFrameRate"), (float)r.s.fps);
    float m[4];
    mouseAt(r.s.mouseKeys, t, 1.0 / r.s.fps, r.w, r.h, m);
    ogl::Uniform4f(ogl::GetUniformLocation(p, "iMouse"), m[0], m[1], m[2], m[3]);
    r.params.apply(p, t);
    float d[4];
    dateAt(r.date, t, d);
    ogl::Uniform4f(ogl::GetUniformLocation(p, "iDate"), d[0], d[1], d[2], d[3]);
    ogl::Uniform1f(ogl::GetUniformLocation(p, "iSampleRate"), 44100.0f);
}

// Rend l'image au temps t ; send : lire les pixels et les envoyer
void renderAt(Rig& r, double t, bool send, const PreviewPlayer::FrameFn& onFrame) {
    const long long idx = std::max(0LL, std::llround((t - r.s.start) * r.s.fps));
    if (r.bp.any()) {  // les tampons avancent image par image ; reculer repart de zero
        if (idx < r.simFrame) {
            r.bp.reset();
            r.simFrame = -(long long)r.s.warmup;
        }
        long long from = r.simFrame;
        if (idx - from > 600 + r.s.warmup) from = idx - 600 - r.s.warmup;  // rattrapage borne (chauffe comprise)
        auto setter = [&](GLuint p, long long n) { setUniforms(r, p, n, r.s.start + (double)n / r.s.fps); };
        for (long long n = from; n < idx; n++) r.bp.step(n, r.s.start + (double)n / r.s.fps, r.w, r.h, setter);
        r.bp.step(idx, r.s.start + (double)idx / r.s.fps, r.w, r.h, setter);
        r.simFrame = idx + 1;
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, r.fbo);
        glViewport(0, 0, r.w, r.h);
    }
    ogl::UseProgram(r.prog);
    updateChannels(r.chans, t, idx);
    setChannelTimes(r.chans, r.prog);
    if (r.chans.anyBuffer()) {
        GLuint latest[5];
        r.bp.latest(latest);
        bindChannels(r.chans, r.prog, latest);
    }
    const bool pp = r.post.active();
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, pp ? r.post.inputFbo() : r.fbo);
    setUniforms(r, r.prog, idx, t);
    ogl::Uniform1f(r.uWeight, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (pp) {
        GLuint latest[5] = {0, 0, 0, 0, 0};
        if (r.bp.any()) r.bp.latest(latest);
        r.post.apply(r.fbo, idx, t, 1.0f, [&](GLuint p, long long n) { setUniforms(r, p, n, r.s.start + (double)n / r.s.fps); }, latest);
        ogl::UseProgram(r.prog);
    }
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, r.fbo);
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
    std::string paramWatch;  // contenu du fichier des valeurs : un changement relit les valeurs sans recompiler
    {
        std::string v;
        readFile(paramsFile(exeDir, shaderPath), v);
        paramWatch = v;
    }
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
    auto last = clk::now(), lastWatch = last, lastParams = last;
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
        if (built && now - lastParams > std::chrono::milliseconds(100)) {  // valeurs du panneau « Parametres »
            lastParams = now;
            std::string v;
            readFile(rig.paramFile, v);
            if (v != paramWatch) {
                paramWatch = v;
                std::wstring e;
                rig.params.load(rig.paramCode, rig.s, rig.paramFile, e);
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
