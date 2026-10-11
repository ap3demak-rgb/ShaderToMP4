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
#include "gl.h"
#include "process.h"
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
static void normalizeShaderText(std::string& code, const Hooks& H) {
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
Result convertFile(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base,
                          const Hooks& H, std::wstring& err) {
    std::string code;
    if (!readFile(shaderPath, code)) { err = TX("error.read_file"); return Result::Failed; }
    normalizeShaderText(code, H);
    std::vector<std::wstring> notes;  // adaptations et avertissements de compatibilite, journalises avant le rendu
    code = adaptShaderCode(code, notes);
    if (code.find("mainImage") == std::string::npos) {
        err = TX("error.no_main_image");
        return Result::Failed;
    }

    Settings s = base;
    parseLines(s, code, true);
    for (int i = 0; i < 4; i++)  // canal lu par le shader mais sans texture : il lit du noir
        if (s.channel[i].empty() && code.find("iChannel" + std::to_string(i)) != std::string::npos)
            H.log(L"  " + TXF("log.warn_ichannel", i, i));
    if (!sameSettings(s, base)) H.log(L"  " + TX("log.shader_overrides"));
    s.width = std::max(16, s.width & ~1);
    s.height = std::max(16, s.height & ~1);
    if (s.fps <= 0) s.fps = 60;
    if (s.duration <= 0) s.duration = 10;
    s.crf = std::min(51, std::max(0, s.crf));
    const bool preview = (bool)H.previewFrame;
    const bool videoMode = !preview && !H.snapshot;
    const OutKind kind = outKind(s.container);
    if (!videoMode || kind == OutKind::Gif || kind == OutKind::Webp) s.depth = 8;  // 10 bits : video et sequences seulement
    const bool deep = s.depth >= 10;
    const size_t bpc = deep ? 2 : 1;  // octets par composante lue sur le GPU
    const int B = std::min(64, std::max(1, s.blur));  // sous-images par image (flou de mouvement)
    const int D = std::min(8, std::max(2, s.pbo));    // profondeur de la file de lecture GPU

    // Duree : multiple entier de la periode si "loop" est donne (boucle parfaite : la derniere image
    // s'arrete juste avant le retour au point de depart)
    double duration = s.duration;
    if (s.loop > 0) duration = s.loop * std::max(1.0, std::floor(s.duration / s.loop + 0.5));
    const long long N = videoMode ? std::max(1LL, std::llround(duration * s.fps)) : 1;
    const double totalSec = N / s.fps;
    const int W_ = s.width, H_ = s.height;
    const int SS = preview ? 1 : std::min(4, std::max(1, s.ss));  // supersampling : reduit par ffmpeg
    const int RW = W_ * SS, RH = H_ * SS;
    const size_t frameBytes = (size_t)RW * RH * 4 * bpc;
    const bool hasAudio = videoMode && kind == OutKind::Video && !s.audio.empty();
    if (videoMode && kind != OutKind::Video && !s.audio.empty()) H.log(L"  " + TX("log.audio_ignored"));
    if (hasAudio && !fileExists(s.audio)) { err = TXF("error.audio_missing", s.audio.c_str()); return Result::Failed; }

    // Chemin de sortie : dossier choisi (sinon celui du shader), meme nom, extension du conteneur (ou .png)
    if (videoMode && !codecContainerOk(s.codec, s.container)) {
        err = TXF("error.codec_container", codecLabel(s.codec), W(s.container).c_str());
        return Result::Failed;
    }
    if (videoMode && kind == OutKind::Video && isDnxhr(s.codec) && (s.width < 256 || s.height < 120)) {
        err = TX("error.dnxhr_size");
        return Result::Failed;
    }
    if (!preview && !s.outdir.empty()) platform::makeDir(s.outdir);  // sans effet si deja present
    const std::wstring ext = H.snapshot ? L".png" : L"." + W(s.container);
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
        H.log(TXF("log.render_summary", W_, H_, s.fps, totalSec, N,
                   s.bitrate > 0 ? wfmt(L"%d kb/s", s.bitrate).c_str() : wfmt(L"crf %d", s.crf).c_str(),
                   W(s.preset).c_str(), kind == OutKind::Video ? codecLabel(s.codec) : containerAt(containerIndex(s.container)).label));
        if (deep) H.log(L"  " + TX("log.depth10"));
        if (kind == OutKind::Gif && s.fps > 30) H.log(L"  " + TXF("log.gif_fps", s.fps));
        if (kind == OutKind::Gif && (double)W_ * H_ * N * 3 > 2.0e9) {
            err = TXF("error.gif_too_big", W_, H_, (long long)N);
            return Result::Failed;
        }
        if (B > 1) H.log(L"  " + TXF("log.motion_blur", B, s.shutter));
        if (s.loop > 0) H.log(L"  " + TXF("log.perfect_loop", s.loop, duration));
    }

    // --- Shader + FBO + PBO ---------------------------------------------------
    BufferPasses bp;  // Buffer A-D et Common : fichiers a cote du shader
    bp.discover(shaderPath, [&](const std::wstring& p, std::string& out) {
        if (!readFile(p, out)) return false;
        normalizeShaderText(out, H);
        out = adaptShaderCode(out, notes);
        return true;
    }, H);
    if (!preview) {
        checkCompatibility(code + "\n" + bp.allCode(), s, notes);
        for (const auto& n : notes) H.log(L"  " + n);
    }
    GLuint prog = buildProgram(bp.withCommon(code), err);
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
        present = buildPresentProgram(err);
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
    if (!bp.build(RW, RH, shaderDir, findFfmpeg(exeDir), H, err)) {
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
    ogl::Uniform3f(uRes, (float)RW, (float)RH, 1.0f);
    ogl::Uniform1f(uDelta, (float)(1.0 / s.fps));
    ogl::Uniform1f(uRate, (float)s.fps);
    float mouseV[4] = {0, 0, 0, 0};
    if (s.mouseSet) {  // iMouse fixe : x, y en pixels du rendu ; z, w negatifs tant que le bouton n'est pas enfonce
        const float mx = (float)(s.mouseX * RW), my = (float)(s.mouseY * RH);
        mouseV[0] = mx; mouseV[1] = my; mouseV[2] = s.mouseDown ? mx : -mx; mouseV[3] = s.mouseDown ? my : -my;
    }
    ogl::Uniform4f(uMouse, mouseV[0], mouseV[1], mouseV[2], mouseV[3]);
    if (!loadChannels(s, shaderDir, findFfmpeg(exeDir), chans, H, err, RW, RH)) {  // canaux d'entree (textures, tampons)
        cleanupGL();
        return Result::Failed;
    }
    bindChannels(chans, prog);
    ogl::Uniform1f(uSR, 44100.0f);
    ogl::Uniform1f(uWeight, 1.0f);
    platform::DateTime st = platform::localTime();
    if (!s.date.empty()) {  // date fixe : AAAA-MM-JJ[THH:MM[:SS]]
        int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
        std::string dt = s.date;
        for (auto& c : dt)
            if (c == 'T' || c == 't' || c == '_') c = ' ';
        if (sscanf(dt.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mi, &ss) >= 3) {
            st.year = y; st.month = mo; st.day = d; st.hour = hh; st.minute = mi; st.second = ss;
        }
    }
    const float dateSec = st.hour * 3600.f + st.minute * 60.f + st.second;

    // Passes de tampon : memes variables que l'image, renseignees pour chaque programme
    auto setPassUniforms = [&](GLuint p, long long n) {
        const double t = s.start + (double)n / s.fps;
        ogl::Uniform3f(ogl::GetUniformLocation(p, "iResolution"), (float)RW, (float)RH, 1.0f);
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iTime"), (float)t);
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iTimeDelta"), (float)(1.0 / s.fps));
        ogl::Uniform1i(ogl::GetUniformLocation(p, "iFrame"), (GLint)n);
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iFrameRate"), (float)s.fps);
        ogl::Uniform4f(ogl::GetUniformLocation(p, "iMouse"), mouseV[0], mouseV[1], mouseV[2], mouseV[3]);
        ogl::Uniform4f(ogl::GetUniformLocation(p, "iDate"), (float)st.year, (float)(st.month - 1), (float)st.day, dateSec + (float)t);
        ogl::Uniform1f(ogl::GetUniformLocation(p, "iSampleRate"), 44100.0f);
    };
    // Les tampons avancent image par image : si le rendu demarre plus loin (reprise, apercu), on rejoue les images
    // precedentes sans rien encoder
    long long simFrame = 0;
    bool replayLogged = false;
    auto advanceBuffers = [&](long long n) {
        if (!bp.any()) return;
        for (; simFrame < n && !H.cancelled(); simFrame++) {
            if (!replayLogged) {
                H.log(L"  " + TXF("log.buffers_replay", (long long)n));
                replayLogged = true;
            }
            bp.step(simFrame, RW, RH, setPassUniforms);
        }
        bp.step(n, RW, RH, setPassUniforms);
        simFrame = n + 1;
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);  // retour a l'image
        ogl::UseProgram(prog);
    };

    auto drawAt = [&](double t, long long n) {
        ogl::Uniform1f(uTime, (float)t);
        ogl::Uniform1i(uFrame, (GLint)n);
        ogl::Uniform4f(uDate, (float)st.year, (float)(st.month - 1), (float)st.day, dateSec + (float)t);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    // Rend l'image n (temps t, facteur de fondu "fade") dans fbo ;
    // avec flou : moyenne de B sous-images etalees sur l'obturateur
    auto renderFrame = [&](long long n, double t, float fade) {
        if (chans.anyBuffer()) {  // le tampon le plus recent change a chaque image
            GLuint latest[4];
            bp.latest(latest);
            bindChannels(chans, prog, latest);
        }
        if (B <= 1) {
            ogl::Uniform1f(uWeight, fade);
            drawAt(t, n);
            return;
        }
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, accFbo);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        ogl::Uniform1f(uWeight, fade / B);
        for (int k = 0; k < B; k++) drawAt(t + (double)k / B * s.shutter / s.fps, n);
        glDisable(GL_BLEND);
        ogl::Uniform1f(uWeight, 1.0f);
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);
        ogl::UseProgram(present);
        glBindTexture(GL_TEXTURE_2D, accTex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        ogl::UseProgram(prog);
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
        std::vector<uint8_t> px(frameBytes), flipped(frameBytes);
        glReadPixels(0, 0, RW, RH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        const size_t row = (size_t)RW * 4;
        for (int y = 0; y < RH; y++) {  // OpenGL est bas->haut ; RGBA -> BGRA
            const uint8_t* src = &px[(size_t)(RH - 1 - y) * row];
            uint8_t* dst = &flipped[(size_t)y * row];
            for (int x = 0; x < RW; x++) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = 255;
            }
        }
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
    const std::string enc = videoMode && kind == OutKind::Video ? resolveEncoder(s, ffmpeg, H) : "cpu";

    const std::wstring fpsStr = wfmt(L"%g", s.fps);
    const std::wstring ssFilter =
        SS > 1 ? L"scale=" + std::to_wstring(W_) + L":" + std::to_wstring(H_) + L":flags=area," : L"";
    const std::wstring head = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -y" +
        std::wstring(deep ? L" -f rawvideo -pix_fmt rgba64le -video_size " : L" -f rawvideo -pix_fmt rgba -video_size ") + std::to_wstring(RW) + L"x" + std::to_wstring(RH) +
        L" -framerate " + fpsStr + L" -i -";
    const ColorSpec cs = colorSpec(s.color);
    const double fi = videoMode ? std::max(0.0, std::min(s.fadein, totalSec)) : 0.0;
    const double fo = videoMode ? std::max(0.0, std::min(s.fadeout, totalSec)) : 0.0;
    // Fondus d'image : appliques dans le shader (facteur par image), donc exacts meme par parties
    auto fadeAt = [&](long long n) -> float {
        double f = 1.0;
        if (fi > 0) f = std::min(f, n / (fi * s.fps));
        if (fo > 0) f = std::min(f, (N - 1 - n) / (fo * s.fps));
        return (float)std::max(0.0, std::min(1.0, f));
    };
    std::wstring audioFilter;  // fondus du son
    if (fi > 0) audioFilter += wfmt(L"afade=t=in:st=0:d=%g,", fi);
    if (fo > 0) audioFilter += wfmt(L"afade=t=out:st=%g:d=%g,", totalSec - fo, fo);
    audioFilter += L"apad";  // son plus court que la video : complete par du silence
    const std::wstring audioArgs = wfmt(L" -ss %g", s.start) + L" -i \"" + s.audio + L"\"";

    // Metadonnees du conteneur : titre = nom du shader, commentaire = taille, cadence et version
    std::wstring metaArgs;
    if (s.metadata && videoMode && kind == OutKind::Video) {
        std::wstring title = shaderPath.substr(shaderPath.find_last_of(L"\\/") + 1);
        if (size_t dot = title.find_last_of(L'.'); dot != std::wstring::npos && dot > 0) title.erase(dot);
        title.erase(std::remove(title.begin(), title.end(), L'"'), title.end());
        metaArgs = L" -metadata \"title=" + title + L"\" -metadata \"comment=" + wfmt(L"%dx%d, %g fps, ", W_, H_, s.fps) +
                   APP_NAME_WSTR + L" " + APP_VER_WSTR + L"\"";
    }

    // Tout ce qui est regle pour une image depuis la boucle de rendu
    using clk = std::chrono::steady_clock;
    double waitSec = 0, loopSec = 0;  // mesure : temps bloque sur ffmpeg / temps total de rendu

    // Rend les images [n0, n1) vers ffmpeg -> dest (muxAudio : son inclus). doneBefore : images deja faites (progression).
    auto encodeRange = [&](long long n0, long long n1, const std::wstring& dest, bool muxAudio, long long doneBefore,
                           std::wstring& e) -> Result {
        std::wstring cmd;
        if (H.snapshot) {  // image RGB pleine plage, sans conversion YUV
            cmd = head + L" -vf \"vflip," + ssFilter + L"format=rgb24\" -frames:v 1 -pix_fmt rgb24 \"" + dest + L"\"";
        } else if (kind != OutKind::Video) {  // image animee ou sequence : RGB pleine plage, pas de YUV
            cmd = head + L" -vf \"vflip," + ssFilter + imageFilterTail(s) + L"\"" + imageEncoderArgs(s) + L" \"" + dest + L"\"";
        } else {
            cmd = head;
            if (muxAudio) cmd += audioArgs;
            cmd += L" -vf \"vflip," + ssFilter + L"scale=in_range=full:out_range=" + cs.range +
                   L":out_color_matrix=" + cs.matrix + L":flags=accurate_rnd+full_chroma_int," + outFormat(s, enc) + L"\"";
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
            const long long n = n0 + k;
            const double t = s.start + (double)n / s.fps;  // temps exact
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
            sigSrc = code + U8(tail);
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
        H.log(L"  " + TXF("log.ok_output", out.c_str()));
        if (H.output) H.output(out);
    }
    return result;
}
