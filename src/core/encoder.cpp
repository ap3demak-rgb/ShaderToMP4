// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "encoder.h"
#include "formats.h"
#include "i18n.h"
#include "process.h"
#include "util.h"
#include <cmath>

ColorSpec colorSpec(const std::string& c) {
    if (c == "bt709full")
        return {L"pc", L"bt709", L"-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range pc"};
    if (c == "srgb")
        return {L"tv", L"bt709", L"-colorspace bt709 -color_primaries bt709 -color_trc iec61966-2-1 -color_range tv"};
    if (c == "bt601")
        return {L"tv", L"bt601", L"-colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv"};
    return {L"tv", L"bt709", L"-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv"};
}

ColorSpec colorSpecFor(const Settings& s) {
    if (s.hdr == "pq")
        return {L"tv", L"bt2020", L"-colorspace bt2020nc -color_primaries bt2020 -color_trc smpte2084 -color_range tv"};
    if (s.hdr == "hlg")
        return {L"tv", L"bt2020", L"-colorspace bt2020nc -color_primaries bt2020 -color_trc arib-std-b67 -color_range tv"};
    return colorSpec(s.color);
}

std::wstring outScaleFilter(const Settings& s, int w, int h, int ow, int oh) {
    if (ow == w && oh == h) return L"";
    return wfmt(L"scale=%d:%d:flags=%ls,", ow, oh, W(s.scaleFilter).c_str());
}

std::wstring interpFilter(const Settings& s) {
    if (s.interp <= 0) return L"";
    // minterpolate ne travaille qu'en 8 bits ; la sortie est reconvertie ensuite au format demande
    return wfmt(L"format=yuv420p,minterpolate=fps=%g:mi_mode=mci:mc_mode=aobmc:me_mode=bidir:vsbmc=1,", s.interp);
}

namespace {
constexpr double kLoudnormTP = -1.5;       // crete vraie maximale (dBTP)
constexpr double kLoudnormMaxLimited = 3;  // dB de crete que le limiteur peut rattraper
}  // namespace

std::wstring loudnormMeasureFilter(const Settings& s) {
    return wfmt(L"loudnorm=I=%g:TP=%g:LRA=11:print_format=json", s.loudnorm, kLoudnormTP);
}

bool parseLoudnormMeasure(const std::string& text, LoudnormMeasure& m) {
    m = LoudnormMeasure();
    // lignes du bloc JSON : "input_i" : "-27.78",
    auto value = [&](const char* key, double& v) {
        const size_t k = text.rfind(std::string("\"") + key + "\"");
        if (k == std::string::npos) return false;
        const size_t q = text.find('"', text.find(':', k) + 1);
        if (q == std::string::npos) return false;
        char* end = nullptr;
        v = strtod(text.c_str() + q + 1, &end);
        return end != text.c_str() + q + 1 && std::isfinite(v);  // "-inf" : son silencieux
    };
    double i = 0, tp = 0;
    if (!value("input_i", i) || !value("input_tp", tp)) return false;
    if (i <= -69.0) return false;  // sous le seuil de mesure : rien a normaliser
    m.inputI = i;
    m.inputTP = tp;
    m.ok = true;
    return true;
}

bool measureLoudnorm(const std::wstring& cmd, LoudnormMeasure& m, std::string* raw) {
    std::string all;
    runProcess(cmd, [&](const std::wstring& l) { all += U8(l) + "\n"; });
    if (raw) *raw = all;
    return parseLoudnormMeasure(all, m);
}

double loudnormGainDb(const Settings& s, const LoudnormMeasure& m, bool& limited) {
    double gain = s.loudnorm - m.inputI;
    limited = false;
    const double room = kLoudnormTP - m.inputTP;  // gain possible sans depasser la crete vraie
    if (gain > room) {
        limited = true;
        gain = std::min(gain, room + kLoudnormMaxLimited);
    }
    return gain;
}

std::wstring loudnormFilter(const Settings& s, const LoudnormMeasure* m) {
    if (s.loudnorm == 0) return L"";
    if (m && m->ok) {
        bool limited = false;
        const double gain = loudnormGainDb(s, *m, limited);
        std::wstring f = wfmt(L"volume=%.3fdB,", gain);
        if (limited) f += wfmt(L"alimiter=limit=%.4f:level=disabled,", std::pow(10.0, kLoudnormTP / 20.0));
        return f;
    }
    return L"";  // pas de mesure : aucune normalisation (le filtre loudnorm en une passe ferait monter et descendre le volume)
}

// Parametres statiques HDR10 du flux (volume d'affichage BT.2020 et luminances declarees) pour x265 / SVT-AV1.
static std::wstring x265Hdr(const Settings& s) {
    if (s.hdr == "pq")
        return wfmt(L":hdr10=1:hdr10-opt=1:repeat-headers=1:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc"
                    L":master-display=G(8500,39850)B(6550,2300)R(35400,14600)WP(15635,16450)L(%d,1):max-cll=%d,%d",
                    s.hdrPeak * 10000, s.hdrPeak, std::min(s.hdrFall, s.hdrPeak));
    if (s.hdr == "hlg") return L":repeat-headers=1:colorprim=bt2020:transfer=arib-std-b67:colormatrix=bt2020nc";
    return L"";
}
static bool g_svtHdrParams = true;  // ce ffmpeg accepte mastering-display / content-light de SVT-AV1 (sonde de resolveEncoder)

static const char* kPresetNames[] = {"ultrafast", "superfast", "veryfast", "faster", "fast",
                                     "medium",    "slow",      "slower",   "veryslow"};


static int presetIndex(const std::string& p) {
    for (int i = 0; i < 9; i++)
        if (p == kPresetNames[i]) return i;
    return 6;
}

// Nom de l'encodeur materiel ffmpeg pour ce codec ("" : pas d'encodeur materiel de ce type).
static std::string hwCodecName(const std::string& enc, const std::string& codec) {
    const char* fam = codec == "h264" ? "h264" : codec == "h265" ? "hevc" : codec == "av1" ? "av1" : nullptr;
    if (!fam) return "";
    if (enc == "videotoolbox" && codec == "av1") return "";
    return std::string(fam) + "_" + enc;
}

// VAAPI travaille sur des images GPU (nv12) : il faut un peripherique et un envoi (hwupload) avant l'encodeur.
static const wchar_t* kVaapiDevice = L" -vaapi_device /dev/dri/renderD128";

std::wstring encoderFormat(const std::string& enc) { return enc == "vaapi" ? L"format=nv12,hwupload" : L"format=yuv420p"; }
std::wstring encoderPixFmt(const std::string& enc) { return enc == "vaapi" ? L"" : L" -pix_fmt yuv420p"; }

// Un encodeur materiel n'est utilisable que si le PC l'accepte : on le verifie par un mini-encodage de test.
static bool probeEncoder(const std::wstring& ffmpeg, const std::string& enc, const std::string& codec) {
    static std::map<std::string, bool> cache;
    const std::string vcodec = hwCodecName(enc, codec);
    if (vcodec.empty()) return false;
    auto it = cache.find(vcodec);
    if (it != cache.end()) return it->second;
    const std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin" + (enc == "vaapi" ? kVaapiDevice : L"") +
                             L" -f lavfi -i nullsrc=s=256x256:d=0.2 -vf " + encoderFormat(enc) + encoderPixFmt(enc) + L" -c:v " +
                             W(vcodec) + L" -f null -";
    bool ok = runProcess(cmd, nullptr) == 0;
    cache[vcodec] = ok;
    return ok;
}

const wchar_t* codecLabel(const std::string& codec) { return codecAt(codecIndex(codec)).label; }

std::wstring audioCodecArgs(const std::string& container) {
    return container == "webm" ? L" -c:a libopus -b:a 160k" : L" -c:a aac -b:a 192k";
}

std::wstring containerArgs(const std::string& container) {
    return container == "mp4" || container == "mov" ? L" -movflags +faststart" : L"";
}

// Teste si ffmpeg sait encoder avec une bibliotheque logicielle donnee (mini-encodage de test, mis en cache).
static bool probeSoftware(const std::wstring& ffmpeg, const char* lib) {
    static std::map<std::string, bool> cache;
    auto it = cache.find(lib);
    if (it != cache.end()) return it->second;
    const std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin" +
                             L" -f lavfi -i nullsrc=s=256x256:d=0.2 -pix_fmt yuv420p -c:v " + W(lib) + L" -f null -";
    bool ok = runProcess(cmd, nullptr) == 0;
    cache[lib] = ok;
    return ok;
}

// Encodeur logiciel : "cpu", ou "cpu-aom" pour l'AV1 quand ffmpeg n'a pas SVT-AV1.
static std::string softwareEncoder(const Settings& s, const std::wstring& ffmpeg) {
    if (s.codec == "av1" && !probeSoftware(ffmpeg, "libsvtav1") && probeSoftware(ffmpeg, "libaom-av1")) return "cpu-aom";
    return "cpu";
}

// SVT-AV1 accepte-t-il les parametres HDR10 statiques ? (mini-encodage de test, mis en cache ; les anciennes versions les refusent)
static bool probeSvtHdr(const std::wstring& ffmpeg) {
    static std::map<std::wstring, bool> cache;
    auto it = cache.find(ffmpeg);
    if (it != cache.end()) return it->second;
    const std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -f lavfi -i nullsrc=s=256x256:d=0.2 -pix_fmt yuv420p10le"
                             L" -c:v libsvtav1 -svtav1-params mastering-display=G(0.17,0.797)B(0.131,0.046)R(0.708,0.292)WP(0.3127,0.329)L(1000,0.0001):content-light=1000,400"
                             L" -f null -";
    const bool ok = runProcess(cmd, nullptr) == 0;
    cache[ffmpeg] = ok;
    return ok;
}

std::string resolveEncoder(const Settings& s, const std::wstring& ffmpeg, const Hooks& H) {
    if (s.hdr != "off" && s.codec == "av1") {
        g_svtHdrParams = probeSvtHdr(ffmpeg);
        if (!g_svtHdrParams) H.log(L"  " + TX("log.hdr_av1_tags"));
    }
    if (s.codec != "h264" && s.codec != "h265" && s.codec != "av1") {  // VP9, ProRes, DNxHR : logiciel seulement
        if (s.encoder != "cpu") H.log(L"  " + TXF("log.codec_cpu_only", codecLabel(s.codec)));
        return "cpu";
    }
    if (s.encoder == "cpu") return softwareEncoder(s, ffmpeg);
    if (s.depth >= 10) {  // les encodeurs materiels sont utilises en 8 bits seulement
        H.log(L"  " + TX("log.depth_cpu_only"));
        return softwareEncoder(s, ffmpeg);
    }
    std::vector<std::string> cands;
    if (s.encoder == "auto") {
#if defined(_WIN32)
        cands = {"nvenc", "amf", "qsv"};
#elif defined(__APPLE__)
        cands = {"videotoolbox"};
#else
        cands = {"nvenc", "vaapi", "qsv"};
#endif
    } else {
        cands = {s.encoder};
    }
    for (const auto& c : cands) {
        if (probeEncoder(ffmpeg, c, s.codec)) {
            H.log(L"  " + TXF("log.hw_encoder", W(hwCodecName(c, s.codec)).c_str()));
            return c;
        }
    }
    if (s.encoder != "auto") H.log(L"  " + TXF("log.encoder_unavailable", W(s.encoder).c_str()));
    return softwareEncoder(s, ffmpeg);
}

std::wstring encoderArgs(const Settings& s, const std::string& enc) {
    const bool h265 = s.codec == "h265";
    std::wstring a;
    if (isProRes(s.codec)) {  // profils 0 (Proxy) a 4 (4444) ; debit fixe par profil, CRF et debit ignores
        const int prof = s.codec == "prores_proxy" ? 0 : s.codec == "prores_lt" ? 1 : s.codec == "prores_422" ? 2 : s.codec == "prores_hq" ? 3 : 4;
        a = wfmt(L" -c:v prores_ks -profile:v %d -vendor apl0", prof);
    } else if (isDnxhr(s.codec)) {
        a = std::wstring(L" -c:v dnxhd -profile:v ") + (s.codec == "dnxhr_sq" ? L"dnxhr_sq" : s.codec == "dnxhr_hqx" ? L"dnxhr_hqx" :
                                                         s.codec == "dnxhr_444" ? L"dnxhr_444" : L"dnxhr_hq");
    } else if (s.codec == "vp9") {  // CRF 0-63 : decale par rapport a x264 ; cpu-used 0 (lent) a 8 (rapide)
        a = wfmt(L" -c:v libvpx-vp9 -row-mt 1 -deadline good -cpu-used %d", std::min(5, 8 - presetIndex(s.preset))) +
            (s.bitrate > 0 ? wfmt(L" -b:v %dk", s.bitrate) : wfmt(L" -crf %d -b:v 0", std::min(63, s.crf + 9)));
        if (s.alpha) a += L" -auto-alt-ref 0";  // libvpx ne code le canal alpha qu'avec les images de reference alternatives coupees
    } else if (s.codec == "av1" && enc == "cpu-aom") {
        a = wfmt(L" -c:v libaom-av1 -row-mt 1 -cpu-used %d", std::min(8, 8 - presetIndex(s.preset) + 1)) +
            (s.bitrate > 0 ? wfmt(L" -b:v %dk", s.bitrate) : wfmt(L" -crf %d -b:v 0", std::min(63, s.crf + 7)));
    } else if (s.codec == "av1") {  // SVT-AV1 : preset 0 (lent) a 12 (rapide)
        a = wfmt(L" -c:v libsvtav1 -preset %d", 12 - presetIndex(s.preset)) +
            (s.bitrate > 0 ? wfmt(L" -b:v %dk", s.bitrate) : wfmt(L" -crf %d", std::min(63, s.crf + 7)));
        if (s.hdr != "off" && g_svtHdrParams)  // les etiquettes de couleur sont ajoutees par ffmpeg ; ici le volume d'affichage et les luminances
            a += s.hdr == "pq" ? wfmt(L" -svtav1-params mastering-display=G(0.17,0.797)B(0.131,0.046)R(0.708,0.292)WP(0.3127,0.329)L(%d,0.0001):content-light=%d,%d",
                                      s.hdrPeak, s.hdrPeak, std::min(s.hdrFall, s.hdrPeak))
                               : std::wstring();
    } else if (enc == "cpu") {
        a = std::wstring(L" -c:v ") + (h265 ? L"libx265" : L"libx264") + L" -preset " + W(s.preset) +
            (s.bitrate > 0 ? wfmt(L" -b:v %dk", s.bitrate) : wfmt(L" -crf %d", s.crf));
        if (h265) a += L" -tag:v hvc1 -x265-params log-level=error" + x265Hdr(s);
    } else if (enc == "nvenc") {
        static const int kMap[] = {1, 2, 3, 4, 4, 5, 6, 7, 7};
        a = std::wstring(L" -c:v ") + W(hwCodecName(enc, s.codec)) + wfmt(L" -preset p%d", kMap[presetIndex(s.preset)]) +
            (s.bitrate > 0 ? wfmt(L" -rc vbr -b:v %dk", s.bitrate) : wfmt(L" -rc vbr -cq %d -b:v 0", s.crf));
        if (h265) a += L" -tag:v hvc1";
    } else if (enc == "amf") {
        a = std::wstring(L" -c:v ") + W(hwCodecName(enc, s.codec)) + L" -quality quality" +
            (s.bitrate > 0 ? wfmt(L" -rc vbr_peak -b:v %dk", s.bitrate)
                           : wfmt(L" -rc cqp -qp_i %d -qp_p %d -qp_b %d", s.crf, s.crf, s.crf));
        if (h265) a += L" -tag:v hvc1";
    } else if (enc == "vaapi") {
        a = std::wstring(kVaapiDevice) + L" -c:v " + W(hwCodecName(enc, s.codec)) +
            (s.bitrate > 0 ? wfmt(L" -b:v %dk", s.bitrate) : wfmt(L" -rc_mode CQP -global_quality %d", s.crf));
        if (h265) a += L" -tag:v hvc1";
    } else if (enc == "videotoolbox") {
        const int q = std::min(100, std::max(1, 100 - s.crf * 2));  // CRF 12 -> 76, CRF 23 -> 54
        a = std::wstring(L" -c:v ") + W(hwCodecName(enc, s.codec)) + (s.bitrate > 0 ? wfmt(L" -b:v %dk", s.bitrate) : wfmt(L" -q:v %d", q));
        if (h265) a += L" -tag:v hvc1";
    } else {  // qsv
        static const char* kQsv[] = {"veryfast", "veryfast", "veryfast", "faster", "fast",
                                     "medium",   "slow",     "veryslow", "veryslow"};
        a = std::wstring(L" -c:v ") + W(hwCodecName(enc, s.codec)) + L" -preset " + W(kQsv[presetIndex(s.preset)]) +
            (s.bitrate > 0 ? wfmt(L" -b:v %dk", s.bitrate) : wfmt(L" -global_quality %d", s.crf));
        if (h265) a += L" -tag:v hvc1";
    }
    return a;
}

std::wstring outPixFmt(const Settings& s, const std::string& enc) {
    if (enc == "vaapi") return L"";
    if (s.alpha && s.codec == "prores_4444") return L"yuva444p10le";  // avec transparence
    if (s.alpha && s.codec == "vp9") return L"yuva420p";               // libvpx : alpha en 8 bits seulement
    if (s.codec == "prores_4444" || s.codec == "dnxhr_444") return L"yuv444p10le";
    if (isProRes(s.codec) || s.codec == "dnxhr_hqx") return L"yuv422p10le";
    if (isDnxhr(s.codec)) return L"yuv422p";
    return s.depth >= 10 ? L"yuv420p10le" : L"yuv420p";
}
std::wstring outFormat(const Settings& s, const std::string& enc) {
    return enc == "vaapi" ? std::wstring(L"format=nv12,hwupload") : L"format=" + outPixFmt(s, enc);
}
std::wstring outPixFmtArg(const Settings& s, const std::string& enc) {
    return enc == "vaapi" ? std::wstring() : L" -pix_fmt " + outPixFmt(s, enc);
}

static std::wstring imageFormatTail(const Settings& s, bool deep);

std::wstring imageFilterTail(const Settings& s, bool alphaModeFilter) {
    const bool deep = s.depth >= 10;
    // alpha premultiplie : la couleur est multipliee par l'alpha avant l'ecriture (PNG, TIFF, DPX) ; ffmpeg ne permet pas de le declarer dans ces fichiers
    const std::wstring premul = (s.alpha && s.alphaMode == "premult" && (s.container == "png" || s.container == "tiff" || s.container == "dpx")) ? (alphaModeFilter ? L"format=gbrap16le,premultiply=inplace=1,setparams=alpha_mode=straight," : L"format=gbrap16le,premultiply=inplace=1,") : L"";
    return premul + imageFormatTail(s, deep);
}

static std::wstring imageFormatTail(const Settings& s, bool deep) {
    if (s.container == "gif")  // palette optimisee calculee sur toute la sequence, tramage Bayer
        return L"format=rgb24,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle";
    if (s.container == "webp") return s.alpha ? L"format=yuva420p" : L"format=yuv420p";
    if (s.container == "png") return s.alpha ? (deep ? L"format=rgba64be" : L"format=rgba") : (deep ? L"format=rgb48be" : L"format=rgb24");
    if (s.container == "tiff") return s.alpha ? (deep ? L"format=rgba64le" : L"format=rgba") : (deep ? L"format=rgb48le" : L"format=rgb24");
    if (s.container == "dpx") {  // 10 bits par composante avec depth=10 ; l'espace de couleur (BT.709) est ecrit dans l'en-tete
        const std::wstring tags = L"setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709,";
        return tags + (s.alpha ? (deep ? L"format=rgba64le" : L"format=rgba") : (deep ? L"format=gbrp10le" : L"format=rgb24"));
    }
    return s.alpha ? L"format=gbrapf32le" : L"format=gbrpf32le";  // exr
}

std::wstring imageEncoderArgs(const Settings& s) {
    if (s.container == "gif") return L" -loop 0";
    if (s.container == "webp")
        return wfmt(L" -c:v libwebp_anim -lossless 0 -q:v %d -compression_level 4 -loop 0", std::min(100, std::max(1, 100 - s.crf * 2)));
    if (s.container == "png") return L" -c:v png -start_number 0";
    if (s.container == "tiff") return L" -c:v tiff -compression_algo deflate -start_number 0";
    if (s.container == "dpx") return L" -c:v dpx -start_number 0";
    return L" -c:v exr -compression zip16 -format half -start_number 0";
}
