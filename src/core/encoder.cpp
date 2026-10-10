#include "encoder.h"
#include "process.h"
#include "util.h"

ColorSpec colorSpec(const std::string& c) {
    if (c == "bt709full")
        return {L"pc", L"bt709", L"-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range pc"};
    if (c == "srgb")
        return {L"tv", L"bt709", L"-colorspace bt709 -color_primaries bt709 -color_trc iec61966-2-1 -color_range tv"};
    if (c == "bt601")
        return {L"tv", L"bt601", L"-colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv"};
    return {L"tv", L"bt709", L"-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv"};
}

static const char* kPresetNames[] = {"ultrafast", "superfast", "veryfast", "faster", "fast",
                                     "medium",    "slow",      "slower",   "veryslow"};


static int presetIndex(const std::string& p) {
    for (int i = 0; i < 9; i++)
        if (p == kPresetNames[i]) return i;
    return 6;
}

static const char* hwCodecName(const std::string& enc, bool h265) {
    if (enc == "nvenc") return h265 ? "hevc_nvenc" : "h264_nvenc";
    if (enc == "amf") return h265 ? "hevc_amf" : "h264_amf";
    return h265 ? "hevc_qsv" : "h264_qsv";
}


// Un encodeur materiel n'est utilisable que si le PC l'accepte : on le verifie par un mini-encodage de test.
static bool probeEncoder(const std::wstring& ffmpeg, const char* vcodec) {
    static std::map<std::string, bool> cache;
    auto it = cache.find(vcodec);
    if (it != cache.end()) return it->second;
    const std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -loglevel error -nostdin -f lavfi -i nullsrc=s=256x256:d=0.2"
                             L" -pix_fmt yuv420p -c:v " + W(vcodec) + L" -f null -";
    bool ok = runProcess(cmd, nullptr) == 0;
    cache[vcodec] = ok;
    return ok;
}

std::string resolveEncoder(const Settings& s, const std::wstring& ffmpeg, const Hooks& H) {
    if (s.encoder == "cpu") return "cpu";
    const bool h265 = s.codec == "h265";
    std::vector<std::string> cands;
    if (s.encoder == "auto") cands = {"nvenc", "amf", "qsv"};
    else cands = {s.encoder};
    for (const auto& c : cands) {
        if (probeEncoder(ffmpeg, hwCodecName(c, h265))) {
            H.log(L"  Encodeur materiel : " + W(hwCodecName(c, h265)));
            return c;
        }
    }
    if (s.encoder != "auto") H.log(L"  Encodeur " + W(s.encoder) + L" indisponible sur ce PC : repli sur le CPU.");
    return "cpu";
}

std::wstring encoderArgs(const Settings& s, const std::string& enc) {
    const bool h265 = s.codec == "h265";
    std::wstring a;
    if (enc == "cpu") {
        a = std::wstring(L" -c:v ") + (h265 ? L"libx265" : L"libx264") + L" -preset " + W(s.preset) +
            (s.bitrate > 0 ? wfmt(L" -b:v %dk", s.bitrate) : wfmt(L" -crf %d", s.crf));
        if (h265) a += L" -tag:v hvc1 -x265-params log-level=error";
    } else if (enc == "nvenc") {
        static const int kMap[] = {1, 2, 3, 4, 4, 5, 6, 7, 7};
        a = std::wstring(L" -c:v ") + W(hwCodecName(enc, h265)) + wfmt(L" -preset p%d", kMap[presetIndex(s.preset)]) +
            (s.bitrate > 0 ? wfmt(L" -rc vbr -b:v %dk", s.bitrate) : wfmt(L" -rc vbr -cq %d -b:v 0", s.crf));
        if (h265) a += L" -tag:v hvc1";
    } else if (enc == "amf") {
        a = std::wstring(L" -c:v ") + W(hwCodecName(enc, h265)) + L" -quality quality" +
            (s.bitrate > 0 ? wfmt(L" -rc vbr_peak -b:v %dk", s.bitrate)
                           : wfmt(L" -rc cqp -qp_i %d -qp_p %d -qp_b %d", s.crf, s.crf, s.crf));
        if (h265) a += L" -tag:v hvc1";
    } else {  // qsv
        static const char* kQsv[] = {"veryfast", "veryfast", "veryfast", "faster", "fast",
                                     "medium",   "slow",     "veryslow", "veryslow"};
        a = std::wstring(L" -c:v ") + W(hwCodecName(enc, h265)) + L" -preset " + W(kQsv[presetIndex(s.preset)]) +
            (s.bitrate > 0 ? wfmt(L" -b:v %dk", s.bitrate) : wfmt(L" -global_quality %d", s.crf));
        if (h265) a += L" -tag:v hvc1";
    }
    return a;
}
