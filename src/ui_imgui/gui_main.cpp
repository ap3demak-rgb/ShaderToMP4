// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Interface Linux / macOS : GLFW (fenêtre, entrées, glisser-déposer) + Dear ImGui + icônes SVG (NanoSVG).
#include "core/ffmpeg_download.h"
#include "core/i18n.h"
#include "core/util.h"
#include "platform/platform.h"
#include "platform/posix/glfw_bridge.h"
#include "ui_imgui/file_browser.h"
#include "ui_imgui/icons.h"
#include "ui_imgui/model.h"
#include "ui_imgui/widgets.h"

#include <cctype>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace ui {

namespace {

struct App {
    explicit App(const std::wstring& dir) : model(dir) {}
    Model model;
    FileBrowser browser;
    unsigned previewTex = 0;
    bool showAbout = false;
    std::wstring lastDir;
    std::vector<std::wstring> drops;
};

App* g_app = nullptr;

// --- Langue mémorisée (window.ini : lang=xx) --------------------------------------------------------------------
std::string readSavedLanguage(const std::wstring& dir) {
    std::string text;
    if (!readFile(dir + L"window.ini", text)) return "";
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.rfind("lang=", 0) == 0) return trim(line.substr(5));
    }
    return "";
}

void saveLanguage(const std::wstring& dir, const std::string& code) {
    if (FILE* f = platform::openFile(dir + L"window.ini", "wb")) {
        const std::string line = "lang=" + code + "\n";
        fwrite(line.data(), 1, line.size(), f);
        fclose(f);
    }
}

// --- PNG non compressé (captures d'écran de l'intégration continue) ---------------------------------------------
uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ d[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

void be32(std::string& s, uint32_t v) {
    for (int i = 3; i >= 0; i--) s += (char)(v >> (8 * i));
}

void pngChunk(std::string& out, const char* type, const std::string& data) {
    be32(out, (uint32_t)data.size());
    std::string body = std::string(type, 4) + data;
    out += body;
    be32(out, crc32((const uint8_t*)body.data(), body.size()));
}

bool writePng(const std::wstring& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::string raw;
    for (int y = 0; y < h; y++) {
        raw += '\0';
        raw.append((const char*)&rgba[(size_t)y * w * 4], (size_t)w * 4);
    }
    std::string z = "\x78\x01";
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z += (char)(pos + n >= raw.size() ? 1 : 0);
        z += (char)(n & 255);
        z += (char)(n >> 8);
        z += (char)(~n & 255);
        z += (char)((~n >> 8) & 255);
        z.append(raw, pos, n);
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);
    std::string png = "\x89PNG\r\n\x1a\n", ihdr;
    be32(ihdr, (uint32_t)w);
    be32(ihdr, (uint32_t)h);
    ihdr += std::string("\x08\x06\x00\x00\x00", 5);
    pngChunk(png, "IHDR", ihdr);
    pngChunk(png, "IDAT", z);
    pngChunk(png, "IEND", "");
    FILE* f = platform::openFile(path, "wb");
    if (!f) return false;
    fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return true;
}

void captureScreen(GLFWwindow* win, const std::string& path) {
    int w = 0, h = 0;
    glfwGetFramebufferSize(win, &w, &h);
    std::vector<uint8_t> px((size_t)w * h * 4), flipped(px.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; y++) memcpy(&flipped[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
    writePng(W(path), w, h, flipped);
}

// --- Dessin --------------------------------------------------------------------------------------------------------
std::vector<std::string> resItems() {
    std::vector<std::string> v;
    for (int i = 0; i < kResCount; i++) {
        std::string s = std::to_string(kRes[i].w) + "x" + std::to_string(kRes[i].h);
        if (kRes[i].tag) s += std::string(" (") + kRes[i].tag + ")";
        else if (kRes[i].key) s += " (" + T(kRes[i].key) + ")";
        v.push_back(s);
    }
    v.push_back(T("ui.res.custom"));
    return v;
}

std::string baseName(const std::wstring& path) {
    const size_t s = path.find_last_of(L"\\/");
    return U8(s == std::wstring::npos ? path : path.substr(s + 1));
}

unsigned stateColor(int st) {
    switch (st) {
        case ST_DONE: return pal::OK;
        case ST_FAILED: return pal::ERR;
        case ST_CANCELLED: return pal::WARN;
        case ST_RUNNING: return pal::ACCENT;
        default: return pal::MUTED;
    }
}

void openBrowser(App& a, BrowseMode mode, const char* titleKey, const std::vector<std::wstring>& exts, const std::wstring& name,
                 std::function<void(const std::vector<std::wstring>&)> done) {
    a.browser.open(mode, titleKey, a.lastDir.empty() ? platform::homeDir() : a.lastDir, exts, name, [&a, done](const std::vector<std::wstring>& r) {
        if (!r.empty()) {
            const std::wstring& p = r[0];
            const size_t s = p.find_last_of(L'/');
            if (s != std::wstring::npos) a.lastDir = platform::dirExists(p) ? p : p.substr(0, s);
        }
        done(r);
    });
}

void drawHeader(App& a, GLFWwindow* win) {
    (void)win;
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    icon("logo", S(40), 0xFFFFFF);
    ImGui::EndGroup();
    ImGui::SameLine(0, S(12));
    ImGui::BeginGroup();
    ImGui::PushFont(fontBold);
    ImGui::TextUnformatted(APP_NAME_STR);
    ImGui::PopFont();
    ImGui::TextColored(colv(pal::MUTED), "%s", T("ui.tagline").c_str());
    ImGui::EndGroup();

    const std::string code = lower(i18n::language());
    std::string up = code;
    for (auto& c : up) c = (char)toupper((unsigned char)c);
    const std::string langLabel = "=" + up;
    const float wLang = buttonWidth(langLabel.c_str(), true), wAbout = buttonWidth("=", true);
    ImGui::SameLine(avail - wLang - wAbout - S(8));
    if (button(langLabel.c_str(), "globe", 0, true, 0, "ui.tip.language")) ImGui::OpenPopup("##lang");
    ImGui::SameLine();
    if (button("=", "info", 0, true, 0, "ui.tip.about")) a.showAbout = true;
    if (ImGui::BeginPopup("##lang")) {
        for (const auto& l : i18n::available()) {
            std::string name = l;
            for (auto& c : name) c = (char)toupper((unsigned char)c);
            if (ImGui::Selectable(name.c_str(), l == code)) {
                i18n::setLanguage(l);
                saveLanguage(a.model.exeDir, l);
                a.model.refreshTexts();
            }
        }
        ImGui::EndPopup();
    }
}

void drawShaders(App& a, float height) {
    Model& m = a.model;
    const bool run = m.running();
    const std::string count = m.paths.size() == 1 ? TXF8("ui.files_one", 1) : TXF8("ui.files_many", (int)m.paths.size());
    if (beginCard("##shaders", "ui.card.shaders", "film", ImVec2(0, height), count)) {
        const float rowH = ImGui::GetFrameHeight() + S(4);
        float rowsW = 0;  // les boutons passent sur deux lignes si la carte est trop étroite (langues aux libellés longs)
        for (const char* k : {"ui.btn.add", "ui.btn.remove", "ui.btn.clear", "ui.btn.folder", "ui.btn.png"})
            rowsW += buttonWidth(k, true) + ImGui::GetStyle().ItemSpacing.x;
        const int btnRows = rowsW > ImGui::GetContentRegionAvail().x ? 2 : 1;
        ImGui::BeginChild("##list", ImVec2(0, -(rowH * btnRows + ImGui::GetStyle().ItemSpacing.y * btnRows + S(4))), ImGuiChildFlags_None);
        if (m.paths.empty()) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, avail.y * 0.5f - S(50)));
            const std::string t1 = T("ui.drop.title"), t2 = T("ui.drop.hint");
            ImGui::SetCursorPosX((avail.x - S(40)) * 0.5f);
            icon("drop", S(40), pal::FAINT);
            ImGui::SetCursorPosX(std::max(0.0f, (avail.x - ImGui::CalcTextSize(t1.c_str()).x) * 0.5f));
            ImGui::TextUnformatted(t1.c_str());
            ImGui::SetCursorPosX(std::max(0.0f, (avail.x - ImGui::CalcTextSize(t2.c_str()).x) * 0.5f));
            ImGui::TextColored(colv(pal::MUTED), "%s", t2.c_str());
        }
        for (int i = 0; i < (int)m.paths.size(); i++) {
            ImGui::PushID(i);
            if (ImGui::Selectable(baseName(m.paths[i]).c_str(), i == m.selected, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetTextLineHeight() + S(6))))
                if (!run) m.selected = i;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s", U8(m.paths[i]).c_str());
            }
            const std::string st = U8(m.stateText[i]);
            const ImVec2 mx = ImGui::GetItemRectMax(), mn = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(mx.x - ImGui::CalcTextSize(st.c_str()).x - S(8), mn.y + S(3)), col(stateColor(m.state[i])),
                                                st.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        const bool any = !m.paths.empty();
        const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        auto fit = [rowRight](float w) {
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <= rowRight) ImGui::SameLine();
        };
        if (button("ui.btn.add", "add", 0, !run, 0, "ui.tip.add"))
            openBrowser(a, BrowseMode::OpenFiles, "dialog.shaders", {L".glsl", L".txt"}, L"", [&a](const std::vector<std::wstring>& r) { a.model.addFiles(r); });
        fit(buttonWidth("ui.btn.remove", true));
        if (button("ui.btn.remove", "trash", 0, !run && any, 0, "ui.tip.remove")) m.removeSelected();
        fit(buttonWidth("ui.btn.clear", true));
        if (button("ui.btn.clear", "clear", 0, !run && any, 0, "ui.tip.clear")) m.clearList();
        fit(buttonWidth("ui.btn.folder", true));
        if (button("ui.btn.folder", "folder", 0, any, 0, "ui.tip.folder")) m.openOutputFolder();
        fit(buttonWidth("ui.btn.png", true));
        if (button("ui.btn.png", "image", 0, !run && any, 0, "ui.tip.png")) m.startJobs(true);
    }
    endCard();
}

void drawPreview(App& a, float height) {
    Model& m = a.model;
    if (m.prevFresh) {
        a.previewTex = uploadBGRA(a.previewTex, m.prevW, m.prevH, m.prevPixels.data());
        m.prevFresh = false;
    }
    if (beginCard("##preview", "ui.card.preview", "eye", ImVec2(0, height))) {
        const float controls = ImGui::GetFrameHeight() + S(14);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 box(avail.x, std::max(S(40), avail.y - controls));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + box.x, p.y + box.y), col(pal::BG), S(8));
        if (a.previewTex && m.prevW > 0) {
            const float sc = std::min(box.x / m.prevW, box.y / m.prevH);
            const ImVec2 sz(m.prevW * sc, m.prevH * sc);
            const ImVec2 q(p.x + (box.x - sz.x) * 0.5f, p.y + (box.y - sz.y) * 0.5f);
            dl->AddImageRounded((ImTextureID)(intptr_t)a.previewTex, q, ImVec2(q.x + sz.x, q.y + sz.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, S(4));
        } else {
            const std::string t1 = T("ui.preview.none"), t2 = T("ui.preview.hint");
            dl->AddText(ImVec2(p.x + (box.x - ImGui::CalcTextSize(t1.c_str()).x) * 0.5f, p.y + box.y * 0.5f - ImGui::GetTextLineHeight()), col(pal::MUTED), t1.c_str());
            dl->AddText(ImVec2(p.x + (box.x - ImGui::CalcTextSize(t2.c_str()).x) * 0.5f, p.y + box.y * 0.5f), col(pal::FAINT), t2.c_str());
        }
        ImGui::Dummy(box);
        double st = 0, du = 0;
        if (!parseSeconds(W(m.f.start), st)) st = 0;
        if (!parseSeconds(W(m.f.dur), du)) du = 0;
        const double t = st + m.f.previewPos * du;
        const std::string timeText = TXF8("ui.preview_time", i18n::fixed(t, 2).c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", timeText.c_str());
        ImGui::SameLine();
        const float bw = buttonWidth("ui.btn.preview", true);
        ImGui::SetNextItemWidth(std::max(S(60), ImGui::GetContentRegionAvail().x - bw - S(8)));
        ImGui::SliderFloat("##pt", &m.f.previewPos, 0.0f, 1.0f, "");
        tip("ui.tip.preview_time");
        ImGui::SameLine();
        if (button("ui.btn.preview", "eye", 0, !m.running() && !m.paths.empty(), 0, "ui.tip.preview")) m.runPreview();
    }
    endCard();
}

void drawLog(App& a) {
    Model& m = a.model;
    if (beginCard("##log", "ui.card.log", "terminal", ImVec2(0, 0))) {
        ImGui::BeginChild("##logtext", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushTextWrapPos(0.0f);
        for (const auto& l : m.log) {
            unsigned c = pal::MUTED;
            if (l.level == 1) c = pal::ERR;
            else if (l.level == 2) c = pal::WARN;
            else if (l.level == 3) c = pal::OK;
            else if (l.level == 4) c = pal::TEXT;
            ImGui::TextColored(colv(c), "%s", l.text.empty() ? " " : U8(l.text).c_str());
        }
        ImGui::PopTextWrapPos();
        if (m.logDirty) {
            ImGui::SetScrollHereY(1.0f);
            m.logDirty = false;
        }
        ImGui::EndChild();
    }
    endCard();
}

std::string encoderName(const std::string& id) {
    if (id == "cpu") return T("ui.enc.cpu");
    if (id == "auto") return T("ui.enc.auto");
    if (id == "nvenc") return "NVENC";
    if (id == "vaapi") return "VAAPI";
    if (id == "qsv") return "QuickSync";
    if (id == "videotoolbox") return "VideoToolbox";
    return id;
}

void drawSettings(App& a, float height) {
    Model& m = a.model;
    Fields& f = m.f;
    const bool run = m.running();
    if (beginCard("##settings", "ui.card.settings", "sliders", ImVec2(0, height))) {
        ImGui::BeginDisabled(run);
        const char* chips[4][2] = {{"ui.chip.draft", "ui.tip.chip_draft"}, {"ui.chip.youtube", "ui.tip.chip_youtube"},
                                   {"ui.chip.4k60", "ui.tip.chip_4k60"}, {"ui.chip.vertical", "ui.tip.chip_vertical"}};
        for (int i = 0; i < 4; i++) {
            if (i) ImGui::SameLine();
            if (chip(chips[i][0], chips[i][1])) m.applyQuickPreset(i);
        }
        ImGui::BeginChild("##rows", ImVec2(0, -(ImGui::GetFrameHeight() + S(14))), ImGuiChildFlags_None);
        const float full = ImGui::GetContentRegionAvail().x - S(120) - ImGui::GetStyle().ScrollbarSize;

        // Résolution
        int resIdx = kResCount;
        for (int i = 0; i < kResCount; i++)
            if (kRes[i].w == f.w && kRes[i].h == f.h) resIdx = i;
        label("ui.lbl.resolution");
        if (comboList("##res", &resIdx, resItems(), full * 0.46f) && resIdx < kResCount) {
            f.w = kRes[resIdx].w;
            f.h = kRes[resIdx].h;
        }
        tip("ui.tip.resolution");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(full * 0.24f);
        ImGui::InputInt("##w", &f.w, 0, 0);
        tip("ui.tip.width");
        ImGui::SameLine(0, S(4));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T("ui.lbl.times").c_str());
        ImGui::SameLine(0, S(4));
        ImGui::SetNextItemWidth(full * 0.24f);
        ImGui::InputInt("##h", &f.h, 0, 0);
        tip("ui.tip.height");

        label("ui.lbl.fps");
        comboInput("fps", f.fps, sizeof(f.fps), {"24", "25", "30", "50", "60", "120"}, full * 0.46f);
        tip("ui.tip.fps");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T("ui.lbl.smoothing").c_str());
        tip("ui.tip.smoothing");
        ImGui::SameLine();
        comboList("##ss", &f.ss, {"1x", "2x", "3x", "4x"}, full * 0.20f);
        tip("ui.tip.smoothing");

        label("ui.lbl.duration");
        ImGui::SetNextItemWidth(full * 0.46f);
        ImGui::InputText("##dur", f.dur, sizeof(f.dur));
        tip("ui.tip.duration");
        label("ui.lbl.start");
        ImGui::SetNextItemWidth(full * 0.46f);
        ImGui::InputText("##start", f.start, sizeof(f.start));
        tip("ui.tip.start_time");

        label("ui.lbl.codec");
        comboList("##codec", &f.codec, {"H.264", "H.265"}, full * 0.46f);
        tip("ui.tip.codec");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T("ui.lbl.preset").c_str());
        ImGui::SameLine();
        std::vector<std::string> presets(kPresetNames, kPresetNames + 9);
        comboList("##preset", &f.preset, presets, full * 0.30f);
        tip("ui.tip.preset");

        label("ui.lbl.quality");
        ImGui::SetNextItemWidth(full * 0.46f);
        ImGui::SliderInt("##crf", &f.crf, 0, 35);
        tip("ui.tip.quality");
        ImGui::SameLine();
        const char* hint = f.crf == 0 ? "ui.crf.lossless" : f.crf <= 12 ? "ui.crf.near_perfect" : f.crf <= 18 ? "ui.crf.very_good" : f.crf <= 23 ? "ui.crf.good" : "ui.crf.reduced";
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T(hint).c_str());

        label("ui.lbl.container");
        comboList("##container", &f.container, {"MP4", "MOV", "MKV"}, full * 0.46f);
        tip("ui.tip.container");

        label("ui.lbl.encoder");
        std::vector<std::string> encs;
        for (const auto& id : encoderList()) encs.push_back(encoderName(id));
        comboList("##encoder", &f.encoder, encs, full * 0.66f);
        tip("ui.tip.encoder");

        label("ui.lbl.bitrate");
        ImGui::SetNextItemWidth(full * 0.30f);
        ImGui::InputInt("##bitrate", &f.bitrate, 0, 0);
        tip("ui.tip.bitrate");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::FAINT), "%s", T("ui.cue.bitrate").c_str());

        label("ui.lbl.color");
        comboList("##color", &f.color, {T("ui.color.bt709tv"), T("ui.color.bt709pc"), T("ui.color.srgb"), T("ui.color.bt601")}, full * 0.66f);
        tip("ui.tip.color");

        label("ui.lbl.blur");
        comboList("##blur", &f.blur, {T("ui.opt.none"), "4", "8", "16"}, full * 0.30f);
        tip("ui.tip.blur");

        label("ui.lbl.loop");
        ImGui::SetNextItemWidth(full * 0.30f);
        ImGui::InputText("##loop", f.loop, sizeof(f.loop));
        tip("ui.tip.loop");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::FAINT), "%s", T("ui.cue.loop").c_str());

        label("ui.lbl.fade_in");
        ImGui::SetNextItemWidth(full * 0.22f);
        ImGui::InputText("##fi", f.fadein, sizeof(f.fadein));
        tip("ui.tip.fade_in");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T("ui.lbl.fade_out").c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(full * 0.22f);
        ImGui::InputText("##fo", f.fadeout, sizeof(f.fadeout));
        tip("ui.tip.fade_out");

        label("ui.lbl.audio");
        ImGui::SetNextItemWidth(full - buttonWidth("ui.btn.browse", false) - S(8));
        ImGui::InputTextWithHint("##audio", T("ui.cue.audio").c_str(), f.audio, sizeof(f.audio));
        tip("ui.tip.audio");
        ImGui::SameLine();
        if (button("ui.btn.browse", nullptr, 0, true, 0, "ui.tip.audio_browse"))
            openBrowser(a, BrowseMode::OpenFile, "dialog.audio_files", {L".mp3", L".wav", L".flac", L".ogg", L".m4a", L".aac"}, L"",
                        [&a](const std::vector<std::wstring>& r) {
                            if (!r.empty()) snprintf(a.model.f.audio, sizeof(a.model.f.audio), "%s", U8(r[0]).c_str());
                        });

        label("ui.lbl.output");
        ImGui::SetNextItemWidth(full - buttonWidth("ui.btn.browse", false) - S(8));
        ImGui::InputTextWithHint("##out", T("ui.cue.output").c_str(), f.outdir, sizeof(f.outdir));
        tip("ui.tip.output");
        ImGui::SameLine();
        ImGui::PushID("outbrowse");
        if (button("ui.btn.browse", nullptr, 0, true, 0, "ui.tip.output_browse"))
            openBrowser(a, BrowseMode::PickFolder, "dialog.out_folder", {}, L"", [&a](const std::vector<std::wstring>& r) {
                if (!r.empty()) snprintf(a.model.f.outdir, sizeof(a.model.f.outdir), "%s", U8(r[0]).c_str());
            });
        ImGui::PopID();
        ImGui::EndChild();

        if (button("ui.btn.profile", "folder", 0, true, 0, "ui.tip.profile_load"))
            openBrowser(a, BrowseMode::OpenFile, "dialog.profiles", {L".ini"}, L"", [&a](const std::vector<std::wstring>& r) {
                if (!r.empty()) a.model.loadProfile(r[0]);
            });
        ImGui::SameLine();
        if (button("ui.btn.save", "save", 0, true, 0, "ui.tip.profile_save"))
            openBrowser(a, BrowseMode::SaveFile, "dialog.profiles", {L".ini"}, L"profile.ini", [&a](const std::vector<std::wstring>& r) {
                if (r.empty()) return;
                std::wstring p = r[0];
                if (p.size() < 4 || lower(U8(p)).compare(p.size() - 4, 4, ".ini") != 0) p += L".ini";
                a.model.saveProfile(p);
            });
        ImGui::EndDisabled();
    }
    endCard();
}

void drawBottom(App& a) {
    Model& m = a.model;
    const bool run = m.running();
    ImGui::BeginChild("##bottom", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(colv(m.progressFailed ? pal::ERR : pal::MUTED), "%s", U8(m.status).c_str());
    const float wStart = buttonWidth("ui.btn.start", true) + S(14), wPause = buttonWidth("ui.btn.resume", true) + S(14),
                wCancel = buttonWidth("ui.btn.cancel", true) + S(14);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttons = wStart + wPause + wCancel + spacing * 2;
    const float y0 = ImGui::GetCursorPosY();
    const float btnH = ImGui::GetFrameHeight() + S(4);
    ImGui::SetCursorPosY(y0 + (btnH - S(14)) * 0.5f);
    progressBar(m.progress, std::max(S(60), ImGui::GetContentRegionAvail().x - buttons - S(14)), S(14), m.progressFailed);
    ImGui::SameLine(0, S(14));
    ImGui::SetCursorPosY(y0);
    if (button("ui.btn.start", "play", 1, !run && !m.paths.empty(), wStart, "ui.tip.start")) m.startJobs(false);
    ImGui::SameLine();
    const bool paused = m.paused();
    if (button(paused ? "ui.btn.resume" : "ui.btn.pause", paused ? "play" : "pause", 0, run && !m.previewRun, wPause, "ui.tip.pause")) m.togglePause();
    ImGui::SameLine();
    if (button("ui.btn.cancel", "stop", 0, run && !m.previewRun, wCancel, "ui.tip.cancel")) m.cancelRun();
    ImGui::EndChild();
}

void linkText(const char* text, const std::wstring& target) {
    ImGui::TextColored(colv(pal::ACCENT), "%s", text);
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, mx.y), ImVec2(mx.x, mx.y), col(pal::ACCENT));
        if (ImGui::IsMouseClicked(0)) platform::openWithDefault(target);
    }
}

void drawAbout(App& a) {
    if (a.showAbout) {
        ImGui::OpenPopup("##about");
        a.showAbout = false;
    }
    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(460), 0));
    if (!ImGui::BeginPopupModal("##about", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) return;
    ImGui::BeginGroup();
    icon("logo", S(56), 0xFFFFFF);
    ImGui::EndGroup();
    ImGui::SameLine(0, S(14));
    ImGui::BeginGroup();
    ImGui::PushFont(fontBold);
    ImGui::TextUnformatted(APP_NAME_STR);
    ImGui::PopFont();
    ImGui::TextColored(colv(pal::MUTED), "%s", TXF8("about.version", APP_VER_WSTR).c_str());
    ImGui::TextColored(colv(pal::MUTED), "%s", T("ui.tagline").c_str());
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(colv(pal::MUTED), "%s", T("about.copyright").c_str());
    ImGui::SameLine(S(110));
    ImGui::Text("© %s %s", APP_COPYRIGHT_YEAR, APP_COPYRIGHT_OWNER);
    ImGui::TextColored(colv(pal::MUTED), "%s", T("about.licence_label").c_str());
    ImGui::SameLine(S(110));
    ImGui::TextUnformatted(T("about.license").c_str());
    ImGui::TextColored(colv(pal::MUTED), "%s", T("about.contact").c_str());
    ImGui::SameLine(S(110));
    linkText(APP_CONTACT_EMAIL, L"mailto:" APP_CONTACT_EMAIL_WSTR);
    ImGui::TextColored(colv(pal::MUTED), "%s", T("about.website").c_str());
    ImGui::SameLine(S(110));
    linkText(APP_WEBSITE_URL, APP_WEBSITE_URL_WSTR);
    ImGui::Spacing();
    ImGui::PushTextWrapPos(S(430));
    ImGui::TextColored(colv(pal::FAINT), "%s", T("about.warranty").c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (button("about.btn.license", nullptr, 0, true)) platform::openWithDefault(a.model.exeDir + L"COPYING");
    ImGui::SameLine();
    const bool fr = i18n::language() == "fr";
    if (button("about.btn.notices", nullptr, 0, true))
        platform::openWithDefault(a.model.exeDir + (fr ? L"THIRD_PARTY_NOTICES.fr.md" : L"THIRD_PARTY_NOTICES.md"));
    ImGui::SameLine();
    if (button("about.btn.close", nullptr, 1, true) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawDialogs(App& a) {
    Model& m = a.model;
    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    if (!m.dialogText.empty() && !ImGui::IsPopupOpen("##msg")) ImGui::OpenPopup("##msg");
    if (m.askDownload) {
        ImGui::OpenPopup("##ask");
        m.askDownload = false;
    }
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(440), 0));
    if (ImGui::BeginPopupModal("##msg", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(fontBold);
        ImGui::TextUnformatted(U8(m.dialogTitle).c_str());
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(S(410));
        ImGui::TextUnformatted(U8(m.dialogText).c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (button("ui.btn.ok", nullptr, 1, true) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m.dialogText.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(460), 0));
    if (ImGui::BeginPopupModal("##ask", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(fontBold);
        ImGui::TextUnformatted(T("dialog.ffmpeg_missing_title").c_str());
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(S(430));
        ImGui::TextUnformatted(T("dialog.ffmpeg_missing").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (button("ui.btn.yes", nullptr, 1, true)) {
            ImGui::CloseCurrentPopup();
            m.startFfmpegDownload();
        }
        ImGui::SameLine();
        if (button("ui.btn.no", nullptr, 0, true) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void drawFrame(App& a, GLFWwindow* win) {
    Model& m = a.model;
    const bool popup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!popup) {  // raccourcis (désactivés pendant une fenêtre modale)
        const bool ctrl = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false) && !m.running())
            openBrowser(a, BrowseMode::OpenFiles, "dialog.shaders", {L".glsl", L".txt"}, L"", [&a](const std::vector<std::wstring>& r) { a.model.addFiles(r); });
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) m.runPreview();
        if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) a.showAbout = true;
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) m.startJobs(false);
        if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) m.togglePause();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m.cancelRun();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) m.removeSelected();
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(14)));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    drawHeader(a, win);
    ImGui::Spacing();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float bottomH = S(72);
    const float bodyH = std::max(S(200), avail.y - bottomH - gap);
    const float leftW = std::floor((avail.x - gap) * 0.44f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##left", ImVec2(leftW, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colv(pal::CARD));
    const float cardsH = bodyH - gap * 2;
    drawShaders(a, std::floor(cardsH * 0.36f));
    drawPreview(a, std::floor(cardsH * 0.34f));
    drawLog(a);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##right", ImVec2(0, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colv(pal::CARD));
    drawSettings(a, ImGui::GetContentRegionAvail().y);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    drawBottom(a);
    drawAbout(a);
    drawDialogs(a);
    a.browser.draw();
    ImGui::End();
    ImGui::PopStyleVar();
}

void onDrop(GLFWwindow*, int count, const char** paths) {
    if (!g_app) return;
    for (int i = 0; i < count; i++) g_app->drops.push_back(W(paths[i]));
}

}  // namespace

}  // namespace ui

// Option de ligne de commande utilisée par l'intégration continue : photographie la fenêtre puis quitte.
static std::string g_shotPath;
static bool g_shotAbout = false;
void setScreenshotRequest(const std::string& path, bool about) {
    g_shotPath = path;
    g_shotAbout = about;
}

// Écrit le logo en PNG (icône des paquets) : ShaderToMP4 --export-logo <taille> <fichier.png>
bool exportLogo(int size, const std::string& path) {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    return ui::rasterizeIcon("logo", size, rgba, w, h) && ui::writePng(W(path), w, h, rgba);
}

int runGui(const std::wstring& exeDir, const std::vector<std::wstring>& files) {
    using namespace ui;
    const std::string saved = readSavedLanguage(exeDir);
    if (!saved.empty()) i18n::setLanguage(saved);
    if (!glfwInit()) {
        fprintf(stderr, "%s\n", U8(TX("error.gl_window")).c_str());
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* hidden = glfwCreateWindow(64, 64, APP_NAME_STR, nullptr, nullptr);  // contexte du thread de rendu
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    GLFWwindow* win = glfwCreateWindow(1180, 860, APP_NAME_STR " " APP_VER_STR, nullptr, nullptr);
    if (!win || !hidden) {
        fprintf(stderr, "%s\n", U8(TX("error.gl_core")).c_str());
        glfwTerminate();
        return 1;
    }
    platform::setSharedGLWindow(hidden);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    float xs = 1, ys = 1;
    glfwGetWindowContentScale(win, &xs, &ys);
    const float content = std::max(xs, ys);
#ifdef __APPLE__
    const float uiScale = 1.0f, fbScale = content;  // macOS : coordonnées en points, framebuffer plus fin
#else
    const float uiScale = content, fbScale = 1.0f;
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // portable : rien n'est écrit en dehors du dossier de l'application
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    setupFonts(uiScale, fbScale);
    applyStyle();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
#ifdef __APPLE__
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

    App app(exeDir);
    g_app = &app;
    glfwSetDropCallback(win, onDrop);
    app.model.appendLog(std::wstring(APP_NAME_WSTR) + L" " + APP_VER_WSTR);
    app.model.addFiles(files);
    const bool shot = !g_shotPath.empty();
    int frameNo = 0;
    if (shot) {
        // pas de rendu complet ni de proposition de téléchargement : on prépare seulement ce qui sera visible
    } else if (!files.empty()) {
        app.model.startJobs(false);
    } else if (app.model.ffmpegMissing()) {
        if (ffmpegDownloadSupported()) app.model.askDownload = true;
    }

    while (!glfwWindowShouldClose(win)) {
        glfwWaitEventsTimeout(shot ? 0.03 : app.model.running() ? 0.05 : 0.2);
        frameNo++;
        if (shot && frameNo == 3 && !app.model.paths.empty()) {
            app.model.f.previewPos = 0.5f;
            app.model.runPreview();
        }
        if (shot && frameNo == 5 && g_shotAbout) app.showAbout = true;
        if (!app.drops.empty()) {
            app.model.addFiles(app.drops);
            app.drops.clear();
        }
        app.model.pump();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::PushFont(fontRegular);
        drawFrame(app, win);
        ImGui::PopFont();
        ImGui::Render();
        int fw = 0, fh = 0;
        glfwGetFramebufferSize(win, &fw, &fh);
        glViewport(0, 0, fw, fh);
        const ImVec4 bg = colv(pal::BG);
        glClearColor(bg.x, bg.y, bg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (shot && frameNo > 12 && (app.model.paths.empty() || app.model.prevW > 0)) {
            captureScreen(win, g_shotPath);
            break;
        }
        glfwSwapBuffers(win);
    }

    Settings cur = app.model.base;
    std::wstring e;
    if (app.model.toSettings(cur, e)) {
        app.model.base = cur;
        app.model.save();
    }
    app.model.cancelRun();
    g_app = nullptr;
    ui::freeTextures();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwDestroyWindow(hidden);
    glfwTerminate();
    return 0;
}
