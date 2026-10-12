// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render.h"
#include "channels.h"
#include "compat.h"
#include "encoder.h"
#include "locate.h"
#include "passes.h"
#include "formats.h"
#include "i18n.h"
#include "dateinput.h"
#include "gl.h"
#include "montage.h"
#include "overlay.h"
#include "params.h"
#include "postfx.h"
#include "previewconv.h"
#include "vr.h"
#include "process.h"
#include "soundpass.h"
#include "util.h"
#include "app_info.h"
#include "platform/platform.h"

// ---------------------------------------------------------------------------
//  Envoi des images a ffmpeg dans un thread dedie (rendu et encodage en parallele)
// ---------------------------------------------------------------------------
struct FrameQueue {
    platform::Process* proc = nullptr;
    size_t frameBytes = 0;
    std::vector<std::vector<uint8_t>> bufs;
    std::queue<int> freeQ, fullQ;
    std::mutex m;
    std::condition_variable cv;
    bool finished = false, failed = false;
};

static void writerThread(FrameQueue* q) {
    for (;;) {
        int idx;
        {
            std::unique_lock<std::mutex> l(q->m);
            q->cv.wait(l, [&] { return !q->fullQ.empty() || q->finished; });
            if (q->fullQ.empty()) return;
            idx = q->fullQ.front();
            q->fullQ.pop();
        }
        bool ok = q->proc->writeStdin(q->bufs[idx].data(), q->frameBytes);
        {
            std::lock_guard<std::mutex> l(q->m);
            if (!ok) q->failed = true;
            q->freeQ.push(idx);
        }
        q->cv.notify_all();
    }
}

// ---------------------------------------------------------------------------
//  Texte du shader : UTF-16 -> UTF-8, fins de ligne normalisees
// ---------------------------------------------------------------------------
void normalizeShaderText(std::string& code, const Hooks& H) {
    const size_t n = code.size();
    bool le = n >= 2 && (uint8_t)code[0] == 0xFF && (uint8_t)code[1] == 0xFE;
    bool be = n >= 2 && (uint8_t)code[0] == 0xFE && (uint8_t)code[1] == 0xFF;
    if (!le && !be && n >= 4) {  // UTF-16 sans BOM : un octet sur deux est nul (texte ASCII)
        size_t zeros = 0, sample = std::min<size_t>(n, 200);
        for (size_t i = 0; i < sample; i++)
            if (code[i] == 0) zeros++;
        if (zeros * 3 > sample) {
            le = code[1] == 0;
            be = !le;
        }
    }
    if (le || be) {
        std::wstring w;
        for (size_t i = (code.size() >= 2 && ((uint8_t)code[0] == 0xFF || (uint8_t)code[0] == 0xFE)) ? 2 : 0; i + 1 < n; i += 2) {
            uint8_t a = (uint8_t)code[i], b = (uint8_t)code[i + 1];
            w += (wchar_t)(le ? (a | (b << 8)) : ((a << 8) | b));
        }
        code = U8(w);
        H.log(L"  " + TX("log.utf16_converted"));
    }
    if (code.size() >= 3 && (uint8_t)code[0] == 0xEF && (uint8_t)code[1] == 0xBB && (uint8_t)code[2] == 0xBF)
        code.erase(0, 3);  // BOM UTF-8
    // \r\n et \r seuls (anciens Mac) -> \n
    std::string out;
    out.reserve(code.size());
    for (size_t i = 0; i < code.size(); i++) {
        if (code[i] == '\r') {
            out += '\n';
            if (i + 1 < code.size() && code[i + 1] == '\n') i++;
        } else {
            out += code[i];
        }
    }
    code.swap(out);
}

// ---------------------------------------------------------------------------
//  Conversion d'un fichier shader -> video / image / apercu
// ---------------------------------------------------------------------------
using Forced = std::vector<std::pair<std::string, std::string>>;  // reglages imposes a une variante

// Un rendu (une variante si `forced` est donne : ses reglages priment sur ceux du shader).
static Result convertOne(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H,
                         std::wstring& err, const Forced* forced) {
    if (isMontageFile(shaderPath)) {  // un montage n'est pas un shader : ni apercu ni image fixe
        err = TX("error.montage_preview");
        return Result::Failed;
    }
    std::string code;
    if (!readFile(shaderPath, code)) { err = TX("error.read_file"); return Result::Failed; }
    normalizeShaderText(code, H);
    std::vector<std::wstring> notes;  // adaptations et avertissements de compatibilite, journalises avant le rendu
    code = adaptShaderCode(code, notes);
    if (code.find("mainImage") == std::string::npos && code.find("mainVR") == std::string::npos) {
        err = TX("error.no_main_image");
        return Result::Failed;
    }

    Settings s = base;
    parseLines(s, code, true);
    if (forced)
        for (const auto& kv : *forced) applySetting(s, kv.first, kv.second);
    for (int i = 0; i < 4; i++)  // canal lu par le shader mais sans texture : il lit du noir
        if (s.channel[i].empty() && code.find("iChannel" + std::to_string(i)) != std::string::npos)
            H.log(L"  " + TXF("log.warn_ichannel", i, i));
    if (!forced && !sameSettings(s, base)) H.log(L"  " + TX("log.shader_overrides"));
    s.width = std::max(16, s.width & ~1);
    s.height = std::max(16, s.height & ~1);
    if (s.fps <= 0) s.fps = 60;
    if (s.duration <= 0) s.duration = 10;
    s.crf = std::min(51, std::max(0, s.crf));
    const bool preview = (bool)H.previewFrame;
    const bool sheet = !preview && !H.snapshot && H.sheetCols > 0 && H.sheetRows > 0;  // planche-contact : N images tirees sur la duree
    const bool thumb = !preview && H.snapshot && H.thumbTime != -1;                     // vignette : une image, 1280 pixels au plus
    if (thumb) s.start = H.thumbTime == -2 ? s.start + s.duration / 2 : H.thumbTime;
    const bool videoMode = !preview && !H.snapshot && !sheet;
    const OutKind kind = outKind(s.container);
    if (!videoMode || kind == OutKind::Gif || kind == OutKind::Webp) s.depth = 8;  // 10 bits : video et sequences seulement
    if (!videoMode) s.hdr = "off";  // l'apercu et l'image fixe restent en SDR
    if (videoMode && s.alpha && !alphaSupported(s.codec, s.container)) {  // transparence : refusee avant le rendu
        err = TXF("error.alpha_unsupported", codecLabel(s.codec), W(s.container).c_str());
        return Result::Failed;
    }
    if (videoMode && s.hdr != "off") {
        if (!hdrSupported(s.codec, s.container)) {
            err = TXF("error.hdr_unsupported", codecLabel(s.codec), W(s.container).c_str());
            return Result::Failed;
        }
        if (s.alpha) {
            err = TX("error.hdr_alpha");
            return Result::Failed;
        }
        if (s.depth < 10) {  // le HDR demande 10 bits
            s.depth = 10;
            H.log(L"  " + TX("log.hdr_depth"));
        }
    }
    if (videoMode && s.interp > 0) {
        if (kind != OutKind::Video) {
            H.log(L"  " + TX("log.interp_video_only"));
            s.interp = 0;
        } else if (s.alpha) {  // minterpolate ne traite pas l'alpha
            err = TX("error.interp_alpha");
            return Result::Failed;
        } else if (s.depth >= 10) {
            H.log(L"  " + TX("log.interp_8bit"));
        }
    }
    const bool deep = s.depth >= 10;
    const size_t bpc = deep ? 2 : 1;  // octets par composante lue sur le GPU
    const int B = std::min(64, std::max(1, s.blur));  // sous-images par image (flou de mouvement)
    const int D = std::min(8, std::max(2, s.pbo));    // profondeur de la file de lecture GPU

    // Duree : multiple entier de la periode si "loop" est donne (boucle parfaite : la derniere image
    // s'arrete juste avant le retour au point de depart)
    double duration = s.duration;
    if (s.loop > 0) duration = s.loop * std::max(1.0, std::floor(s.duration / s.loop + 0.5));
    const long long N = videoMode ? std::max(1LL, std::llround(duration * s.fps)) : sheet ? (long long)H.sheetCols * H.sheetRows : 1;
    const double totalSec = N / s.fps;
    const int W_ = s.width, H_ = s.height;
    int OW = W_, OH = H_;  // taille de sortie : celle du rendu, ou celle de la mise a l'echelle (scale=)
    if (videoMode && !outputSize(s, OW, OH)) {
        err = TXF("error.scale_invalid", W(s.scale).c_str());
        return Result::Failed;
    }
    const int SS = preview ? 1 : std::min(4, std::max(1, s.ss));  // supersampling : reduit par ffmpeg
    const int RW = W_ * SS, RH = H_ * SS;
    const size_t frameBytes = (size_t)RW * RH * 4 * bpc;
    bool hasAudio = videoMode && kind == OutKind::Video && !s.audio.empty();
    if (videoMode && kind != OutKind::Video && !s.audio.empty()) H.log(L"  " + TX("log.audio_ignored"));
    if (hasAudio && !fileExists(s.audio)) { err = TXF("error.audio_missing", s.audio.c_str()); return Result::Failed; }

    // Chemin de sortie : dossier choisi (sinon celui du shader), meme nom, extension du conteneur (ou .png)
    if (videoMode && !codecContainerOk(s.codec, s.container)) {
        err = TXF("error.codec_container", codecLabel(s.codec), W(s.container).c_str());
        return Result::Failed;
    }
    if (videoMode && kind == OutKind::Video && isDnxhr(s.codec) && (OW < 256 || OH < 120)) {
        err = TX("error.dnxhr_size");
        return Result::Failed;
    }
    if (!preview && !s.outdir.empty()) platform::makeDir(s.outdir);  // sans effet si deja present
    const std::wstring ext = thumb ? L"_thumb.png" : H.snapshot ? L".png" : sheet ? L"_sheet.png" : L"." + W(s.container);
    bool skipExisting = false;
    const std::wstring out = !videoMode ? outputPath(shaderPath, s.outdir, ext) : outputFile(shaderPath, s, true, &skipExisting);
    if (skipExisting) {
        H.log(L"  " + TXF("log.skip_existing", out.c_str()));
        return Result::Ok;
    }
    const bool sequence = videoMode && kind == OutKind::Sequence;
    const std::wstring seqDir = sequence ? sequenceDir(out) : L"";
    const std::wstring seqPattern = sequence ? sequencePattern(out) : L"";
    const std::wstring& outRef = sequence ? seqDir : out;
    const std::wstring outDir = outRef.substr(0, outRef.find_last_of(L"\\/"));

    // --- Verifications avant de commencer : espace disque ------------------------
    if (videoMode) {
        const double est = estimateSizeBytes(s, totalSec, hasAudio);
        uint64_t freeB = 0;
        if (platform::freeSpace(outDir, freeB)) {
            H.log(L"  " + TXF("log.size_estimate", est / 1048576.0, freeB / 1048576.0));
            const bool chunkedGuess = s.chunk > 0 && N > std::llround(s.chunk * s.fps);
            const double need = est * (chunkedGuess ? 2.2 : 1.2) + 50e6;  // les parties s'ajoutent au fichier final
            if ((double)freeB < need) {
                err = TXF("error.disk_full", outDir.c_str(), need / 1048576.0, freeB / 1048576.0);
                return Result::Failed;
            }
        }
        H.log(TXF("log.render_summary", OW, OH, s.interp > 0 ? s.interp : s.fps, totalSec, N,
                   s.bitrate > 0 ? wfmt(L"%d kb/s", s.bitrate).c_str() : wfmt(L"crf %d", s.crf).c_str(),
                   W(s.preset).c_str(), kind == OutKind::Video ? codecLabel(s.codec) : containerAt(containerIndex(s.container)).label));
        if (deep) H.log(L"  " + TX("log.depth10"));
        if (kind == OutKind::Gif && s.fps > 30) H.log(L"  " + TXF("log.gif_fps", s.fps));
        if (kind == OutKind::Gif && (double)OW * OH * N * 3 > 2.0e9) {
            err = TXF("error.gif_too_big", OW, OH, (long long)N);
            return Result::Failed;
        }
        if (OW != W_ || OH != H_) H.log(L"  " + TXF("log.scale_output", W_, H_, OW, OH, W(s.scaleFilter).c_str()));
        if (s.interp > 0) H.log(L"  " + TXF("log.interp", s.fps, s.interp));
        if (s.alpha) H.log(L"  " + TX("log.alpha_on"));
        if (s.hdr != "off") H.log(L"  " + TXF("log.hdr_on", s.hdr == "pq" ? L"HDR10 (PQ, BT.2020)" : L"HLG (BT.2020)", s.hdrWhite));
        if (s.loudnorm != 0) H.log(L"  " + TXF("log.loudnorm", s.loudnorm));
        if (B > 1) H.log(L"  " + TXF("log.motion_blur", B, s.shutter));
        if (s.loop > 0) H.log(L"  " + TXF("log.perfect_loop", s.loop, duration));
    }

    // --- Shader + FBO + PBO ---------------------------------------------------
    int vrMode = 0;  // mainVR : 360 equirectangulaire, mono (1) ou stereo haut / bas (2)
    if (!vrModeFor(code, s, vrMode, err)) return Result::Failed;
    if (vrMode > 0 && !preview) {
        H.log(L"  " + TX(vrMode == 2 ? "log.vr_stereo" : "log.compat_vr"));
        if (!vrAspectOk(W_, H_, vrMode)) H.log(L"  " + TXF("log.vr_aspect", W_, H_, vrMode == 2 ? L"1:1" : L"2:1"));
    }
    BufferPasses bp;  // Buffer A-D et Common : fichiers a cote du shader
    PostChain post;   // post-traitement en virgule flottante (passe .post.glsl, effets, LUT)
    bp.discover(shaderPath, [&](const std::wstring& p, std::string& out) {
        if (!readFile(p, out)) return false;
        normalizeShaderText(out, H);
        out = adaptShaderCode(out, notes);
        return true;
    }, H);
    if (!preview) {
        checkCompatibility(code + "\n" + bp.allCode(), s, notes);
        bp.report(s, code, notes);
        for (const auto& n : notes) H.log(L"  " + n);
    }
    // Parametres (// uniform ...) : declarations injectees dans tous les programmes, valeurs reglees a chaque image
    ParamSet params;
    {
        std::vector<std::wstring> pnotes;
        if (!params.load(code + "\n" + bp.allCode(), s, paramsFile(exeDir, shaderPath), err, &pnotes)) return Result::Failed;
        if (!preview)
            for (const auto& n : pnotes) H.log(L"  " + n);
        if (params.needsAudio()) {
            if (s.audio.empty()) {
                if (!preview) H.log(L"  " + TX("log.param_audio_missing"));
            } else if (!params.setAudio(findFfmpeg(exeDir), s.audio, err)) {
                return Result::Failed;
            }
        }
        bp.setPreamble(params.declarations());
    }
    std::string chTypes[4];
    channelTypes(s, chTypes);
    GLuint prog = buildProgram(bp.withCommon(code), err, false, chTypes, false, false, vrMode, params.declarations(), s.alpha);
    if (!prog) {  // erreurs « fichier:ligne: message », cliquables dans le journal
        err = locateGlslErrors(err, shaderPath, 1 + (int)std::count(code.begin(), code.end(), '\n'), bp.commonPath());
        return Result::Failed;
    }

    ChannelSet chans;
    GLuint vao = 0, tex = 0, fbo = 0, accTex = 0, accFbo = 0, present = 0;
    GLuint pbo[8] = {0};
    auto cleanupGL = [&] {
        ogl::BindBuffer(ogl::K_PIXEL_PACK_BUFFER, 0);
        if (pbo[0]) ogl::DeleteBuffers(D, pbo);
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, 0);
        if (fbo) ogl::DeleteFramebuffers(1, &fbo);
        if (accFbo) ogl::DeleteFramebuffers(1, &accFbo);
        if (tex) glDeleteTextures(1, &tex);
        if (accTex) glDeleteTextures(1, &accTex);
        ogl::UseProgram(0);
        freeChannels(chans);
        post.release();
        bp.release();
        if (present) ogl::DeleteProgram(present);
        ogl::DeleteProgram(prog);
    };

    GLint maxTex = 0;  // limite du GPU
    glGetIntegerv(0x0D33 /*GL_MAX_TEXTURE_SIZE*/, &maxTex);
    if (maxTex > 0 && (RW > maxTex || RH > maxTex)) {
        err = TXF("error.render_size", RW, RH, (int)maxTex);
        cleanupGL();
        return Result::Failed;
    }

    ogl::GenVertexArrays(1, &vao);
    ogl::BindVertexArray(vao);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, deep ? ogl::K_RGBA16 : ogl::K_RGBA8, RW, RH, 0, GL_RGBA, deep ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ogl::GenFramebuffers(1, &fbo);
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);
    ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (ogl::CheckFramebufferStatus(ogl::K_FRAMEBUFFER) != ogl::K_FRAMEBUFFER_COMPLETE) {
        err = TX("error.framebuffer");
        cleanupGL();
        return Result::Failed;
    }

    if (B > 1) {  // accumulation flottante pour le flou de mouvement
        present = buildPresentProgram(err, s.alpha);
        if (!present) { cleanupGL(); return Result::Failed; }
        glGenTextures(1, &accTex);
        glBindTexture(GL_TEXTURE_2D, accTex);
        glTexImage2D(GL_TEXTURE_2D, 0, ogl::K_RGBA32F, RW, RH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ogl::GenFramebuffers(1, &accFbo);
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, accFbo);
        ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accTex, 0);
        if (ogl::CheckFramebufferStatus(ogl::K_FRAMEBUFFER) != ogl::K_FRAMEBUFFER_COMPLETE) {
            err = TX("error.blur_unavailable");
            cleanupGL();
            return Result::Failed;
        }
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);
    }
    const std::wstring shaderDir = shaderPath.find_last_of(L"\\/") == std::wstring::npos ? L"" : shaderPath.substr(0, shaderPath.find_last_of(L"\\/") + 1);
    if (!bp.build(RW, RH, shaderDir, findFfmpeg(exeDir), H, err, s.start, s.fps, &s)) {
        cleanupGL();
        return Result::Failed;
    }
    if (!post.build(s, bp, shaderDir, findFfmpeg(exeDir), H, err, RW, RH, params.declarations())) {
        cleanupGL();
        return Result::Failed;
    }
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);  // la creation des tampons a change la cible
    glViewport(0, 0, RW, RH);

    ogl::UseProgram(prog);
    GLint uRes = ogl::GetUniformLocation(prog, "iResolution");
    GLint uTime = ogl::GetUniformLocation(prog, "iTime");
    GLint uDelta = ogl::GetUniformLocation(prog, "iTimeDelta");
    GLint uFrame = ogl::GetUniformLocation(prog, "iFrame");
    GLint uRate = ogl::GetUniformLocation(prog, "iFrameRate");
    GLint uMouse = ogl::GetUniformLocation(prog, "iMouse");
    GLint uDate = ogl::GetUniformLocation(prog, "iDate");
    GLint uSR = ogl::GetUniformLocation(prog, "iSampleRate");
    GLint uWeight = ogl::GetUniformLocation(prog, "st_weight");
    ogl::Uniform3f(uRes, (float)RW, (float)(vrMode == 2 ? RH / 2 : RH), 1.0f);  // stereo : iResolution = taille d'un oeil
    ogl::Uniform1f(ogl::GetUniformLocation(prog, "st_ipd"), (float)s.vrIpd);
    ogl::Uniform1f(uDelta, (float)(1.0 / s.fps));
    ogl::Uniform1f(uRate, (float)s.fps);
    // iMouse simule : valeur en pixels du rendu au temps t (voir mouse.h)
    auto setMouse = [&](GLint loc, double t) {
        float m[4];
        mouseAt(s.mouseKeys, t, 1.0 / s.fps, RW, RH, m);
        ogl::Uniform4f(loc, m[0], m[1], m[2], m[3]);
    };
    setMouse(uMouse, s.start);
    if (!loadChannels(s, shaderDir, findFfmpeg(exeDir), chans, H, err, RW, RH, s.start, s.fps)) {  // canaux d'entree (textures, tampons)
        cleanupGL();
        return Result::Failed;
    }
    bp.fixChannelSizes(chans);
    bindChannels(chans, prog);
    ogl::Uniform1f(uSR, 44100.0f);
    ogl::Uniform1f(uWeight, 1.0f);
    const DateInput dateIn = dateInputFrom(s.date);  // date fixe ou reelle au depart, qui avance avec iTime
    auto setDate = [&](GLint loc, double t) {
        float d[4];
        dateAt(dateIn, t, d);
        ogl::Uniform4f(loc, d[0], d[1], d[2], d[3]);
    };

    // Passes de tampon : memes variables que l'image, renseignees pour chaque programme
    auto setPassUniforms = [&](GLuint p, long long n) {
        const double t = s.start + (double)n / s.fps;
        ogl::Uniform3f(ogl::GetUniformLocation(p, "iResolution"), (float)RW, (float)RH, 1.0f);
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iTime"), (float)t);
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iTimeDelta"), (float)(1.0 / s.fps));
        ogl::Uniform1i(ogl::GetUniformLocation(p, "iFrame"), (GLint)(n + s.warmup));
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iFrameRate"), (float)s.fps);
        setMouse(ogl::GetUniformLocation(p, "iMouse"), t);
        setDate(ogl::GetUniformLocation(p, "iDate"), t);
        params.apply(p, t);
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iSampleRate"), 44100.0f);
    };
    // Les tampons avancent image par image : si le rendu demarre plus loin (reprise, apercu), on rejoue les images
    // precedentes sans rien encoder
    // Chauffe (warmup) : les N images avant la premiere sont simulees aussi, et iFrame est decale de N
    long long simFrame = -(long long)s.warmup;
    bool replayLogged = false;
    auto advanceBuffers = [&](long long n) {
        if (!bp.any()) return;
        for (; simFrame < n && !H.cancelled(); simFrame++) {
            if (!replayLogged) {
                H.log(L"  " + TXF("log.buffers_replay", (long long)(n + s.warmup)));
                replayLogged = true;
            }
            bp.step(simFrame, s.start + (double)simFrame / s.fps, RW, RH, setPassUniforms);
        }
        bp.step(n, s.start + (double)n / s.fps, RW, RH, setPassUniforms);
        simFrame = n + 1;
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);  // retour a l'image
        ogl::UseProgram(prog);
    };

    auto drawAt = [&](double t, long long n) {
        ogl::Uniform1f(uTime, (float)t);
        setMouse(uMouse, t);
        ogl::Uniform1i(uFrame, (GLint)(n + s.warmup));
        setDate(uDate, t);
        params.apply(prog, t);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    // Rend l'image n (temps t, facteur de fondu "fade") dans fbo ;
    // avec flou : moyenne de B sous-images etalees sur l'obturateur
    auto renderFrame = [&](long long n, double t, float fade) {
        updateChannels(chans, t, n);  // audio, video et clavier au temps exact de l'image
        setChannelTimes(chans, prog);
        if (chans.anyBuffer()) {  // le tampon le plus recent change a chaque image
            GLuint latest[5];
            bp.latest(latest);
            bindChannels(chans, prog, latest);
        }
        // avec post-traitement, l'image va dans une texture flottante (fondu applique a la fin de la chaine)
        const bool pp = post.active();
        const GLuint imgFbo = pp ? post.inputFbo() : fbo;
        const float weight = pp ? 1.0f : fade;
        if (B <= 1) {
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, imgFbo);
            ogl::Uniform1f(uWeight, weight);
            drawAt(t, n);
        } else {
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, accFbo);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            ogl::Uniform1f(uWeight, weight / B);
            ogl::Uniform1i(ogl::GetUniformLocation(prog, "st_premul"), s.alpha ? 1 : 0);  // alpha : somme de couleurs prémultipliées
            for (int k = 0; k < B; k++) drawAt(t + (double)k / B * s.shutter / s.fps, n);
            glDisable(GL_BLEND);
            ogl::Uniform1i(ogl::GetUniformLocation(prog, "st_premul"), 0);
            ogl::Uniform1f(uWeight, 1.0f);
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, imgFbo);
            ogl::UseProgram(present);
            glBindTexture(GL_TEXTURE_2D, accTex);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            ogl::UseProgram(prog);
        }
        if (pp) {
            GLuint latest[5] = {0, 0, 0, 0, 0};
            if (bp.any()) bp.latest(latest);
            post.apply(fbo, n, t, fade, setPassUniforms, latest);
            ogl::UseProgram(prog);
        }
    };

    // --- Apercu : une image renvoyee a l'appelant, sans ffmpeg -------------------
    if (preview) {
        long long pf = 0;
        double pt = H.previewTime;
        if (bp.any()) {  // les tampons dependent de l'historique : on les simule jusqu'a l'image voulue (600 images au plus)
            pf = std::min(600LL, std::max(0LL, std::llround((H.previewTime - s.start) * s.fps)));
            pt = s.start + (double)pf / s.fps;
            advanceBuffers(pf);
        }
        renderFrame(pf, pt, 1.0f);
        std::vector<uint8_t> px(frameBytes);
        glReadPixels(0, 0, RW, RH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        std::vector<uint8_t> flipped = previewBgra(px.data(), RW, RH, s.alpha);  // haut en bas, BGRA ; transparence sur un damier
        cleanupGL();
        H.previewFrame(RW, RH, std::move(flipped));
        return Result::Ok;
    }

    ogl::GenBuffers(D, pbo);
    for (int i = 0; i < D; i++) {
        ogl::BindBuffer(ogl::K_PIXEL_PACK_BUFFER, pbo[i]);
        ogl::BufferData(ogl::K_PIXEL_PACK_BUFFER, (ptrdiff_t)frameBytes, nullptr, ogl::K_STREAM_READ);
    }

    // --- ffmpeg (ffmpeg.exe a cote du programme) ------------------------------
    const std::wstring ffmpeg = findFfmpeg(exeDir);
    if (videoMode && !formatAvailable(ffmpeg, s.codec, s.container)) {
        err = TXF("error.encoder_missing", W(requiredEncoders(s.codec, s.container).front()).c_str());
        cleanupGL();
        return Result::Failed;
    }
    if (videoMode) {  // texte incruste : drawtext et une police doivent exister (verifie avant le premier rendu)
        std::wstring unused;
        if (!overlayFilter(s, 0, OW, OH, ffmpeg, unused, err)) {
            cleanupGL();
            return Result::Failed;
        }
    }
    const std::string enc = videoMode && kind == OutKind::Video ? resolveEncoder(s, ffmpeg, H) : "cpu";

    const std::wstring fpsStr = wfmt(L"%g", s.fps);
    // reduction du supersampling (area), puis mise a l'echelle de sortie (scale= : filtre au choix) ; les deux finissent par une virgule
    const std::wstring ssFilter = (SS > 1 ? L"scale=" + std::to_wstring(W_) + L":" + std::to_wstring(H_) + L":flags=area," : L"") +
                                  (videoMode ? outScaleFilter(s, W_, H_, OW, OH) : L"");
    const std::wstring head = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -y" +
        std::wstring(deep ? L" -f rawvideo -pix_fmt rgba64le -video_size " : L" -f rawvideo -pix_fmt rgba -video_size ") + std::to_wstring(RW) + L"x" + std::to_wstring(RH) +
        L" -framerate " + fpsStr + L" -i -";
    const ColorSpec cs = colorSpecFor(s);
    const double fi = videoMode ? std::max(0.0, std::min(s.fadein, totalSec)) : 0.0;
    const double fo = videoMode ? std::max(0.0, std::min(s.fadeout, totalSec)) : 0.0;
    // Fondus d'image : appliques dans le shader (facteur par image), donc exacts meme par parties
    auto fadeAt = [&](long long n) -> float {
        double f = 1.0;
        if (fi > 0) f = std::min(f, n / (fi * s.fps));
        if (fo > 0) f = std::min(f, (N - 1 - n) / (fo * s.fps));
        return (float)std::max(0.0, std::min(1.0, f));
    };
    // Passe Sound (<nom>.sound.glsl) : rendue en WAV avant la video, puis ajoutee comme piste audio ; le reglage « audio » est prioritaire
    std::wstring audioFile = s.audio;
    bool audioSeek = true;  // -ss : seulement pour un fichier audio complet (le WAV produit commence deja a « start »)
    struct WavGuard {
        std::wstring path;
        ~WavGuard() {
            if (!path.empty()) platform::removeFile(path);
        }
    } wavGuard;
    if (bp.hasSound() && videoMode && kind == OutKind::Video) {
        if (!s.audio.empty()) {
            H.log(L"  " + TX("log.sound_audio_wins"));
        } else {
            H.log(L"  " + TX("log.sound_rendering"));
            wavGuard.path = out + L".sound.wav";
            const SoundResult sr = renderSoundWav(bp.withCommon(bp.soundCode()), bp.soundPath(), 1 + (int)std::count(bp.soundCode().begin(), bp.soundCode().end(), '\n'),
                                                  bp.commonPath(), s.start, totalSec, wavGuard.path, H, err);
            if (sr != SoundResult::Ok) {
                wavGuard.path.clear();
                cleanupGL();
                return sr == SoundResult::Cancelled ? Result::Cancelled : Result::Failed;
            }
            ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);  // l'etat du rendu principal, modifie par la passe Sound
            glViewport(0, 0, RW, RH);
            ogl::UseProgram(prog);
            ogl::BindVertexArray(vao);
            audioFile = wavGuard.path;
            audioSeek = false;
            hasAudio = true;
        }
    }
    std::wstring audioFilter = loudnormFilter(s);  // normalisation du son (LUFS), puis fondus
    if (fi > 0) audioFilter += wfmt(L"afade=t=in:st=0:d=%g,", fi);
    if (fo > 0) audioFilter += wfmt(L"afade=t=out:st=%g:d=%g,", totalSec - fo, fo);
    audioFilter += L"apad";  // son plus court que la video : complete par du silence
    const std::wstring audioArgs = (audioSeek ? wfmt(L" -ss %g", s.start) : std::wstring()) + L" -i \"" + audioFile + L"\"";

    // Metadonnees du conteneur : titre = nom du shader, commentaire = taille, cadence et version
    std::wstring metaArgs;
    if (s.metadata && videoMode && kind == OutKind::Video) {
        std::wstring title = shaderPath.substr(shaderPath.find_last_of(L"\\/") + 1);
        if (size_t dot = title.find_last_of(L'.'); dot != std::wstring::npos && dot > 0) title.erase(dot);
        title.erase(std::remove(title.begin(), title.end(), L'"'), title.end());
        metaArgs = L" -metadata \"title=" + title + L"\" -metadata \"comment=" + wfmt(L"%dx%d, %g fps, ", OW, OH, s.interp > 0 ? s.interp : s.fps) +
                   APP_NAME_WSTR + L" " + APP_VER_WSTR + L"\"";
    }

    // Tout ce qui est regle pour une image depuis la boucle de rendu
    using clk = std::chrono::steady_clock;
    double waitSec = 0, loopSec = 0;  // mesure : temps bloque sur ffmpeg / temps total de rendu

    // Rend les images [n0, n1) vers ffmpeg -> dest (muxAudio : son inclus). doneBefore : images deja faites (progression).
    auto encodeRange = [&](long long n0, long long n1, const std::wstring& dest, bool muxAudio, long long doneBefore,
                           std::wstring& e) -> Result {
        std::wstring cmd;
        if (sheet) {  // planche-contact : les N images sont assemblees en grille par le filtre tile (vignettes de 1920 pixels de large au total au plus)
            const int tw = std::max(160, std::min(W_, 1920 / H.sheetCols)) & ~1;
            cmd = head + L" -vf \"vflip," + ssFilter + wfmt(L"scale=%d:-2:flags=area,tile=%dx%d:padding=4:margin=4:color=0x202020,format=rgb24\"", tw, H.sheetCols, H.sheetRows) +
                  L" -frames:v 1 -pix_fmt rgb24 \"" + dest + L"\"";
        } else if (H.snapshot) {  // image RGB pleine plage, sans conversion YUV
            const std::wstring thumbScale = thumb ? L"scale='min(1280,iw)':-2:flags=area," : L"";
            cmd = head + L" -vf \"vflip," + ssFilter + thumbScale + (s.alpha ? L"format=rgba\" -frames:v 1 -pix_fmt rgba \"" : L"format=rgb24\" -frames:v 1 -pix_fmt rgb24 \"") + dest + L"\"";
        } else if (kind != OutKind::Video) {  // image animee ou sequence : RGB pleine plage, pas de YUV
            std::wstring text;  // texte incruste (le temps de la video : une image par 1/fps)
            std::wstring oe;
            overlayFilter(s, (double)n0 / s.fps, OW, OH, ffmpeg, text, oe);
            cmd = head + L" -vf \"vflip," + ssFilter + text + imageFilterTail(s) + L"\"" + imageEncoderArgs(s) + L" \"" + dest + L"\"";
        } else {
            std::wstring text, oe;
            overlayFilter(s, (double)n0 / s.fps, OW, OH, ffmpeg, text, oe);  // la partie commence a n0 / fps dans la video
            cmd = head;
            if (muxAudio) cmd += audioArgs;
            cmd += L" -vf \"vflip," + ssFilter + text + L"scale=in_range=full:out_range=" + cs.range +
                   L":out_color_matrix=" + cs.matrix + L":flags=accurate_rnd+full_chroma_int," + interpFilter(s) + outFormat(s, enc) + L"\"";
            if (muxAudio) cmd += L" -map 0:v -map 1:a" + audioCodecArgs(s.container) + L" -af \"" + audioFilter + L"\" -shortest";
            cmd += encoderArgs(s, enc) + outPixFmtArg(s, enc) + L" " + cs.tags + containerArgs(s.container) + metaArgs + L" \"" + dest + L"\"";
        }

        // stdin de ffmpeg = les images ; stdout/stderr = tube lu par un thread et envoye au journal
        auto proc = platform::Process::start(platform::splitCommand(cmd), true, true);
        if (!proc) {
            e = TX("error.ffmpeg_launch");
            return Result::Failed;
        }

        std::thread errReader([&] {
            std::string acc;
            char buf[4096];
            size_t got;
            while ((got = proc->readOutput(buf, sizeof(buf))) > 0) {
                acc.append(buf, got);
                size_t p;
                while ((p = acc.find('\n')) != std::string::npos) {
                    std::string line = trim(acc.substr(0, p));
                    acc.erase(0, p + 1);
                    if (!line.empty()) H.log(TX("log.ffmpeg_prefix") + W(line));
                }
            }
            acc = trim(acc);
            if (!acc.empty()) H.log(TX("log.ffmpeg_prefix") + W(acc));
        });

        FrameQueue q;
        q.proc = proc.get();
        q.frameBytes = frameBytes;
        q.bufs.assign(4, std::vector<uint8_t>(frameBytes));
        for (int i = 0; i < 4; i++) q.freeQ.push(i);
        std::thread writer(writerThread, &q);

        bool aborted = false;
        auto fetch = [&](long long idx) -> bool {
            ogl::BindBuffer(ogl::K_PIXEL_PACK_BUFFER, pbo[idx % D]);
            const uint8_t* src = (const uint8_t*)ogl::MapBuffer(ogl::K_PIXEL_PACK_BUFFER, ogl::K_READ_ONLY);
            if (!src) return false;
            int slot;
            {
                std::unique_lock<std::mutex> l(q.m);
                auto w0 = clk::now();
                while (q.freeQ.empty() && !q.failed) {
                    if (H.cancelled()) {
                        ogl::UnmapBuffer(ogl::K_PIXEL_PACK_BUFFER);
                        return false;
                    }
                    q.cv.wait_for(l, std::chrono::milliseconds(100));
                }
                waitSec += std::chrono::duration<double>(clk::now() - w0).count();
                if (q.failed) {
                    ogl::UnmapBuffer(ogl::K_PIXEL_PACK_BUFFER);
                    return false;
                }
                slot = q.freeQ.front();
                q.freeQ.pop();
            }
            memcpy(q.bufs[slot].data(), src, frameBytes);
            ogl::UnmapBuffer(ogl::K_PIXEL_PACK_BUFFER);
            {
                std::lock_guard<std::mutex> l(q.m);
                q.fullQ.push(slot);
            }
            q.cv.notify_all();
            return true;
        };

        const long long cnt = n1 - n0;
        auto t0 = clk::now(), tLast = t0;
        for (long long k = 0; k < cnt && !aborted; ++k) {
            if (H.paused()) {  // pause : on attend, sans compter ce temps dans la vitesse affichee
                auto ps = clk::now();
                while (H.paused() && !H.cancelled()) platform::sleepMs(50);
                auto d = clk::now() - ps;
                t0 += d;
                tLast += d;
            }
            if (H.cancelled()) { aborted = true; break; }
            long long n = n0 + k;
            double t = s.start + (double)n / s.fps;  // temps exact
            if (sheet) {  // vignette k : au milieu du k-ieme intervalle de la plage rendue ; n = l'image de ce temps (les tampons y sont rejoues)
                t = s.start + ((double)n + 0.5) * duration / (double)N;
                n = std::llround((t - s.start) * s.fps);
            }
            advanceBuffers(n);
            renderFrame(n, t, videoMode ? fadeAt(n) : 1.0f);

            ogl::BindBuffer(ogl::K_PIXEL_PACK_BUFFER, pbo[k % D]);
            glReadPixels(0, 0, RW, RH, GL_RGBA, deep ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, nullptr);
            if (k >= D - 1 && !fetch(k - (D - 1))) aborted = true;

            auto now = clk::now();
            if (now - tLast > std::chrono::milliseconds(250) || k == cnt - 1) {
                tLast = now;
                double el = std::chrono::duration<double>(now - t0).count();
                double speed = (k + 1) / std::max(el, 1e-6);
                if (H.progress) H.progress(doneBefore + k + 1, N, speed, (N - doneBefore - k - 1) / speed);
            }
        }
        for (long long m = std::max(0LL, cnt - (D - 1)); m < cnt && !aborted; ++m)
            if (!fetch(m)) aborted = true;
        loopSec += std::chrono::duration<double>(clk::now() - t0).count();

        // --- Fin : vider la file, fermer le pipe, attendre ffmpeg ----------------
        const bool cancelled = aborted && H.cancelled();
        if (cancelled) proc->terminate();  // debloque aussi le thread d'ecriture
        {
            std::lock_guard<std::mutex> l(q.m);
            q.finished = true;
        }
        q.cv.notify_all();
        writer.join();
        proc->closeStdin();
        const int code2 = proc->wait();
        errReader.join();

        if (cancelled) {
            platform::removeFile(dest);  // fichier partiel inutilisable
            return Result::Cancelled;
        }
        if (aborted || code2 != 0) {
            e = TX("error.ffmpeg_failed");
            return Result::Failed;
        }
        return Result::Ok;
    };

    // --- Plan : un seul fichier, ou des parties reprenables puis assemblees ----------
    const long long chunkFrames = (videoMode && kind == OutKind::Video && s.chunk > 0) ? std::max(1LL, std::llround(s.chunk * s.fps)) : 0;
    Result result = Result::Ok;
    if (chunkFrames == 0 || N <= chunkFrames) {
        if (sequence && !platform::dirExists(seqDir)) platform::makeDir(seqDir);
        result = encodeRange(0, N, sequence ? seqPattern : out, hasAudio, 0, err);
    } else {
        const std::wstring partsDir = out.substr(0, out.find_last_of(L'.')) + L".parts";
        platform::makeDir(partsDir);
        // Signature : tout ce qui change le resultat. Differente -> les anciennes parties sont jetees.
        std::string sigSrc;
        {
            std::wstring tail = wfmt(L"|%d|%d|%g|%g|%d|%ls|%ls|%ls|%d|%d|%ls|%g|%g|%g|%d|%ls|%lld|%lld|%d", W_, H_, s.fps,
                                     s.start, s.crf, W(s.preset).c_str(), W(s.codec).c_str(), W(enc).c_str(), s.bitrate,
                                     s.blur, W(s.color).c_str(), s.shutter, s.fadein, s.fadeout, s.ss,
                                     W(s.container).c_str(), N, chunkFrames, s.depth);
            sigSrc = code + U8(tail) + "|vr" + std::to_string(vrMode) + (vrMode == 2 ? "|" + std::to_string(s.vrIpd) : "") + params.signature();
            {  // post-traitement : passe .post, effets et contenu du fichier LUT
                std::string lutBytes;
                if (!s.lut.empty()) {
                    const bool absolute = s.lut.size() > 1 && (s.lut[1] == L':' || s.lut[0] == L'/' || s.lut[0] == L'\\');
                    readFile(absolute ? s.lut : shaderDir + s.lut, lutBytes);
                }
                sigSrc += "|post:" + s.post + "|" + fnvHex(bp.postCode()) + "|" + fnvHex(lutBytes);
                // sortie : tout ce qui change les images ou l'encodage d'une partie
                sigSrc += "|out:" + std::to_string(s.alpha) + s.hdr + std::to_string(s.hdrPeak) + "," + std::to_string(s.hdrFall) + "," +
                          std::to_string(s.hdrWhite) + "|" + std::to_string(s.interp) + "|" + std::to_string(OW) + "x" + std::to_string(OH) + s.scaleFilter +
                          "|" + U8(s.title) + "|" + U8(s.credit) + "|" + std::to_string(s.titleDur);
            }
        }
        const std::string sig = fnvHex(sigSrc);
        const std::wstring sigFile = partsDir + L"/signature.txt";
        std::string old;
        if (!readFile(sigFile, old) || trim(old) != sig) {
            clearDir(partsDir);
            if (FILE* f = platform::openFile(sigFile, "wb")) {
                fputs(sig.c_str(), f);
                fclose(f);
            }
        }

        const long long nChunks = (N + chunkFrames - 1) / chunkFrames;
        long long done = 0;
        int resumed = 0;
        bool resumeLogged = false;
        std::vector<std::wstring> parts;
        for (long long i = 0; i < nChunks && result == Result::Ok; i++) {
            const long long n0 = i * chunkFrames, n1 = std::min(N, n0 + chunkFrames);
            const std::wstring part = partsDir + L"/" + wfmt(L"%05lld", i) + ext;
            parts.push_back(part);
            if (fileExists(part)) {  // partie deja terminee lors d'un rendu precedent
                done += n1 - n0;
                resumed++;
                continue;
            }
            if (resumed && !resumeLogged) {
                H.log(L"  " + TXF("log.resume", resumed, (long long)nChunks));
                resumeLogged = true;
            }
            const std::wstring tmp = partsDir + L"/" + wfmt(L"%05lld", i) + L".part" + ext;
            result = encodeRange(n0, n1, tmp, false, done, err);
            if (result == Result::Ok) {
                if (!platform::moveFile(tmp, part, true)) {
                    err = TXF("error.part_finalize", partsDir.c_str());
                    result = Result::Failed;
                }
            }
            done += n1 - n0;
        }
        if (result == Result::Ok) {  // assemblage : copie des flux, son ajoute ici
            H.log(L"  " + TX("log.assembling"));
            const std::wstring listFile = partsDir + L"/list.txt";
            std::string list;
            for (const auto& p : parts) list += "file '" + U8(p.substr(p.find_last_of(L"\\/") + 1)) + "'\n";
            if (FILE* f = platform::openFile(listFile, "wb")) {
                fputs(list.c_str(), f);
                fclose(f);
            }
            std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -f concat -safe 0 -i \"" + listFile + L"\"";
            if (hasAudio) cmd += audioArgs;
            cmd += L" -map 0:v";
            if (hasAudio) cmd += L" -map 1:a" + audioCodecArgs(s.container) + L" -af \"" + audioFilter + L"\" -shortest";
            cmd += L" -c:v copy" + std::wstring(s.codec == "h265" ? L" -tag:v hvc1" : L"") + containerArgs(s.container) + metaArgs +
                   L" \"" + out + L"\"";
            int rc = runProcess(cmd, [&](const std::wstring& line) { H.log(TX("log.ffmpeg_prefix") + line); });
            if (rc != 0) {
                err = TX("error.assemble_failed");
                result = Result::Failed;
            } else {
                clearDir(partsDir);
                platform::removeDir(partsDir);
            }
        } else if (result == Result::Cancelled) {
            H.log(L"  " + TXF("log.interrupted",
                       (long long)std::count_if(parts.begin(), parts.end(), [](const std::wstring& p) { return fileExists(p); }),
                       (long long)nChunks, partsDir.c_str()));
        }
    }
    cleanupGL();

    if (result == Result::Ok) {
        if (videoMode && loopSec > 1.0 && waitSec > 0.3 * loopSec)
            H.log(L"  " + TXF("log.measure_encoder_bound",
                       100.0 * waitSec / loopSec));
        else if (videoMode && loopSec > 1.0)
            H.log(L"  " + TXF("log.measure_gpu_bound", 100.0 * waitSec / loopSec));
        if (vrMode > 0 && videoMode && kind == OutKind::Video && s.metadata) {  // video spherique reconnue par YouTube et les lecteurs 360
            std::wstring metaErr;
            if (s.container != "mp4" && s.container != "mov") H.log(L"  " + TXF("log.vr_meta_skipped", W(s.container).c_str()));
            else if (injectSphericalMetadata(out, vrMode == 2, metaErr)) H.log(L"  " + TX("log.vr_meta_done"));
            else H.log(L"  " + metaErr);
        }
        H.log(L"  " + TXF("log.ok_output", out.c_str()));
        if (H.output) H.output(out);
    }
    return result;
}

// ---------------------------------------------------------------------------
//  Variantes : le rendu de base, puis un rendu par variante (autre taille, cadence, codec...)
// ---------------------------------------------------------------------------
// « 720p:width=1280|height=720 » -> nom + reglages ; le nom entre dans le nom du fichier de sortie
static bool parseVariant(const std::string& spec, std::string& name, Forced& out) {
    const size_t colon = spec.find(':');
    name = trim(colon == std::string::npos ? spec : spec.substr(0, colon));
    for (auto& c : name)
        if (!isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
    if (name.empty()) return false;
    size_t pos = colon == std::string::npos ? spec.size() : colon + 1;
    while (pos < spec.size()) {
        size_t e = spec.find('|', pos);
        if (e == std::string::npos) e = spec.size();
        const std::string item = spec.substr(pos, e - pos);
        pos = e + 1;
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(item.substr(0, eq)));
        if (isSettingKey(key) && key != "variant") out.push_back({key, trim(item.substr(eq + 1))});
    }
    return true;
}

Result convertFileForced(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H, std::wstring& err,
                         const std::vector<std::pair<std::string, std::string>>& forced) {
    return convertOne(shaderPath, exeDir, base, H, err, &forced);
}

Result convertFile(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H,
                   std::wstring& err) {
    if (isMontageFile(shaderPath) && !H.snapshot && !H.previewFrame) return convertMontage(shaderPath, exeDir, base, H, err);
    if (H.snapshot || H.previewFrame || (H.sheetCols > 0 && H.sheetRows > 0)) return convertOne(shaderPath, exeDir, base, H, err, nullptr);
    std::string code;
    Settings probe = base;
    if (readFile(shaderPath, code)) {
        Hooks quiet;
        quiet.log = [](const std::wstring&) {};
        normalizeShaderText(code, quiet);
        parseLines(probe, code, true);
    }
    if (probe.variants.empty()) return convertOne(shaderPath, exeDir, base, H, err, nullptr);

    Result result = convertOne(shaderPath, exeDir, base, H, err, nullptr);  // rendu de base
    if (result == Result::Cancelled) return result;
    int index = 0;
    for (const auto& spec : probe.variants) {
        std::string name;
        Forced forced;
        index++;
        if (!parseVariant(spec, name, forced)) continue;
        bool hasName = false;
        for (const auto& kv : forced)
            if (kv.first == "name") hasName = true;
        if (!hasName) forced.push_back({"name", (probe.name.empty() ? std::string("{shader}") : U8(probe.name)) + "_" + name});
        H.log(L"  " + TXF("log.variant", W(name).c_str(), index, (int)probe.variants.size()));
        std::wstring e;
        const Result r = convertOne(shaderPath, exeDir, base, H, e, &forced);
        if (r == Result::Cancelled) return r;
        if (r == Result::Failed) {
            err = e;
            result = Result::Failed;
        }
    }
    return result;
}
