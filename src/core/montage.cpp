// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "montage.h"
#include "encoder.h"
#include "formats.h"
#include "i18n.h"
#include "overlay.h"
#include "params.h"
#include "process.h"
#include "render.h"
#include "rhythm.h"
#include "util.h"
#include "platform/platform.h"
#include <sstream>

bool isMontageFile(const std::wstring& path) {
    if (path.size() < 9) return false;
    std::wstring ext = path.substr(path.size() - 8);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext == L".montage";
}

static bool validTransition(const std::string& t) { return t == "cut" || t == "fade" || t == "crossfade" || t == "wipe"; }

static bool toNumber(const std::string& text, double& out) {
    char* end = nullptr;
    out = strtod(text.c_str(), &end);
    return !text.empty() && *end == 0 && std::isfinite(out);
}

bool parseMontage(const std::wstring& path, double defaultDuration, MontageDef& out, std::wstring& err) {
    std::string text;
    if (!readFile(path, text)) {
        err = TXF("error.montage_read", path.c_str());
        return false;
    }
    if (text.size() >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) text.erase(0, 3);
    const size_t cut = path.find_last_of(L"\\/");
    const std::wstring dir = cut == std::wstring::npos ? L"" : path.substr(0, cut + 1);
    double duration = defaultDuration, transDur = 1.0;
    std::string transition = "cut";
    out = MontageDef();
    int lineNo = 0;
    size_t pos = 0;
    auto fail = [&](const char* key, const std::wstring& what) {
        err = TXF(key, path.c_str(), lineNo, what.c_str());
        return false;
    };
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        lineNo++;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        line = trim(line);
        if (line.empty()) continue;
        if (lower(line.compare(0, 4, "set ") == 0 ? line.substr(0, 4) : "") == "set ") {  // « set cle=valeur » : reglage de la video finale
            const std::string kv = trim(line.substr(4));
            const size_t eq = kv.find('=');
            const std::string key = eq == std::string::npos ? "" : lower(trim(kv.substr(0, eq)));
            if (!isSettingKey(key)) return fail("error.montage_line", W(line));
            out.settings.push_back({key, trim(kv.substr(eq + 1))});
            continue;
        }
        const size_t eq = line.find('=');
        const bool directive = eq != std::string::npos && line.find_first_of(" \t") > eq;  // « duration=6 », « transition=fade 1 » : pas un chemin
        if (directive && line[0] != '"') {
            const std::string key = lower(trim(line.substr(0, eq))), val = trim(line.substr(eq + 1));
            if (key == "duration") {
                if (!toNumber(val, duration) || duration < 0.5 || duration > 36000) return fail("error.montage_line", W(line));
            } else if (key == "transition") {
                std::istringstream is(val);
                std::string name, secs;
                is >> name >> secs;
                if (!validTransition(lower(name))) return fail("error.montage_transition", W(name));
                transition = lower(name);
                if (!secs.empty() && (!toNumber(secs, transDur) || transDur < 0.05 || transDur > 60)) return fail("error.montage_line", W(line));
            } else {
                return fail("error.montage_line", W(line));
            }
            continue;
        }
        // ligne de clip : chemin (entre guillemets ou jusqu'au premier espace), puis duree, puis transition [duree]
        std::string file, rest;
        if (line[0] == '"') {
            const size_t q = line.find('"', 1);
            if (q == std::string::npos) return fail("error.montage_line", W(line));
            file = line.substr(1, q - 1);
            rest = trim(line.substr(q + 1));
        } else {
            const size_t sp = line.find_first_of(" \t");
            file = line.substr(0, sp);
            rest = sp == std::string::npos ? "" : trim(line.substr(sp));
        }
        MontageClip clip;
        const std::wstring wf = W(file);
        const bool absolute = wf.size() > 1 && (wf[1] == L':' || wf[0] == L'/' || wf[0] == L'\\');
        clip.path = absolute ? wf : dir + wf;
        clip.duration = duration;
        clip.transition = transition;
        clip.transitionDuration = transDur;
        std::istringstream is(rest);
        std::string tok;
        bool haveDuration = false, haveTransition = false;
        while (is >> tok) {
            double v = 0;
            const std::string lt = lower(tok);
            if (validTransition(lt)) {
                clip.transition = lt;
                haveTransition = true;
            } else if (toNumber(tok, v)) {
                if (!haveDuration && !haveTransition) {
                    if (v < 0.5 || v > 36000) return fail("error.montage_line", W(line));
                    clip.duration = v;
                    haveDuration = true;
                } else if (haveTransition) {
                    if (v < 0.05 || v > 60) return fail("error.montage_line", W(line));
                    clip.transitionDuration = v;
                } else {
                    return fail("error.montage_line", W(line));
                }
            } else {
                return fail("error.montage_transition", W(tok));
            }
        }
        if (!fileExists(clip.path)) {
            err = TXF("error.montage_clip_missing", path.c_str(), lineNo, clip.path.c_str());
            return false;
        }
        out.clips.push_back(clip);
    }
    if (out.clips.empty()) {
        err = TXF("error.montage_empty", path.c_str());
        return false;
    }
    return true;
}

// Duree et presence d'une piste audio d'un clip, lues sur la sortie de « ffmpeg -i »
static bool probeClip(const std::wstring& ffmpeg, const std::wstring& file, double& duration, bool& hasAudio) {
    std::string all;
    runProcess(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -i \"" + file + L"\"", [&](const std::wstring& l) { all += U8(l) + "\n"; });
    const size_t p = all.find("Duration: ");
    int h = 0, m = 0;
    double s = 0;
    if (p == std::string::npos || sscanf(all.c_str() + p + 10, "%d:%d:%lf", &h, &m, &s) != 3) return false;
    duration = h * 3600.0 + m * 60.0 + s;
    hasAudio = all.find("Audio:") != std::string::npos;
    return true;
}

// Empreinte d'un clip : son code (passes comprises) et les reglages qui changent ses images ; sans les commentaires de config.ini (langue)
static std::string clipSignature(const std::wstring& shader, const Settings& s, const std::vector<std::pair<std::string, std::string>>& forced) {
    std::string sig = collectShaderCode(shader);
    for (const auto& kv : forced) sig += "|" + kv.first + "=" + kv.second;
    Settings c = s;
    for (const auto& kv : forced) applySetting(c, kv.first, kv.second);
    const std::wstring tmp = platform::appDir() + L"montage_sig.tmp";
    writeIni(tmp, c);
    std::string ini;
    readFile(tmp, ini);
    platform::removeFile(tmp);
    size_t pos = 0;
    while (pos < ini.size()) {
        size_t e = ini.find('\n', pos);
        if (e == std::string::npos) e = ini.size();
        std::string line = ini.substr(pos, e - pos);
        pos = e + 1;
        if (line.empty() || line[0] == ';') continue;
        const size_t sc = line.find("      ;");
        if (sc != std::string::npos) line.resize(sc);
        sig += line;
    }
    return fnvHex(sig);
}

std::vector<double> snapMontageDurations(const std::vector<double>& requested, const std::vector<double>& trans, const std::vector<double>& grid) {
    const size_t n = requested.size();
    std::vector<double> dur = requested;
    double start = 0;  // instant ou le clip i commence a l'ecran
    for (size_t i = 0; i < n; i++) {
        const double into = i > 0 && i < trans.size() ? trans[i] : 0.0;           // transition qui amene le clip
        const double next = i + 1 < n && i + 1 < trans.size() ? trans[i + 1] : 0.0;  // transition vers le suivant
        const double minDur = std::max(0.5, into + next);
        const double nominal = start + requested[i] - next / 2;  // centre de la coupe demandee (fin du dernier clip)
        const double minCut = start + minDur - next / 2;
        double best = -1;
        for (double g : grid) {
            if (g < minCut - 1e-9) continue;
            if (best < 0 || std::fabs(g - nominal) < std::fabs(best - nominal) - 1e-9) best = g;
        }
        const double cut = best >= 0 ? best : std::max(nominal, minCut);
        dur[i] = cut - start + next / 2;
        start = cut - next / 2;
    }
    return dur;
}

static std::wstring xfadeName(const std::string& t) { return t == "fade" ? L"fadeblack" : t == "wipe" ? L"wipeleft" : L"fade"; }

Result convertMontage(const std::wstring& montagePath, const std::wstring& exeDir, const Settings& base, const Hooks& H, std::wstring& err) {
    MontageDef def;
    if (!parseMontage(montagePath, base.duration, def, err)) return Result::Failed;
    Settings s = base;
    for (const auto& kv : def.settings) applySetting(s, kv.first, kv.second);
    s.width = std::max(16, s.width & ~1);
    s.height = std::max(16, s.height & ~1);
    if (s.fps <= 0) s.fps = 60;
    const OutKind kind = outKind(s.container);
    if (kind != OutKind::Video) {
        err = TXF("error.montage_container", W(s.container).c_str());
        return Result::Failed;
    }
    if (s.alpha || s.hdr != "off") {
        err = TX("error.montage_alpha_hdr");
        return Result::Failed;
    }
    if (!codecContainerOk(s.codec, s.container)) {
        err = TXF("error.codec_container", codecLabel(s.codec), W(s.container).c_str());
        return Result::Failed;
    }
    int OW = s.width, OH = s.height;
    if (!outputSize(s, OW, OH)) {
        err = TXF("error.scale_invalid", W(s.scale).c_str());
        return Result::Failed;
    }
    const std::wstring ffmpeg = findFfmpeg(exeDir);
    if (!formatAvailable(ffmpeg, s.codec, s.container)) {
        err = TXF("error.encoder_missing", W(requiredEncoders(s.codec, s.container).front()).c_str());
        return Result::Failed;
    }
    // Rythme du son : coupes calees sur les temps, les mesures ou les sections, marqueurs du fichier final
    Rhythm rhythm;
    bool rhythmOk = false;
    if ((s.beatSync != "off" || s.markers != "off") && !s.audio.empty()) {
        std::wstring re;
        rhythmOk = rhythm.load(ffmpeg, s.audio, s.bpm, s.beatsPerBar, re);
        if (!rhythmOk) H.log(L"  " + re);
    } else if (s.beatSync != "off") {
        H.log(L"  " + TX("log.beatsync_no_audio"));
    }
    if (s.beatSync != "off" && rhythmOk) {
        const RhythmGrid& g = rhythm.grid();
        if (g.beats.empty()) {
            H.log(L"  " + TX("log.markers_notempo"));
        } else {
            const std::vector<double>& src = s.beatSync == "section" ? g.sections : s.beatSync == "bar" && !g.bars.empty() ? g.bars : g.beats;
            std::vector<double> grid;
            for (double t : src)
                if (t - s.start > 0) grid.push_back(t - s.start);
            std::vector<double> requested, trans;
            for (const auto& c : def.clips) {
                requested.push_back(c.duration);
                trans.push_back(c.transition == "cut" ? 0.0 : c.transitionDuration);
            }
            const std::vector<double> snapped = snapMontageDurations(requested, trans, grid);
            for (size_t i = 0; i < def.clips.size(); i++) {
                if (std::fabs(snapped[i] - requested[i]) > 1e-6) H.log(L"  " + TXF("log.beatsync_clip", (int)i + 1, requested[i], snapped[i]));
                def.clips[i].duration = snapped[i];
            }
        }
    }
    std::wstring overlay;
    if (!overlayFilter(s, 0, OW, OH, ffmpeg, overlay, err)) return Result::Failed;
    if (!s.outdir.empty()) platform::makeDir(s.outdir);
    bool skip = false;
    const std::wstring out = outputFile(montagePath, s, true, &skip);
    if (skip) {
        H.log(L"  " + TXF("log.skip_existing", out.c_str()));
        return Result::Ok;
    }
    const std::wstring work = out.substr(0, out.find_last_of(L'.')) + L".clips";
    platform::makeDir(work);
    const std::wstring sep(1, platform::pathSeparator());
    const bool prores = formatAvailable(ffmpeg, "prores_hq", "mov");  // intermediaires : ProRes HQ 10 bits, sinon H.264 sans perte
    const size_t n = def.clips.size();
    H.log(TXF("log.montage_start", (int)n, (int)(def.clips.size() > 1 ? n - 1 : 0)));

    long long allFrames = 0;
    for (const auto& c : def.clips) allFrames += std::max(1LL, std::llround(c.duration * s.fps));
    long long doneFrames = 0;
    std::vector<std::wstring> files;
    std::vector<double> durations(n, 0);
    std::vector<char> audio(n, 0);  // 1 = le clip a une piste audio
    for (size_t i = 0; i < n; i++) {
        const MontageClip& clip = def.clips[i];
        const std::wstring name = wfmt(L"clip%03d", (int)i + 1);
        const std::wstring file = work + sep + name + (prores ? L".mov" : L".mkv");
        std::vector<std::pair<std::string, std::string>> forced = {
            {"duration", std::to_string(clip.duration)}, {"loop", "0"},     {"width", std::to_string(s.width)}, {"height", std::to_string(s.height)},
            {"fps", std::to_string(s.fps)},              {"outdir", U8(work)}, {"name", U8(name)},               {"exists", "overwrite"},
            {"audio", ""},                               {"scale", "off"},  {"interp", "off"},                  {"title", ""},
            {"credit", ""},                              {"loudnorm", "off"}, {"fadein", "0"},                  {"fadeout", "0"},
            {"metadata", "0"},                           {"hdr", "off"},    {"alpha", "0"},                     {"container", prores ? "mov" : "mkv"},
            {"codec", prores ? "prores_hq" : "h264"},    {"depth", prores ? "10" : "8"}, {"post", s.post},       {"lut", U8(s.lut)}};
        if (!prores) {
            forced.push_back({"crf", "0"});
            forced.push_back({"preset", "ultrafast"});
        }
        const std::wstring sigFile = work + sep + name + L".sig";
        const std::string sig = clipSignature(clip.path, s, forced);
        std::string old;
        bool done = fileExists(file) && readFile(sigFile, old) && trim(old) == sig;
        const long long frames = std::max(1LL, std::llround(clip.duration * s.fps));
        if (done) {
            H.log(TXF("log.montage_clip_reused", (int)i + 1, (int)n, clip.path.c_str()));
        } else {
            H.log(TXF("log.montage_clip", (int)i + 1, (int)n, clip.path.c_str(), clip.duration));
            Hooks hc = H;
            hc.output = nullptr;
            hc.progress = [&](long long k, long long, double speed, double remaining) {
                if (H.progress) H.progress(doneFrames + k, allFrames, speed, remaining);
            };
            std::wstring e;
            const Result r = convertFileForced(clip.path, exeDir, s, hc, e, forced);
            if (r == Result::Cancelled) return r;
            if (r != Result::Ok) {
                err = TXF("error.montage_clip_failed", (int)i + 1, clip.path.c_str(), e.c_str());
                return Result::Failed;
            }
            if (FILE* f = platform::openFile(sigFile, "wb")) {
                fputs(sig.c_str(), f);
                fclose(f);
            }
        }
        doneFrames += frames;
        files.push_back(file);
        bool hasAudio = false;
        if (!probeClip(ffmpeg, file, durations[i], hasAudio)) {
            err = TXF("error.montage_clip_failed", (int)i + 1, clip.path.c_str(), TX("error.montage_probe").c_str());
            return Result::Failed;
        }
        audio[i] = hasAudio ? 1 : 0;
    }

    // --- Assemblage : transitions (xfade, concat) sur l'image et le son, puis mise a l'echelle, texte, fondus, interpolation, format ---
    H.log(L"  " + TX("log.assembling"));
    std::wstring graph;
    for (size_t i = 0; i < n; i++) graph += wfmt(L"[%d:v]settb=AVTB[s%d];", (int)i, (int)i);  // meme base de temps pour tous les clips (xfade l'exige)
    std::wstring cur = L"[s0]";
    double total = durations[0];
    for (size_t i = 1; i < n; i++) {
        const MontageClip& c = def.clips[i];
        const std::wstring label = wfmt(L"[v%d]", (int)i);
        const double d = std::min(c.transitionDuration, std::min(durations[i], total) * 0.5);
        if (c.transition == "cut" || d <= 0.01) {
            graph += cur + wfmt(L"[s%d]concat=n=2:v=1:a=0,settb=AVTB", (int)i) + label + L";";
            total += durations[i];
        } else {
            graph += cur + wfmt(L"[s%d]xfade=transition=%ls:duration=%.4f:offset=%.4f", (int)i, xfadeName(c.transition).c_str(), d, total - d) + label + L";";
            total += durations[i] - d;
        }
        cur = label;
    }
    const double fi = std::max(0.0, std::min(s.fadein, total)), fo = std::max(0.0, std::min(s.fadeout, total));
    std::wstring tail = outScaleFilter(s, s.width, s.height, OW, OH) + overlay;
    if (fi > 0) tail += wfmt(L"fade=t=in:st=0:d=%g,", fi);
    if (fo > 0) tail += wfmt(L"fade=t=out:st=%.4f:d=%g,", total - fo, fo);
    tail += interpFilter(s);
    const std::string enc = resolveEncoder(s, ffmpeg, H);
    graph += cur + tail + outFormat(s, enc) + L"[vout]";

    bool anyAudio = false;
    for (size_t i = 0; i < n; i++) anyAudio = anyAudio || audio[i];
    const bool globalAudio = !s.audio.empty();
    if (globalAudio && !fileExists(s.audio)) {
        err = TXF("error.audio_missing", s.audio.c_str());
        return Result::Failed;
    }
    std::wstring audioIn;
    std::wstring audioMap;
    if (globalAudio || anyAudio) {
        std::wstring chain;
        std::wstring agraph;  // la partie son du graphe (sans la normalisation ni les fondus), reprise telle quelle pour mesurer le volume
        if (globalAudio) {
            chain = wfmt(L"[%d:a]", (int)n);  // la bande-son du reglage « audio » couvre tout le montage
        } else {
            std::wstring ac;
            for (size_t i = 0; i < n; i++) {  // un clip sans son recoit du silence de sa duree
                const std::wstring lab = wfmt(L"[a%d]", (int)i);
                if (audio[i]) agraph += wfmt(L";[%d:a]aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo", (int)i) + lab;
                else agraph += wfmt(L";anullsrc=r=48000:cl=stereo:d=%.4f,aformat=sample_fmts=fltp", durations[i]) + lab;
            }
            std::wstring acur = L"[a0]";
            for (size_t i = 1; i < n; i++) {
                const MontageClip& c = def.clips[i];
                const std::wstring label = wfmt(L"[ac%d]", (int)i);
                const double d = std::min(c.transitionDuration, std::min(durations[i], durations[i - 1]) * 0.5);
                if (c.transition == "cut" || d <= 0.01) agraph += L";" + acur + wfmt(L"[a%d]concat=n=2:v=0:a=1", (int)i) + label;
                else agraph += L";" + acur + wfmt(L"[a%d]acrossfade=d=%.4f:c1=tri:c2=tri", (int)i, d) + label;
                acur = label;
            }
            chain = acur;
        }
        if (globalAudio) audioIn = wfmt(L" -ss %g", s.start) + L" -i \"" + s.audio + L"\"";
        // Normalisation du son : une premiere passe mesure le son du montage, puis un gain FIXE l'amene a la cible (le filtre loudnorm de ffmpeg en
        // une passe change son gain pendant la lecture : le volume monte et descend avec les passages doux et forts de la musique).
        LoudnormMeasure loud;
        if (s.loudnorm != 0) {
            std::wstring mcmd = L"\"" + ffmpeg + L"\" -hide_banner -nostats -nostdin";
            for (const auto& f : files) mcmd += L" -i \"" + f + L"\"";
            mcmd += audioIn + L" -filter_complex \"" + (agraph.empty() ? std::wstring() : agraph.substr(1) + L";") + chain + wfmt(L"atrim=end=%.4f,", total) +
                    loudnormMeasureFilter(s) + L"[aout]\" -map \"[aout]\" -f null -";
            std::string rawMeasure;
            measureLoudnorm(mcmd, loud, &rawMeasure);
            if (loud.ok) {
                bool limited = false;
                H.log(L"  " + TXF("log.loudnorm_measured", loud.inputI, loudnormGainDb(s, loud, limited)));
            } else {
                H.log(L"  " + TX("log.loudnorm_unmeasured"));
                const std::string tail = rawMeasure.size() > 600 ? rawMeasure.substr(rawMeasure.size() - 600) : rawMeasure;
                H.log(L"  ffmpeg: " + W(tail));
            }
        }
        std::wstring af = loudnormFilter(s, &loud);
        if (fi > 0) af += wfmt(L"afade=t=in:st=0:d=%g,", fi);
        if (fo > 0) af += wfmt(L"afade=t=out:st=%.4f:d=%g,", total - fo, fo);
        graph += agraph + L";" + chain + af + L"apad[aout]";
        audioMap = L" -map \"[aout]\"" + audioCodecArgs(s.container) + L" -shortest";
    }
    const ColorSpec cs = colorSpecFor(s);
    std::wstring title = montagePath.substr(montagePath.find_last_of(L"\\/") == std::wstring::npos ? 0 : montagePath.find_last_of(L"\\/") + 1);
    if (const size_t dot = title.find_last_of(L'.'); dot != std::wstring::npos && dot > 0) title.erase(dot);
    title.erase(std::remove(title.begin(), title.end(), L'"'), title.end());
    std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -y";
    for (const auto& f : files) cmd += L" -i \"" + f + L"\"";
    cmd += audioIn + L" -filter_complex \"" + graph + L"\" -map \"[vout]\"" + audioMap + encoderArgs(s, enc) + outPixFmtArg(s, enc) + L" " + cs.tags +
           containerArgs(s.container);
    if (s.metadata) cmd += L" -metadata \"title=" + title + L"\" -metadata \"comment=" + wfmt(L"%dx%d, %g fps, ", OW, OH, s.interp > 0 ? s.interp : s.fps) +
                           APP_NAME_WSTR + L" " + APP_VER_WSTR + L"\"";
    cmd += L" \"" + out + L"\"";

    auto proc = platform::Process::start(platform::splitCommand(cmd), false, true);
    if (!proc) {
        err = TX("error.ffmpeg_launch");
        return Result::Failed;
    }
    std::atomic<bool> finished{false};
    std::thread watcher([&] {  // annulation : on arrete ffmpeg
        while (!finished) {
            if (H.cancelled()) {
                proc->terminate();
                return;
            }
            platform::sleepMs(100);
        }
    });
    std::thread reader([&] {
        std::string acc;
        char buf[4096];
        size_t got;
        while ((got = proc->readOutput(buf, sizeof(buf))) > 0) {
            acc.append(buf, got);
            size_t p;
            while ((p = acc.find('\n')) != std::string::npos) {
                const std::string line = trim(acc.substr(0, p));
                acc.erase(0, p + 1);
                if (!line.empty()) H.log(TX("log.ffmpeg_prefix") + W(line));
            }
        }
    });
    reader.join();
    const int rc = proc->wait();
    finished = true;
    watcher.join();
    if (H.cancelled()) {
        platform::removeFile(out);
        return Result::Cancelled;
    }
    if (rc != 0) {
        platform::removeFile(out);
        err = TX("error.assemble_failed");
        return Result::Failed;
    }
    // clips intermediaires : supprimes une fois le montage fini
    for (size_t i = 0; i < n; i++) {
        const std::wstring name = wfmt(L"clip%03d", (int)i + 1);
        platform::removeFile(files[i]);
        platform::removeFile(work + sep + name + L".sig");
    }
    std::vector<platform::DirEntry> left;
    platform::listDir(work, left);
    for (const auto& en : left)
        if (en.isDir) {  // restes de parties reprenables d'un clip
            std::wstring sub = work + sep + en.name;
            std::vector<platform::DirEntry> inner;
            platform::listDir(sub, inner);
            for (const auto& f : inner) platform::removeFile(sub + sep + f.name);
            platform::removeDir(sub);
        } else {
            platform::removeFile(work + sep + en.name);
        }
    platform::removeDir(work);
    if (rhythmOk && s.markers != "off")
        applyMarkerSetting(s.markers, rhythm, ffmpeg, out, s.start, total, 1.0, s.interp > 0 ? s.interp : s.fps, [&](const std::wstring& l) { H.log(L"  " + l); });
    H.log(L"  " + TXF("log.ok_output", out.c_str()));
    if (H.output) H.output(out);
    return Result::Ok;
}
