// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Interface Linux / macOS : GLFW (fenêtre, entrées, glisser-déposer) + Dear ImGui + icônes SVG (NanoSVG).
#include "core/ffmpeg_download.h"
#include "core/formats.h"
#include "core/locate.h"
#include "core/outputopts.h"
#include "core/params.h"
#include "core/postfx.h"
#include "core/presets.h"
#include "core/queue.h"
#include "core/i18n.h"
#include "core/util.h"
#include "platform/platform.h"
#include "platform/posix/glfw_bridge.h"
#include "ui_imgui/dock.h"
#include "ui_imgui/editor_imgui.h"
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
    bool fontsDirty = false;  // la langue change : japonais et chinois demandent une autre police, l'atlas est refait avant l'image suivante
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
                const std::string before = lower(i18n::language());
                i18n::setLanguage(l);
                if (l == "ja" || l == "zh" || before == "ja" || before == "zh") a.fontsDirty = true;
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
        for (const char* k : {"ui.btn.add", "ui.btn.remove", "ui.btn.clear", "ui.btn.folder", "ui.btn.edit", "ui.btn.png"})
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
            if (ImGui::Selectable((batchOf(m.batches, m.paths[i]) ? "[" + std::to_string(batchOf(m.batches, m.paths[i])) + "] " + baseName(m.paths[i]) : baseName(m.paths[i])).c_str(), i == m.selected, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetTextLineHeight() + S(6))))
                if (!run) m.selected = i;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s", U8(m.paths[i]).c_str());
            }
            if (ImGui::BeginPopupContextItem("##itemmenu")) {  // clic droit : priorite, rendre cette seule entree, retirer
                if (!run) m.selected = i;
                ImGui::BeginDisabled(run);
                if (ImGui::Selectable(T("ui.menu.move_top").c_str())) m.moveSelected(0);
                if (ImGui::Selectable(T("ui.menu.move_up").c_str())) m.moveSelected(-1);
                if (ImGui::Selectable(T("ui.menu.move_down").c_str())) m.moveSelected(1);
                ImGui::Separator();
                if (ImGui::Selectable(T("ui.menu.edit").c_str())) openEditor(m);
                if (ImGui::Selectable(T("ui.menu.render_selection").c_str())) m.startJobs(false, 0, 0, 0, false, true);
                if (ImGui::Selectable(T("ui.menu.reset_state").c_str())) m.resetSelectedState();
                if (ImGui::BeginMenu(T("ui.menu.batch").c_str())) {  // lots : numeroter le shader, puis rendre un lot
                    for (int n = 1; n <= kBatchCount; n++)
                        if (ImGui::MenuItem(TXF8("ui.menu.batch_set", n).c_str())) m.setBatch(n);
                    if (ImGui::MenuItem(T("ui.menu.batch_none").c_str())) m.setBatch(0);
                    ImGui::EndMenu();
                }
                for (int n = 1; n <= kBatchCount; n++) {
                    int count = 0;
                    for (const auto& p : m.paths) count += batchOf(m.batches, p) == n;
                    if (count > 0 && ImGui::Selectable(TXF8("ui.menu.batch_render", n, count).c_str())) m.startJobs(false, 0, 0, 0, false, false, n);
                }
                ImGui::BeginDisabled(!m.fileScopeOn());
                if (ImGui::Selectable(T("ui.menu.write_shader").c_str())) m.writeScopeIntoShader();
                ImGui::EndDisabled();
                ImGui::Separator();
                if (ImGui::Selectable(T("ui.btn.remove").c_str())) m.removeSelected();
                ImGui::EndDisabled();
                ImGui::EndPopup();
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
            openBrowser(a, BrowseMode::OpenFiles, "dialog.shaders", {L".glsl", L".txt", L".json", L".montage"}, L"", [&a](const std::vector<std::wstring>& r) { a.model.addFiles(r); });
        fit(buttonWidth("ui.btn.remove", true));
        if (button("ui.btn.remove", "trash", 0, !run && any, 0, "ui.tip.remove")) m.removeSelected();
        fit(buttonWidth("ui.btn.clear", true));
        if (button("ui.btn.clear", "clear", 0, !run && any, 0, "ui.tip.clear")) m.clearList();
        fit(buttonWidth("ui.btn.folder", true));
        if (button("ui.btn.folder", "folder", 0, any, 0, "ui.tip.folder")) m.openOutputFolder();
        fit(buttonWidth("ui.btn.edit", true));
        if (button("ui.btn.edit", "edit", 0, any, 0, "ui.tip.editor_btn")) openEditor(m);
        fit(buttonWidth("ui.btn.png", true));
        if (button("ui.btn.png", "image", 0, !run && any, 0, "ui.tip.png")) ImGui::OpenPopup("##images");
        if (ImGui::BeginPopup("##images")) {  // image PNG, vignette de couverture ou planche-contact (grille au choix)
            if (ImGui::Selectable(T("ui.menu.png").c_str())) m.startJobs(true, 0);
            if (ImGui::Selectable(T("ui.menu.png_now").c_str())) m.startJobs(true, 0, 0, 0, true);
            if (ImGui::Selectable(T("ui.menu.thumb").c_str())) m.startJobs(true, 1);
            if (ImGui::Selectable(T("ui.menu.thumb_smart").c_str())) m.startJobs(true, 3);
            ImGui::Separator();
            for (int i = 0; i < kSheetGridCount; i++)
                if (ImGui::Selectable(TXF8("ui.menu.sheet", kSheetGrids[i][0], kSheetGrids[i][1]).c_str())) m.startJobs(true, 2, kSheetGrids[i][0], kSheetGrids[i][1]);
            ImGui::EndPopup();
        }
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
        const std::string timeText = m.playing() ? TXF8("ui.preview_play", i18n::fixed(t, 2).c_str(), (long long)std::llround((t - st) * m.base.fps), i18n::fixed(m.playMeter.fps(), 0).c_str())
                                                 : TXF8("ui.preview_time", i18n::fixed(t, 2).c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", timeText.c_str());
        ImGui::SameLine();
        const float bw = buttonWidth("ui.btn.preview", true);
        const bool on = m.playing();
        const float pw = std::max(buttonWidth("ui.btn.play", true), buttonWidth("ui.btn.stop_play", true));
        ImGui::SetNextItemWidth(std::max(S(60), ImGui::GetContentRegionAvail().x - bw - pw - S(16)));
        if (ImGui::SliderFloat("##pt", &m.f.previewPos, 0.0f, 1.0f, "") && on) m.seekPlay();
        tip("ui.tip.preview_time");
        {  // images clés des paramètres du shader sélectionné : un trait fin sur le curseur (relues si le code ou la plage change)
            static std::string lastKey;
            static std::vector<float> marks;
            const std::wstring path = m.paths.empty() ? L"" : m.paths[std::min(std::max(0, m.selected), (int)m.paths.size() - 1)];
            const std::string key = U8(path) + "|" + std::to_string(st) + "|" + std::to_string(du) + "|" + (path.empty() ? "" : fnvHex(collectShaderCode(path)));
            if (key != lastKey) {
                lastKey = key;
                marks.clear();
                std::vector<ParamInfo> infos;
                std::vector<double> keys;
                std::wstring err;
                if (!path.empty() && du > 0 && loadShaderParams(m.exeDir, path, infos, err, nullptr, &keys))
                    for (double kt : keys) {
                        const double f = (kt - st) / du;
                        if (f >= 0 && f <= 1) marks.push_back((float)f);
                    }
            }
            const ImVec2 a0 = ImGui::GetItemRectMin(), a1 = ImGui::GetItemRectMax();
            const float grab = ImGui::GetStyle().GrabMinSize;
            for (float f : marks) {
                const float x = a0.x + grab * 0.5f + f * (a1.x - a0.x - grab);
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x - S(1), a0.y + S(2)), ImVec2(x + S(1), a1.y - S(2)), col(pal::ACCENT, 0.9f), S(1));
            }
        }
        ImGui::SameLine();
        if (button(on ? "ui.btn.stop_play" : "ui.btn.play", on ? "stop" : "play", 0, !m.running() && !m.paths.empty(), pw, "ui.tip.play")) m.togglePlay();
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
            size_t pos = 0;  // une ligne par appel : double-clic sur « fichier:ligne: » ouvre le shader a la ligne
            for (;;) {
                const size_t e = l.text.find(L'\n', pos);
                const std::wstring one = l.text.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos);
                ImGui::TextColored(colv(c), "%s", one.empty() ? " " : U8(one).c_str());
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    std::wstring path;
                    int line = 0;
                    if (parseLocation(one, path, line)) openAtLine(path, line);
                }
                if (e == std::wstring::npos) break;
                pos = e + 1;
            }
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

// Noms des codecs et des conteneurs ; ceux que ce ffmpeg ne sait pas produire sont signales (liste refaite si la langue
// ou ffmpeg change)
const std::vector<std::string>& formatLabels(Model& m, bool codecs) {
    static std::vector<std::string> codecNames, containerNames;
    static int revision = -1;
    static std::string lang;
    if (revision != m.formatsRevision || lang != i18n::language() || codecNames.empty()) {
        revision = m.formatsRevision;
        lang = i18n::language();
        const std::wstring ff = findFfmpeg(m.exeDir);
        const std::string na = "  (" + T("ui.opt.unavailable") + ")";
        codecNames.clear();
        containerNames.clear();
        for (int i = 0; i < codecCount(); i++)
            codecNames.push_back(U8(codecAt(i).label) + (codecAvailable(ff, codecAt(i).id) ? "" : na));
        for (int i = 0; i < containerCount(); i++)
            containerNames.push_back(U8(containerAt(i).label) + (containerAvailable(ff, containerAt(i).id) ? "" : na));
    }
    return codecs ? codecNames : containerNames;
}

// --- Paramètres du shader (// uniform ...) ------------------------------------------------------------------------------------
// Un curseur, une case ou un sélecteur de couleur par paramètre déclaré dans le shader sélectionné ; les valeurs sont enregistrées
// dans params/ (dossier de l'application) et reprises par l'aperçu en lecture et par les rendus.
struct ParamsUi {
    std::wstring path, file;
    std::string codeHash;
    double checked = -10;
    std::vector<ParamInfo> infos;
    std::string error;
};

void reloadParams(Model& m, ParamsUi& u, const std::wstring& path) {
    u.path = path;
    u.infos.clear();
    u.error.clear();
    u.codeHash.clear();
    if (path.empty()) return;
    u.file = paramsFile(m.exeDir, path);
    std::wstring err;
    if (!loadShaderParams(m.exeDir, path, u.infos, err, &u.codeHash)) u.error = U8(err);
}

void drawParams(App& a) {
    static ParamsUi u;
    Model& m = a.model;
    const std::wstring cur = m.paths.empty() ? L"" : m.paths[std::min(std::max(0, m.selected), (int)m.paths.size() - 1)];
    const double now = ImGui::GetTime();
    if (cur != u.path) {
        reloadParams(m, u, cur);
        u.checked = now;
    } else if (now - u.checked > 1.0) {  // le fichier du shader a pu changer sur le disque
        u.checked = now;
        if (!cur.empty() && fnvHex(collectShaderCode(cur)) != u.codeHash) reloadParams(m, u, cur);
    }
    if (u.infos.empty() && u.error.empty()) return;
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::TextColored(colv(pal::ACCENT), "%s", T("ui.lbl.params").c_str());
    ImGui::Separator();
    if (!u.error.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(colv(pal::ERR), "%s", u.error.c_str());
        ImGui::PopTextWrapPos();
        return;
    }
    const float width = ImGui::GetContentRegionAvail().x - S(120) - S(110);
    for (ParamInfo& p : u.infos) {
        ImGui::PushID(p.name.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", p.name.c_str());
        ImGui::SameLine(S(120));
        const bool locked = p.animated || !p.audio.empty();
        double lo = 0, hi = 1;
        paramRange(p, lo, hi);
        float v[4];
        for (int i = 0; i < 4; i++) v[i] = (float)p.value[i];
        bool changed = false;
        ImGui::BeginDisabled(locked);
        ImGui::SetNextItemWidth(width);
        if (p.type == "float") changed = ImGui::SliderFloat("##v", &v[0], (float)lo, (float)hi, "%.3g");
        else if (p.type == "int") {
            int iv = (int)std::lround(v[0]);
            if (ImGui::SliderInt("##v", &iv, (int)std::lround(lo), (int)std::lround(hi))) {
                v[0] = (float)iv;
                changed = true;
            }
        } else if (p.type == "bool") {
            bool b = v[0] >= 0.5f;
            if (ImGui::Checkbox("##v", &b)) {
                v[0] = b ? 1.0f : 0.0f;
                changed = true;
            }
        } else if (p.type == "color") changed = ImGui::ColorEdit3("##v", v, ImGuiColorEditFlags_PickerHueWheel);
        else if (p.type == "vec2") changed = ImGui::SliderFloat2("##v", v, (float)lo, (float)hi, "%.3g");
        else if (p.type == "vec3") changed = ImGui::SliderFloat3("##v", v, (float)lo, (float)hi, "%.3g");
        else changed = ImGui::SliderFloat4("##v", v, (float)lo, (float)hi, "%.3g");
        ImGui::EndDisabled();
        tip("ui.tip.param");
        if (changed) {
            for (int i = 0; i < 4; i++) p.value[i] = v[i];
            saveParamValue(u.file, p.name, formatParamValue(p, p.value));
        }
        if (locked) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(colv(pal::FAINT), "%s", T(p.animated ? "ui.params.animated" : "ui.params.audio").c_str());
        }
        ImGui::PopID();
    }
    if (button("ui.btn.reset_params", nullptr, 0, true, 0, "ui.tip.reset_params")) {
        resetParamValues(u.file);
        reloadParams(m, u, cur);
    }
}

// --- Post-traitement : effets et LUT couleur ---------------------------------------------------------------------------------
void drawPostfx(App& a) {
    Fields& f = a.model.f;
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::TextColored(colv(pal::ACCENT), "%s", T("ui.lbl.post").c_str());
    ImGui::Separator();
    const auto& fx = postEffectInfos();
    const float width = ImGui::GetContentRegionAvail().x - S(120) - S(110);
    for (size_t i = 0; i < fx.size() && i < 8; i++) {
        ImGui::PushID((int)i);
        ImGui::Checkbox("##on", &f.postOn[i]);
        tip("ui.tip.post");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(f.postOn[i] ? pal::TEXT : pal::MUTED), "%s", T(fx[i].labelKey).c_str());
        ImGui::SameLine(S(120));
        ImGui::BeginDisabled(!f.postOn[i]);
        ImGui::SetNextItemWidth(width);
        ImGui::SliderFloat("##amt", &f.postAmt[i], (float)fx[i].lo, (float)fx[i].hi, "%.3f");
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    label("ui.lbl.lut");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth("ui.btn.browse", false) - S(110));
    ImGui::InputTextWithHint("##lut", T("ui.cue.lut").c_str(), f.lut, sizeof(f.lut));
    tip("ui.tip.lut");
    ImGui::SameLine();
    ImGui::PushID("lutbrowse");
    if (button("ui.btn.browse", nullptr, 0, true, 0, "ui.tip.lut_browse"))
        openBrowser(a, BrowseMode::OpenFile, "dialog.lut_files", {L".cube"}, L"", [&a](const std::vector<std::wstring>& r) {
            if (!r.empty()) snprintf(a.model.f.lut, sizeof(a.model.f.lut), "%s", U8(r[0]).c_str());
        });
    ImGui::PopID();
}

// --- Sortie : transparence, HDR, taille, interpolation, volume sonore, texte sur la video (core/outputopts) -------------------
void drawOutput(App& a) {
    Fields& f = a.model.f;
    const float colX = S(190);  // les libelles de cette section sont plus longs que ceux de la carte : champs decales
    const float width = std::max(S(80), ImGui::GetContentRegionAvail().x - colX - S(110));
    auto row = [&](const char* key) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T(key).c_str());
        ImGui::SameLine(colX);
    };
    auto section = [&](const char* key) {
        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::TextColored(colv(pal::ACCENT), "%s", T(key).c_str());
        ImGui::Separator();
    };
    auto note = [&](const char* key) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(colv(pal::FAINT), "%s", T(key).c_str());
        ImGui::PopTextWrapPos();
    };
    const std::vector<std::string> offOn = {T("ui.output.off"), T("ui.output.on")};

    section("ui.output.sec_alpha");
    row("ui.output.alpha");
    int al = f.alpha ? 1 : 0;
    if (comboList("##alpha", &al, offOn, width)) f.alpha = al == 1;
    note("ui.output.hint_alpha");

    section("ui.output.sec_hdr");
    row("ui.output.hdr");
    comboList("##hdr", &f.hdr, {T("ui.output.off"), "HDR10 (PQ)", "HLG"}, width);
    ImGui::BeginDisabled(f.hdr == 0);
    row("ui.output.white");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##hdrwhite", f.hdrWhite, sizeof(f.hdrWhite));
    ImGui::EndDisabled();
    ImGui::BeginDisabled(f.hdr != 1);
    row("ui.output.peak");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##hdrpeak", f.hdrPeak, sizeof(f.hdrPeak));
    row("ui.output.fall");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##hdrfall", f.hdrFall, sizeof(f.hdrFall));
    ImGui::EndDisabled();
    note("ui.output.hint_hdr");

    section("ui.output.sec_size");
    row("ui.output.scale");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##scale", f.scale, sizeof(f.scale));
    row("ui.output.filter");
    std::vector<std::string> filters;
    for (int i = 0; i < kScaleFilterCount; i++) filters.push_back(T(scaleFilterLabelKey(i)));
    comboList("##scalefilter", &f.scaleFilter, filters, width);
    row("ui.output.interp");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##interp", f.interp, sizeof(f.interp));
    row("ui.output.loudnorm");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##loudnorm", f.loudnorm, sizeof(f.loudnorm));
    note("ui.output.hint_size");

    section("ui.output.sec_text");
    row("ui.output.text_title");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##title", f.title, sizeof(f.title));
    row("ui.output.credit");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##credit", f.credit, sizeof(f.credit));
    row("ui.output.titledur");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##titledur", f.titleDur, sizeof(f.titleDur));
    note("ui.output.hint_text");

    section("ui.output.sec_files");
    row("ui.output.name");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##name", f.name, sizeof(f.name));
    row("ui.output.exists");
    comboList("##exists", &f.exists, {T("ui.output.exists_overwrite"), T("ui.output.exists_number"), T("ui.output.exists_skip")}, width);
    row("ui.output.metadata");
    int meta = f.metadata ? 1 : 0;
    if (comboList("##metadata", &meta, offOn, width)) f.metadata = meta == 1;
    row("ui.output.variants");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##variants", f.variants, sizeof(f.variants));
    row("ui.output.endaction");
    comboList("##endaction", &f.endAction, {T("ui.output.end_none"), T("ui.output.end_folder"), T("ui.output.end_sleep")}, width);
    note("ui.output.hint_files");

    section("ui.output.sec_engine");
    row("ui.output.chunk");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##chunk", f.chunk, sizeof(f.chunk));
    row("ui.output.pbo");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##pbo", f.pbo, sizeof(f.pbo));
    row("ui.output.shutter");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##shutter", f.shutter, sizeof(f.shutter));
    note("ui.output.hint_engine");

    section("ui.output.sec_time");
    row("ui.output.loopmode");
    comboList("##loopmode", &f.loopMode, {T("ui.loopmode.none"), T("ui.loopmode.crossfade"), T("ui.loopmode.pingpong")}, width);
    row("ui.output.loopfade");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##loopfade", f.loopFade, sizeof(f.loopFade));
    row("ui.output.timemap");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##timemap", f.timemap, sizeof(f.timemap));
    row("ui.output.speed");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##speed", f.speed, sizeof(f.speed));
    row("ui.output.samples");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##samples", f.samples, sizeof(f.samples));
    note("ui.output.hint_time");
    if (button("ui.output.findloop_btn", "eye", 0, !a.model.running() && !a.model.paths.empty(), 0, "ui.output.hint_time")) a.model.startJobs(true, 4);
}

// Reglage impose par le shader selectionne : le champ est grise (il montre la valeur du shader)
struct Lock {
    bool on;
    explicit Lock(bool b) : on(b) { if (on) ImGui::BeginDisabled(); }
    ~Lock() { if (on) ImGui::EndDisabled(); }
};

void drawSettings(App& a, float height) {
    Model& m = a.model;
    Fields& f = m.f;
    const bool run = m.running();
    if (beginCard("##settings", "ui.card.settings", "sliders", ImVec2(0, height))) {
        ImGui::BeginDisabled(run);
        // « ce shader seul » : les champs valent pour le shader selectionne uniquement (jobs/), un second clic rend les reglages generaux
        if (button("ui.btn.file_scope", "sliders", m.fileScopeOn() ? 1 : 0, !m.paths.empty(), 0, "ui.tip.file_scope")) m.toggleFileScope();
        ImGui::SameLine();
        const char* chips[4][2] = {{"ui.chip.draft", "ui.tip.chip_draft"}, {"ui.chip.youtube", "ui.tip.chip_youtube"},
                                   {"ui.chip.4k60", "ui.tip.chip_4k60"}, {"ui.chip.vertical", "ui.tip.chip_vertical"}};
        for (int i = 0; i < 4; i++) {
            if (i) ImGui::SameLine();
            if (chip(chips[i][0], chips[i][1])) m.applyQuickPreset(i);
        }
        {  // « Plus » : preselections supplementaires et fichiers presets/*.ini
            static std::vector<UserPreset> users;
            ImGui::SameLine();
            if (chip("ui.chip.more", "ui.tip.chip_more")) {
                users = userPresets(m.exeDir);
                ImGui::OpenPopup("##morepresets");
            }
            if (ImGui::BeginPopup("##morepresets")) {
                for (int i = kChipPresetCount; i < kBuiltinPresetCount; i++)
                    if (ImGui::Selectable(T(builtinPresetKey(i)).c_str())) m.applyQuickPreset(i);
                ImGui::Separator();
                if (users.empty()) {
                    ImGui::BeginDisabled();
                    ImGui::Selectable(T("ui.preset.none").c_str());
                    ImGui::EndDisabled();
                }
                for (const auto& u : users)
                    if (ImGui::Selectable(U8(u.name).c_str())) m.loadProfile(u.path);
                ImGui::EndPopup();
            }
        }
        {  // reglages ecrits dans le shader selectionne : ils priment sur ceux de la fenetre
            static std::wstring lastPath;
            static std::string forced;
            const std::wstring cur = m.paths.empty() ? L"" : m.paths[std::min(std::max(0, m.selected), (int)m.paths.size() - 1)];
            if (cur != lastPath) {
                lastPath = cur;
                const std::wstring o = cur.empty() ? L"" : shaderOverridesOf(cur);
                forced = o.empty() ? "" : U8(TXF("ui.forced", o.c_str()));
            }
            if (!forced.empty()) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(colv(pal::ACCENT), "%s", forced.c_str());
                ImGui::PopTextWrapPos();
                tip("ui.tip.forced");
            }
        }
        ImGui::BeginChild("##rows", ImVec2(0, -(ImGui::GetFrameHeight() + S(14))), ImGuiChildFlags_None);
        const float full = ImGui::GetContentRegionAvail().x - S(120) - ImGui::GetStyle().ScrollbarSize;

        // Résolution
        int resIdx = kResCount;
        for (int i = 0; i < kResCount; i++)
            if (kRes[i].w == f.w && kRes[i].h == f.h) resIdx = i;
        label("ui.lbl.resolution");
        {
            Lock lk(m.isForced("width") || m.isForced("height"));
            if (comboList("##res", &resIdx, resItems(), full * 0.46f) && resIdx < kResCount) {
                f.w = kRes[resIdx].w;
                f.h = kRes[resIdx].h;
            }
        }
        tip("ui.tip.resolution");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(full * 0.24f);
        {
            Lock lk(m.isForced("width"));
            ImGui::InputInt("##w", &f.w, 0, 0);
        }
        tip("ui.tip.width");
        ImGui::SameLine(0, S(4));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T("ui.lbl.times").c_str());
        ImGui::SameLine(0, S(4));
        ImGui::SetNextItemWidth(full * 0.24f);
        {
            Lock lk(m.isForced("height"));
            ImGui::InputInt("##h", &f.h, 0, 0);
        }
        tip("ui.tip.height");

        label("ui.lbl.fps");
        {
            Lock lk(m.isForced("fps"));
            comboInput("fps", f.fps, sizeof(f.fps), {"24", "25", "30", "50", "60", "120"}, full * 0.46f);
        }
        tip("ui.tip.fps");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T("ui.lbl.smoothing").c_str());
        tip("ui.tip.smoothing");
        ImGui::SameLine();
        {
            Lock lk(m.isForced("ss"));
            comboList("##ss", &f.ss, {"1x", "2x", "3x", "4x"}, full * 0.20f);
        }
        tip("ui.tip.smoothing");

        label("ui.lbl.duration");
        ImGui::SetNextItemWidth(full * 0.46f);
        {
            Lock lk(m.isForced("duration"));
            ImGui::InputText("##dur", f.dur, sizeof(f.dur));
        }
        tip("ui.tip.duration");
        label("ui.lbl.start");
        ImGui::SetNextItemWidth(full * 0.46f);
        {
            Lock lk(m.isForced("start"));
            ImGui::InputText("##start", f.start, sizeof(f.start));
        }
        tip("ui.tip.start_time");

        label("ui.lbl.codec");
        {
            Lock lk(m.isForced("codec"));
            comboList("##codec", &f.codec, formatLabels(m, true), full * 0.46f);
        }
        tip("ui.tip.codec");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T("ui.lbl.preset").c_str());
        ImGui::SameLine();
        std::vector<std::string> presets(kPresetNames, kPresetNames + 9);
        {
            Lock lk(m.isForced("preset"));
            comboList("##preset", &f.preset, presets, full * 0.30f);
        }
        tip("ui.tip.preset");

        label("ui.lbl.quality");
        ImGui::SetNextItemWidth(full * 0.46f);
        {
            Lock lk(m.isForced("crf"));
            ImGui::SliderInt("##crf", &f.crf, 0, 35);
        }
        tip("ui.tip.quality");
        ImGui::SameLine();
        const char* hint = f.crf == 0 ? "ui.crf.lossless" : f.crf <= 12 ? "ui.crf.near_perfect" : f.crf <= 18 ? "ui.crf.very_good" : f.crf <= 23 ? "ui.crf.good" : "ui.crf.reduced";
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T(hint).c_str());

        label("ui.lbl.container");
        {
            Lock lk(m.isForced("container"));
            comboList("##container", &f.container, formatLabels(m, false), full * 0.46f);
        }
        tip("ui.tip.container");

        label("ui.lbl.depth");
        {
            Lock lk(m.isForced("depth"));
            comboList("##depth", &f.depth, {"8", "10"}, full * 0.30f);
        }
        tip("ui.tip.depth");

        label("ui.lbl.estimate");
        {
            Settings cur = m.base;
            std::wstring e;
            std::string est;
            if (m.toSettings(cur, e)) {
                applyOverrides(cur, m.forced);  // l'estimation suit ce que le shader imposera
                std::wstring dir = cur.outdir;
                if (dir.empty() && !m.paths.empty()) {
                    const size_t cut = m.paths[0].find_last_of(L"\\/");
                    dir = cut == std::wstring::npos ? L"" : m.paths[0].substr(0, cut);
                }
                est = U8(estimateText(cur, dir));
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(colv(pal::MUTED), "%s", est.c_str());
            tip("ui.tip.estimate");
        }

        label("ui.lbl.encoder");
        std::vector<std::string> encs;
        for (const auto& id : encoderList()) encs.push_back(encoderName(id));
        {
            Lock lk(m.isForced("encoder"));
            comboList("##encoder", &f.encoder, encs, full * 0.66f);
        }
        tip("ui.tip.encoder");

        label("ui.lbl.bitrate");
        ImGui::SetNextItemWidth(full * 0.30f);
        {
            Lock lk(m.isForced("bitrate"));
            ImGui::InputInt("##bitrate", &f.bitrate, 0, 0);
        }
        tip("ui.tip.bitrate");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::FAINT), "%s", T("ui.cue.bitrate").c_str());

        label("ui.lbl.color");
        {
            Lock lk(m.isForced("color"));
            comboList("##color", &f.color, {T("ui.color.bt709tv"), T("ui.color.bt709pc"), T("ui.color.srgb"), T("ui.color.bt601")}, full * 0.66f);
        }
        tip("ui.tip.color");

        label("ui.lbl.blur");
        {
            Lock lk(m.isForced("blur"));
            comboList("##blur", &f.blur, {T("ui.opt.none"), "4", "8", "16"}, full * 0.30f);
        }
        tip("ui.tip.blur");

        label("ui.lbl.loop");
        ImGui::SetNextItemWidth(full * 0.30f);
        {
            Lock lk(m.isForced("loop"));
            ImGui::InputText("##loop", f.loop, sizeof(f.loop));
        }
        tip("ui.tip.loop");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::FAINT), "%s", T("ui.cue.loop").c_str());

        label("ui.lbl.fade_in");
        ImGui::SetNextItemWidth(full * 0.22f);
        {
            Lock lk(m.isForced("fadein"));
            ImGui::InputText("##fi", f.fadein, sizeof(f.fadein));
        }
        tip("ui.tip.fade_in");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colv(pal::MUTED), "%s", T("ui.lbl.fade_out").c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(full * 0.22f);
        {
            Lock lk(m.isForced("fadeout"));
            ImGui::InputText("##fo", f.fadeout, sizeof(f.fadeout));
        }
        tip("ui.tip.fade_out");

        label("ui.lbl.audio");
        ImGui::SetNextItemWidth(full - buttonWidth("ui.btn.browse", false) - S(8));
        {
            Lock lk(m.isForced("audio"));
            ImGui::InputTextWithHint("##audio", T("ui.cue.audio").c_str(), f.audio, sizeof(f.audio));
            tip("ui.tip.audio");
            ImGui::SameLine();
            if (button("ui.btn.browse", nullptr, 0, true, 0, "ui.tip.audio_browse"))
                openBrowser(a, BrowseMode::OpenFile, "dialog.audio_files", {L".mp3", L".wav", L".flac", L".ogg", L".m4a", L".aac"}, L"",
                            [&a](const std::vector<std::wstring>& r) {
                                if (!r.empty()) snprintf(a.model.f.audio, sizeof(a.model.f.audio), "%s", U8(r[0]).c_str());
                            });
        }

        label("ui.lbl.output");
        ImGui::SetNextItemWidth(full - buttonWidth("ui.btn.browse", false) - S(8));
        {
            Lock lk(m.isForced("outdir"));
            ImGui::InputTextWithHint("##out", T("ui.cue.output").c_str(), f.outdir, sizeof(f.outdir));
            tip("ui.tip.output");
            ImGui::SameLine();
            ImGui::PushID("outbrowse");
            if (button("ui.btn.browse", nullptr, 0, true, 0, "ui.tip.output_browse"))
                openBrowser(a, BrowseMode::PickFolder, "dialog.out_folder", {}, L"", [&a](const std::vector<std::wstring>& r) {
                    if (!r.empty()) snprintf(a.model.f.outdir, sizeof(a.model.f.outdir), "%s", U8(r[0]).c_str());
                });
            ImGui::PopID();
        }
        drawPostfx(a);
        drawOutput(a);
        drawParams(a);
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
    if (!m.pendingMedia.empty() && !ImGui::IsPopupOpen("##media")) ImGui::OpenPopup("##media");
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(440), 0));
    if (ImGui::BeginPopupModal("##media", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        // image, son ou video deposes : choix du canal ; le choix ecrit la ligne `// iChannelN=...` dans le shader selectionne
        if (m.pendingMedia.empty()) {
            ImGui::CloseCurrentPopup();
        } else {
            const std::wstring file = m.pendingMedia.front();
            const size_t cut = file.find_last_of(L"\\/");
            ImGui::PushFont(fontBold);
            ImGui::TextUnformatted(TXF8("ui.menu.drop_title", (cut == std::wstring::npos ? file : file.substr(cut + 1)).c_str()).c_str());
            ImGui::PopFont();
            ImGui::Spacing();
            bool done = false;
            for (int i = 0; i < 4 && !done; i++) {
                if (i) ImGui::SameLine();
                ImGui::PushID(i);
                if (button(("=" + TXF8("ui.menu.drop_channel", i)).c_str(), nullptr, 0, true)) {
                    m.applyMedia(file, i);
                    done = true;
                }
                ImGui::PopID();
            }
            if (!done && classifyDropped(file) == DropKind::Sound && button("ui.menu.drop_soundtrack", nullptr, 0, true)) {
                m.applyMedia(file, -1);
                done = true;
            }
            if (!done && (button("ui.btn.cancel", nullptr, 2, true) || ImGui::IsKeyPressed(ImGuiKey_Escape))) done = true;
            if (done) m.pendingMedia.erase(m.pendingMedia.begin());
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
    if (!popup && !editorOpen()) {  // raccourcis (désactivés pendant une fenêtre modale et dans l'éditeur : Suppr y efface du texte)
        const bool ctrl = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false) && !m.running())
            openBrowser(a, BrowseMode::OpenFiles, "dialog.shaders", {L".glsl", L".txt", L".json", L".montage"}, L"", [&a](const std::vector<std::wstring>& r) { a.model.addFiles(r); });
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) m.runPreview();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_E, false) && !editorOpen()) openEditor(m);
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
    drawShaders(a, std::floor(cardsH * 0.41f));
    drawPreview(a, std::floor(cardsH * 0.30f));
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
    drawEditor(a.model);
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
    {
        std::vector<uint8_t> logoPixels;
        int logoW = 0, logoH = 0;
        if (rasterizeIcon("logo", 64, logoPixels, logoW, logoH)) {
            GLFWimage logo{logoW, logoH, logoPixels.data()};
            glfwSetWindowIcon(win, 1, &logo);  // sans effet sous macOS (l'icône vient du paquet .app)
        }
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
    app.model.batches = loadBatches(exeDir);
    app.model.logHook = editorNoteLog;  // les erreurs du compilateur se marquent dans l'éditeur
    g_app = &app;
    glfwSetDropCallback(win, onDrop);
    app.model.appendLog(std::wstring(APP_NAME_WSTR) + L" " + APP_VER_WSTR);
    app.model.addFiles(files);
    if (files.empty() && g_shotPath.empty()) {  // shaders restes a rendre lors de la derniere session
        const std::vector<std::wstring> restored = loadQueue(exeDir);
        if (!restored.empty()) {
            app.model.addFiles(restored);
            app.model.appendLog(TXF("log.queue_restored", (int)restored.size()));
        }
    }
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
        glfwWaitEventsTimeout(shot ? 0.03 : app.model.running() ? 0.05 : app.model.playing() ? 0.016 : 0.2);
        if (app.model.attention) {  // fin de lot : la barre des taches / le Dock attire l'attention
            glfwRequestWindowAttention(win);
            app.model.attention = false;
        }
        frameNo++;
        if (shot && frameNo == 3 && !app.model.paths.empty()) {
            app.model.f.previewPos = 0.5f;
            app.model.runPreview();
        }
        if (shot && frameNo == 5 && g_shotAbout) app.showAbout = true;
        if (!app.drops.empty()) {
            app.model.dropFiles(app.drops);
            app.drops.clear();
        }
        app.model.pump();
        {  // progression dans le Dock (macOS) : seulement quand le pourcentage change
            static int lastPct = -2;
            const int pct = app.model.running() && !app.model.previewRun ? (int)(app.model.progress * 100.0f) : -1;
            if (pct != lastPct) {
                lastPct = pct;
                setDockProgress(pct < 0 ? -1.0f : pct / 100.0f);
            }
        }
        if (app.fontsDirty) {
            app.fontsDirty = false;
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            setupFonts(metrics.ui, metrics.fb);  // recree le texte japonais / chinois avec une police du systeme (NewFrame refait la texture)
        }
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
