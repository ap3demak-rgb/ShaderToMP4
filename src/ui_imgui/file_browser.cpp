// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "ui_imgui/file_browser.h"
#include "core/util.h"
#include "imgui.h"
#include "platform/platform.h"
#include "ui_imgui/widgets.h"

namespace ui {

static std::wstring joinPath(const std::wstring& dir, const std::wstring& name) {
    if (!dir.empty() && dir.back() == L'/') return dir + name;
    return dir + L"/" + name;
}

static std::wstring parentOf(const std::wstring& dir) {
    std::wstring d = dir;
    while (d.size() > 1 && d.back() == L'/') d.pop_back();
    size_t s = d.find_last_of(L'/');
    if (s == std::wstring::npos || s == 0) return L"/";
    return d.substr(0, s);
}

void FileBrowser::open(BrowseMode mode, const std::string& titleKey, const std::wstring& startDir, const std::vector<std::wstring>& extensions,
                       const std::wstring& defaultName, std::function<void(const std::vector<std::wstring>&)> onDone) {
    mode_ = mode;
    titleKey_ = titleKey;
    exts_ = extensions;
    onDone_ = std::move(onDone);
    dir_ = platform::dirExists(startDir) ? startDir : platform::homeDir();
    picked_.clear();
    snprintf(name_, sizeof(name_), "%s", U8(defaultName).c_str());
    openRequested_ = true;
    refresh();
}

bool FileBrowser::matches(const std::wstring& name) const {
    if (exts_.empty()) return true;
    const std::wstring low = W(lower(U8(name)));
    for (const auto& e : exts_)
        if (low.size() >= e.size() && low.compare(low.size() - e.size(), e.size(), e) == 0) return true;
    return false;
}

void FileBrowser::refresh() {
    dirs_.clear();
    files_.clear();
    std::vector<platform::DirEntry> list;
    platform::listDir(dir_, list);
    for (const auto& e : list) {
        if (!showHidden_ && !e.name.empty() && e.name[0] == L'.') continue;
        if (e.isDir) dirs_.push_back(e.name);
        else if (mode_ != BrowseMode::PickFolder && matches(e.name)) files_.push_back(e.name);
    }
    auto byName = [](const std::wstring& a, const std::wstring& b) { return lower(U8(a)) < lower(U8(b)); };
    std::sort(dirs_.begin(), dirs_.end(), byName);
    std::sort(files_.begin(), files_.end(), byName);
}

void FileBrowser::draw() {
    if (titleKey_.empty()) return;
    const std::string title = T(titleKey_.c_str()) + "###filebrowser";
    if (openRequested_) {
        ImGui::OpenPopup(title.c_str());
        openRequested_ = false;
    }
    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(vp.x - S(40), S(720)), std::min(vp.y - S(40), S(520))));
    bool keepOpen = true;
    if (!ImGui::BeginPopupModal(title.c_str(), &keepOpen, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        if (!keepOpen) titleKey_.clear();
        return;
    }
    // Barre du haut : dossier parent, accueil, chemin courant
    if (ui::button("ui.fb.up", nullptr, 0, true)) {
        dir_ = parentOf(dir_);
        refresh();
    }
    ImGui::SameLine();
    if (ui::button("ui.fb.home", nullptr, 0, true)) {
        dir_ = platform::homeDir();
        refresh();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(T("ui.fb.hidden").c_str(), &showHidden_)) refresh();
    ImGui::TextColored(colv(pal::MUTED), "%s", U8(dir_).c_str());

    const float footer = ImGui::GetFrameHeightWithSpacing() * (mode_ == BrowseMode::SaveFile ? 2.4f : 1.4f) + S(10);
    ImGui::BeginChild("##fblist", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    std::wstring enterDir;
    for (const auto& d : dirs_) {
        if (ImGui::Selectable(("> " + U8(d) + "##d").c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) enterDir = d;
        }
    }
    for (const auto& f : files_) {
        const std::wstring full = joinPath(dir_, f);
        const bool multi = mode_ == BrowseMode::OpenFiles;
        const bool sel = std::find(picked_.begin(), picked_.end(), full) != picked_.end();
        if (ImGui::Selectable((U8(f) + "##f").c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (mode_ == BrowseMode::SaveFile) {
                snprintf(name_, sizeof(name_), "%s", U8(f).c_str());
            } else if (multi) {
                if (sel) picked_.erase(std::find(picked_.begin(), picked_.end(), full));
                else picked_.push_back(full);
            } else {
                picked_.assign(1, full);
            }
            if (ImGui::IsMouseDoubleClicked(0) && !multi && mode_ != BrowseMode::SaveFile) {
                onDone_(picked_);
                titleKey_.clear();
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::EndChild();
    if (!enterDir.empty()) {
        dir_ = joinPath(dir_, enterDir);
        picked_.clear();
        refresh();
    }

    if (mode_ == BrowseMode::SaveFile) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##name", name_, sizeof(name_));
    }
    const bool canOk = mode_ == BrowseMode::PickFolder || (mode_ == BrowseMode::SaveFile ? name_[0] != 0 : !picked_.empty());
    if (ui::button("ui.fb.select", nullptr, 1, canOk)) {
        std::vector<std::wstring> result;
        if (mode_ == BrowseMode::PickFolder) result.push_back(dir_);
        else if (mode_ == BrowseMode::SaveFile) result.push_back(joinPath(dir_, W(name_)));
        else result = picked_;
        onDone_(result);
        titleKey_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::button("ui.btn.cancel", nullptr, 0, true) || !keepOpen) {
        titleKey_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace ui
