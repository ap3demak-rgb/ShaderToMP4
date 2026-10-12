// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "report.h"
#include "app_info.h"
#include "i18n.h"
#include "util.h"
#include "platform/platform.h"

namespace {

std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += (char)c;
                }
        }
    }
    return o + "\"";
}

std::string jnum(double v) {
    char b[48];
    snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

std::wstring sizeText(uint64_t bytes) {
    if (bytes >= (1ull << 30)) return i18n::fixed((double)bytes / (1ull << 30), 2) + L" GB";
    if (bytes >= (1ull << 20)) return i18n::fixed((double)bytes / (1ull << 20), 1) + L" MB";
    return i18n::fixed((double)bytes / 1024.0, 0) + L" KB";
}

}  // namespace

double reportRenderFps(const RenderReport& r) { return r.loopSeconds > 0.05 && r.frames > 0 ? r.frames / r.loopSeconds : 0.0; }

double reportWaitPercent(const RenderReport& r) { return r.loopSeconds > 1.0 ? 100.0 * r.waitSeconds / r.loopSeconds : 0.0; }

// Meme regle que le journal : le rendu a attendu l'encodeur plus de 30 % du temps -> l'encodeur est le goulot
std::string reportBottleneck(const RenderReport& r) {
    if (r.loopSeconds <= 1.0) return "";
    return r.waitSeconds > 0.3 * r.loopSeconds ? "encoder" : "gpu";
}

bool isWarningLine(const std::wstring& line) {
    const std::string words = U8(TX("log.words.warn"));
    size_t pos = 0;
    const std::string text = U8(line);
    while (pos <= words.size()) {
        size_t e = words.find('|', pos);
        if (e == std::string::npos) e = words.size();
        const std::string w = words.substr(pos, e - pos);
        pos = e + 1;
        if (!w.empty() && text.find(w) != std::string::npos) return true;
    }
    return false;
}

std::string reportJson(const RenderReport& r) {
    std::string o = "{\n";
    auto field = [&](const std::string& k, const std::string& v, bool last = false) { o += "  \"" + k + "\": " + v + (last ? "\n" : ",\n"); };
    field("version", jstr(APP_VER_STR));
    field("shader", jstr(U8(r.shader)));
    field("output", jstr(U8(r.output)));
    field("status", jstr(r.status));
    if (!r.error.empty()) field("error", jstr(U8(r.error)));
    field("width", std::to_string(r.width));
    field("height", std::to_string(r.height));
    field("fps", jnum(r.fps));
    field("frames", std::to_string(r.frames));
    field("media_seconds", jnum(r.mediaSeconds));
    field("wall_seconds", jnum(r.wallSeconds));
    field("render_fps", jnum(reportRenderFps(r)));
    field("realtime_factor", jnum(r.wallSeconds > 0 ? r.mediaSeconds / r.wallSeconds : 0.0));
    const std::string bn = reportBottleneck(r);
    field("bottleneck", bn.empty() ? "null" : jstr(bn));
    field("encoder_wait_percent", jnum(reportWaitPercent(r)));
    field("bytes", std::to_string(r.bytes));
    field("codec", jstr(r.codec));
    field("container", jstr(r.container));
    field("encoder", jstr(r.encoder));
    if (r.gpuSamples > 0)
        field("gpu_ms", "{\"image\": " + jnum(r.gpuImageMs) + ", \"buffers\": " + jnum(r.gpuBufferMs) + ", \"post\": " + jnum(r.gpuPostMs) + ", \"sampled_frames\": " + std::to_string(r.gpuSamples) + "}");
    else
        field("gpu_ms", "null");
    if (r.loopChecked)
        field("loop", "{\"seam\": " + jnum(r.loopSeam) + ", \"typical\": " + jnum(r.loopTypical) + ", \"perfect\": " + (r.loopPerfect ? "true" : "false") + "}");
    else
        field("loop", "null");
    o += "  \"warnings\": [";
    for (size_t i = 0; i < r.warnings.size(); i++) o += (i ? ", " : "") + jstr(U8(r.warnings[i]));
    o += "]\n}\n";
    return o;
}

std::string reportText(const RenderReport& r) {
    std::wstring o = std::wstring(APP_NAME_WSTR) + L" " + APP_VER_WSTR + L" - " + TX("log.report.title") + L"\n";
    auto line = [&](const char* key, const std::wstring& v) { o += TX(key) + L": " + v + L"\n"; };
    line("log.report.shader", r.shader);
    line("log.report.output", r.output.empty() ? L"-" : r.output);
    line("log.report.status", TX(r.status == "ok" ? "log.report.status_ok" : r.status == "cancelled" ? "log.report.status_cancelled" : "log.report.status_failed"));
    if (!r.error.empty()) line("log.report.error", r.error);
    if (r.frames > 0)
        line("log.report.frames", TXF("log.report.frames_value", r.frames, r.width, r.height, i18n::num(r.fps).c_str(), i18n::fixed(r.mediaSeconds, 1).c_str()));
    line("log.report.time", TXF("log.report.time_value", i18n::fixed(r.wallSeconds, 1).c_str(), i18n::fixed(reportRenderFps(r), 1).c_str(),
                           i18n::fixed(r.wallSeconds > 0 ? r.mediaSeconds / r.wallSeconds : 0.0, 2).c_str()));
    const std::string bn = reportBottleneck(r);
    if (!bn.empty()) line("log.report.bottleneck", TXF(bn == "encoder" ? "log.report.bn_encoder" : "log.report.bn_gpu", i18n::fixed(reportWaitPercent(r), 0).c_str()));
    if (r.bytes > 0) line("log.report.bytes", sizeText(r.bytes));
    if (!r.container.empty()) line("log.report.format", W(r.codec) + L" / " + W(r.container) + L" / " + W(r.encoder));
    if (r.gpuSamples > 0)
        line("log.report.gpu", TXF("log.report.gpu_value", i18n::fixed(r.gpuImageMs, 2).c_str(), i18n::fixed(r.gpuBufferMs, 2).c_str(), i18n::fixed(r.gpuPostMs, 2).c_str(), r.gpuSamples));
    if (r.loopChecked)
        line("log.report.loop", TXF(r.loopPerfect ? "log.report.loop_perfect" : "log.report.loop_visible", i18n::fixed(r.loopSeam, 2).c_str(), i18n::fixed(r.loopTypical, 2).c_str()));
    o += TX("log.report.warnings") + L":";
    if (r.warnings.empty()) o += L" " + TX("log.report.none") + L"\n";
    else {
        o += L"\n";
        for (const auto& w : r.warnings) o += L"  - " + w + L"\n";
    }
    return U8(o);
}

bool writeRenderReport(const RenderReport& r, const std::wstring& path, const std::string& format) {
    const std::string text = format == "text" ? reportText(r) : reportJson(r);
    FILE* f = platform::openFile(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    return ok;
}
