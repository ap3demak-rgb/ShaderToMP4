#include "render.h"
#include "encoder.h"
#include "gl.h"
#include "process.h"
#include "util.h"

// ---------------------------------------------------------------------------
//  Envoi des images a ffmpeg dans un thread dedie (rendu et encodage en parallele)
// ---------------------------------------------------------------------------
struct FrameQueue {
    HANDLE pipe = nullptr;
    size_t frameBytes = 0;
    std::vector<std::vector<uint8_t>> bufs;
    std::queue<int> freeQ, fullQ;
    std::mutex m;
    std::condition_variable cv;
    bool finished = false, failed = false;
};

static bool writeAll(HANDLE h, const uint8_t* p, size_t n) {
    while (n > 0) {
        DWORD chunk = (DWORD)std::min<size_t>(n, 1u << 22), w = 0;
        if (!WriteFile(h, p, chunk, &w, nullptr) || w == 0) return false;
        p += w;
        n -= w;
    }
    return true;
}

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
        bool ok = writeAll(q->pipe, q->bufs[idx].data(), q->frameBytes);
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
        H.log(L"  Fichier shader en UTF-16 : converti en UTF-8.");
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
    if (!readFile(shaderPath, code)) { err = L"Impossible de lire le fichier."; return Result::Failed; }
    normalizeShaderText(code, H);
    if (code.find("mainImage") == std::string::npos) {
        err = L"Aucune fonction mainImage trouvee : ce n'est pas un shader Shadertoy (onglet Image).";
        return Result::Failed;
    }
    if (code.find("iChannel") != std::string::npos)
        H.log(L"  ATTENTION : le shader utilise iChannel (textures/buffers), non supportes : rendu noir pour ces entrees.");

    Settings s = base;
    parseLines(s, code, true);
    if (!sameSettings(s, base)) H.log(L"  Reglages du shader (// cle=valeur) prioritaires sur ceux de la fenetre.");
    s.width = std::max(16, s.width & ~1);
    s.height = std::max(16, s.height & ~1);
    if (s.fps <= 0) s.fps = 60;
    if (s.duration <= 0) s.duration = 10;
    s.crf = std::min(51, std::max(0, s.crf));
    const bool preview = (bool)H.previewFrame;
    const bool videoMode = !preview && !H.snapshot;
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
    const size_t frameBytes = (size_t)RW * RH * 4;
    const bool hasAudio = videoMode && !s.audio.empty();
    if (hasAudio && !fileExists(s.audio)) { err = L"Fichier audio introuvable : " + s.audio; return Result::Failed; }

    // Chemin de sortie : dossier choisi (sinon celui du shader), meme nom, extension du conteneur (ou .png)
    if (!preview && !s.outdir.empty()) CreateDirectoryW(s.outdir.c_str(), nullptr);  // sans effet si deja present
    const std::wstring ext = H.snapshot ? L".png" : L"." + W(s.container);
    const std::wstring out = outputPath(shaderPath, s.outdir, ext);
    const std::wstring outDir = out.substr(0, out.find_last_of(L"\\/"));

    // --- Verifications avant de commencer : espace disque ------------------------
    if (videoMode) {
        const double bpp = 0.3 * std::pow(2.0, -(s.crf - 12) / 6.0);  // ordre de grandeur, bits par pixel et par image
        const double est = s.bitrate > 0 ? s.bitrate * 125.0 * totalSec : (double)W_ * H_ * s.fps * totalSec * bpp / 8.0;
        ULARGE_INTEGER freeB{};
        if (GetDiskFreeSpaceExW(outDir.c_str(), &freeB, nullptr, nullptr)) {
            H.log(wfmt(L"  Taille estimee : ~%.0f Mo (espace libre : %.0f Mo)", est / 1048576.0, freeB.QuadPart / 1048576.0));
            const bool chunkedGuess = s.chunk > 0 && N > std::llround(s.chunk * s.fps);
            const double need = est * (chunkedGuess ? 2.2 : 1.2) + 50e6;  // les parties s'ajoutent au fichier final
            if ((double)freeB.QuadPart < need) {
                err = wfmt(L"Espace disque insuffisant sur %ls : il faut environ %.0f Mo, %.0f Mo libres.", outDir.c_str(),
                           need / 1048576.0, freeB.QuadPart / 1048576.0);
                return Result::Failed;
            }
        }
        H.log(wfmt(L"  %dx%d  %g fps  %g s  (%lld images)  %ls  preset %ls  %ls", W_, H_, s.fps, totalSec, N,
                   s.bitrate > 0 ? wfmt(L"%d kb/s", s.bitrate).c_str() : wfmt(L"crf %d", s.crf).c_str(),
                   W(s.preset).c_str(), s.codec == "h265" ? L"H.265" : L"H.264"));
        if (B > 1) H.log(wfmt(L"  Flou de mouvement : %d sous-images par image (obturateur %g)", B, s.shutter));
        if (s.loop > 0) H.log(wfmt(L"  Boucle parfaite : periode %g s -> duree %g s", s.loop, duration));
    }

    // --- Shader + FBO + PBO ---------------------------------------------------
    GLuint prog = buildProgram(code, err);
    if (!prog) return Result::Failed;

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
        if (present) ogl::DeleteProgram(present);
        ogl::DeleteProgram(prog);
    };

    GLint maxTex = 0;  // limite du GPU
    glGetIntegerv(0x0D33 /*GL_MAX_TEXTURE_SIZE*/, &maxTex);
    if (maxTex > 0 && (RW > maxTex || RH > maxTex)) {
        err = wfmt(L"Taille de rendu %dx%d (resolution x lissage) superieure a la limite du GPU (%d).", RW, RH, (int)maxTex);
        cleanupGL();
        return Result::Failed;
    }

    ogl::GenVertexArrays(1, &vao);
    ogl::BindVertexArray(vao);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, ogl::K_RGBA8, RW, RH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ogl::GenFramebuffers(1, &fbo);
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);
    ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (ogl::CheckFramebufferStatus(ogl::K_FRAMEBUFFER) != ogl::K_FRAMEBUFFER_COMPLETE) {
        err = L"Framebuffer incomplet (resolution trop grande pour ce GPU ?).";
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
            err = L"Flou de mouvement indisponible (framebuffer flottant refuse par le GPU).";
            cleanupGL();
            return Result::Failed;
        }
        ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);
    }
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
    ogl::Uniform4f(uMouse, 0, 0, 0, 0);
    ogl::Uniform1f(uSR, 44100.0f);
    ogl::Uniform1f(uWeight, 1.0f);
    SYSTEMTIME st;
    GetLocalTime(&st);
    const float dateSec = st.wHour * 3600.f + st.wMinute * 60.f + st.wSecond;

    auto drawAt = [&](double t, long long n) {
        ogl::Uniform1f(uTime, (float)t);
        ogl::Uniform1i(uFrame, (GLint)n);
        ogl::Uniform4f(uDate, (float)st.wYear, (float)(st.wMonth - 1), (float)st.wDay, dateSec + (float)t);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    // Rend l'image n (temps t, facteur de fondu "fade") dans fbo ;
    // avec flou : moyenne de B sous-images etalees sur l'obturateur
    auto renderFrame = [&](long long n, double t, float fade) {
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
        renderFrame(0, H.previewTime, 1.0f);
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
    std::wstring ffmpeg = exeDir + L"ffmpeg.exe";
    if (!fileExists(ffmpeg)) ffmpeg = L"ffmpeg.exe";  // sinon : PATH
    const std::string enc = videoMode ? resolveEncoder(s, ffmpeg, H) : "cpu";

    const std::wstring fpsStr = wfmt(L"%g", s.fps);
    const std::wstring ssFilter =
        SS > 1 ? L"scale=" + std::to_wstring(W_) + L":" + std::to_wstring(H_) + L":flags=area," : L"";
    const std::wstring head = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -y"
        L" -f rawvideo -pix_fmt rgba -video_size " + std::to_wstring(RW) + L"x" + std::to_wstring(RH) +
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

    // Tout ce qui est regle pour une image depuis la boucle de rendu
    using clk = std::chrono::steady_clock;
    double waitSec = 0, loopSec = 0;  // mesure : temps bloque sur ffmpeg / temps total de rendu

    // Rend les images [n0, n1) vers ffmpeg -> dest (muxAudio : son inclus). doneBefore : images deja faites (progression).
    auto encodeRange = [&](long long n0, long long n1, const std::wstring& dest, bool muxAudio, long long doneBefore,
                           std::wstring& e) -> Result {
        std::wstring cmd;
        if (H.snapshot) {  // image RGB pleine plage, sans conversion YUV
            cmd = head + L" -vf \"vflip," + ssFilter + L"format=rgb24\" -frames:v 1 -pix_fmt rgb24 \"" + dest + L"\"";
        } else {
            cmd = head;
            if (muxAudio) cmd += audioArgs;
            cmd += L" -vf \"vflip," + ssFilter + L"scale=in_range=full:out_range=" + cs.range +
                   L":out_color_matrix=" + cs.matrix + L":flags=accurate_rnd+full_chroma_int,format=yuv420p\"";
            if (muxAudio) cmd += L" -map 0:v -map 1:a -c:a aac -b:a 192k -af \"" + audioFilter + L"\" -shortest";
            cmd += encoderArgs(s, enc) + L" -pix_fmt yuv420p " + cs.tags +
                   (s.container == "mkv" ? L"" : L" -movflags +faststart") + L" \"" + dest + L"\"";
        }

        // stdin de ffmpeg = les images ; stdout/stderr = tube lu par un thread et envoye au journal
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE rd = nullptr, wr = nullptr, eRd = nullptr, eWr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 1 << 24)) { e = L"Impossible de creer le pipe."; return Result::Failed; }
        SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);
        if (!CreatePipe(&eRd, &eWr, &sa, 0)) {
            e = L"Impossible de creer le pipe.";
            CloseHandle(rd);
            CloseHandle(wr);
            return Result::Failed;
        }
        SetHandleInformation(eRd, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = rd;
        si.hStdOutput = eWr;
        si.hStdError = eWr;
        PROCESS_INFORMATION pi{};
        std::wstring cmdBuf = cmd;
        if (!CreateProcessW(nullptr, &cmdBuf[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            e = L"Impossible de lancer ffmpeg.exe. Place ffmpeg.exe dans le meme dossier que ShaderToMP4.exe.";
            CloseHandle(rd);
            CloseHandle(wr);
            CloseHandle(eRd);
            CloseHandle(eWr);
            return Result::Failed;
        }
        CloseHandle(rd);
        CloseHandle(eWr);

        std::thread errReader([&] {
            std::string acc;
            char buf[4096];
            DWORD got = 0;
            while (ReadFile(eRd, buf, sizeof(buf), &got, nullptr) && got > 0) {
                acc.append(buf, got);
                size_t p;
                while ((p = acc.find('\n')) != std::string::npos) {
                    std::string line = trim(acc.substr(0, p));
                    acc.erase(0, p + 1);
                    if (!line.empty()) H.log(L"  ffmpeg : " + W(line));
                }
            }
            acc = trim(acc);
            if (!acc.empty()) H.log(L"  ffmpeg : " + W(acc));
        });

        FrameQueue q;
        q.pipe = wr;
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
                while (H.paused() && !H.cancelled()) Sleep(50);
                auto d = clk::now() - ps;
                t0 += d;
                tLast += d;
            }
            if (H.cancelled()) { aborted = true; break; }
            const long long n = n0 + k;
            const double t = s.start + (double)n / s.fps;  // temps exact
            renderFrame(n, t, videoMode ? fadeAt(n) : 1.0f);

            ogl::BindBuffer(ogl::K_PIXEL_PACK_BUFFER, pbo[k % D]);
            glReadPixels(0, 0, RW, RH, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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
        if (cancelled) TerminateProcess(pi.hProcess, 1);  // debloque aussi le thread d'ecriture
        {
            std::lock_guard<std::mutex> l(q.m);
            q.finished = true;
        }
        q.cv.notify_all();
        writer.join();
        CloseHandle(wr);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code2 = 1;
        GetExitCodeProcess(pi.hProcess, &code2);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        errReader.join();
        CloseHandle(eRd);

        if (cancelled) {
            DeleteFileW(dest.c_str());  // fichier partiel inutilisable
            return Result::Cancelled;
        }
        if (aborted || code2 != 0) {
            e = L"ffmpeg s'est arrete avec une erreur (voir le journal ci-dessus).";
            return Result::Failed;
        }
        return Result::Ok;
    };

    // --- Plan : un seul fichier, ou des parties reprenables puis assemblees ----------
    const long long chunkFrames = (videoMode && s.chunk > 0) ? std::max(1LL, std::llround(s.chunk * s.fps)) : 0;
    Result result = Result::Ok;
    if (chunkFrames == 0 || N <= chunkFrames) {
        result = encodeRange(0, N, out, hasAudio, 0, err);
    } else {
        const std::wstring partsDir = out.substr(0, out.find_last_of(L'.')) + L".parts";
        CreateDirectoryW(partsDir.c_str(), nullptr);
        // Signature : tout ce qui change le resultat. Differente -> les anciennes parties sont jetees.
        std::string sigSrc;
        {
            std::wstring tail = wfmt(L"|%d|%d|%g|%g|%d|%ls|%ls|%ls|%d|%d|%ls|%g|%g|%g|%d|%ls|%lld|%lld", W_, H_, s.fps,
                                     s.start, s.crf, W(s.preset).c_str(), W(s.codec).c_str(), W(enc).c_str(), s.bitrate,
                                     s.blur, W(s.color).c_str(), s.shutter, s.fadein, s.fadeout, s.ss,
                                     W(s.container).c_str(), N, chunkFrames);
            sigSrc = code + U8(tail);
        }
        const std::string sig = fnvHex(sigSrc);
        const std::wstring sigFile = partsDir + L"\\signature.txt";
        std::string old;
        if (!readFile(sigFile, old) || trim(old) != sig) {
            clearDir(partsDir);
            if (FILE* f = _wfopen(sigFile.c_str(), L"wb")) {
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
            const std::wstring part = partsDir + L"\\" + wfmt(L"%05lld", i) + ext;
            parts.push_back(part);
            if (fileExists(part)) {  // partie deja terminee lors d'un rendu precedent
                done += n1 - n0;
                resumed++;
                continue;
            }
            if (resumed && !resumeLogged) {
                H.log(wfmt(L"  Reprise : %d partie(s) deja terminee(s) sur %lld.", resumed, (long long)nChunks));
                resumeLogged = true;
            }
            const std::wstring tmp = partsDir + L"\\" + wfmt(L"%05lld", i) + L".part" + ext;
            result = encodeRange(n0, n1, tmp, false, done, err);
            if (result == Result::Ok) {
                if (!MoveFileExW(tmp.c_str(), part.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    err = L"Impossible de finaliser une partie dans " + partsDir;
                    result = Result::Failed;
                }
            }
            done += n1 - n0;
        }
        if (result == Result::Ok) {  // assemblage : copie des flux, son ajoute ici
            H.log(L"  Assemblage des parties...");
            const std::wstring listFile = partsDir + L"\\list.txt";
            std::string list;
            for (const auto& p : parts) list += "file '" + U8(p.substr(p.find_last_of(L'\\') + 1)) + "'\n";
            if (FILE* f = _wfopen(listFile.c_str(), L"wb")) {
                fputs(list.c_str(), f);
                fclose(f);
            }
            std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -y -f concat -safe 0 -i \"" + listFile + L"\"";
            if (hasAudio) cmd += audioArgs;
            cmd += L" -map 0:v";
            if (hasAudio) cmd += L" -map 1:a -c:a aac -b:a 192k -af \"" + audioFilter + L"\" -shortest";
            cmd += L" -c:v copy" + std::wstring(s.codec == "h265" ? L" -tag:v hvc1" : L"") +
                   (s.container == "mkv" ? L"" : L" -movflags +faststart") + L" \"" + out + L"\"";
            int rc = runProcess(cmd, [&](const std::wstring& line) { H.log(L"  ffmpeg : " + line); });
            if (rc != 0) {
                err = L"L'assemblage des parties a echoue (voir le journal). Les parties sont conservees : relance pour reessayer.";
                result = Result::Failed;
            } else {
                clearDir(partsDir);
                RemoveDirectoryW(partsDir.c_str());
            }
        } else if (result == Result::Cancelled) {
            H.log(wfmt(L"  Interrompu : %lld/%lld parties terminees, conservees dans %ls (relance pour reprendre).",
                       (long long)std::count_if(parts.begin(), parts.end(), [](const std::wstring& p) { return fileExists(p); }),
                       (long long)nChunks, partsDir.c_str()));
        }
    }
    cleanupGL();

    if (result == Result::Ok) {
        if (videoMode && loopSec > 1.0 && waitSec > 0.3 * loopSec)
            H.log(wfmt(L"  Mesure : le rendu a attendu l'encodeur %.0f%% du temps (l'encodage est le goulot : "
                       L"essaie un preset plus rapide ou encoder=auto).",
                       100.0 * waitSec / loopSec));
        else if (videoMode && loopSec > 1.0)
            H.log(wfmt(L"  Mesure : attente de l'encodeur %.0f%% du temps (le GPU est le goulot).", 100.0 * waitSec / loopSec));
        H.log(L"  OK -> " + out);
    }
    return result;
}
