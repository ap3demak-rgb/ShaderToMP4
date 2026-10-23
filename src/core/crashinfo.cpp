// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "crashinfo.h"
#include "util.h"
#include <deque>
#include <mutex>

namespace {
std::mutex mu;
std::deque<std::string> lines;
std::vector<std::pair<std::string, std::string>> info;

bool pathChar(char c) { return !(c == ' ' || c == '"' || c == '\'' || c == '<' || c == '>' || c == '|' || c == '\n' || c == '\r' || c == '\t'); }
bool sep(char c) { return c == '\\' || c == '/'; }
}  // namespace

std::string scrubPaths(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        const bool drive = i + 2 < text.size() && isalpha((unsigned char)c) && text[i + 1] == ':' && sep(text[i + 2]) && (i == 0 || !isalnum((unsigned char)text[i - 1]));
        const bool unc = i + 1 < text.size() && c == '\\' && text[i + 1] == '\\';
        if (drive || unc) {
            size_t j = i + (drive ? 3 : 2);
            size_t lastSep = j - 1;  // debut du dernier element du chemin
            const size_t nl = text.find_first_of("\r\n", j);
            const size_t eol = nl == std::string::npos ? text.size() : nl;
            while (j < eol) {
                if (sep(text[j])) lastSep = j;
                if (text[j] == ':' && j + 1 < eol && isdigit((unsigned char)text[j + 1])) break;  // « fichier:12: » garde son numero de ligne
                if (text[j] == ' ') {  // un espace ne finit le chemin que si le dernier element a deja une extension ou si aucun separateur ne suit
                    const bool hasExt = text.find('.', lastSep) < j;
                    size_t nextSep = j;
                    while (nextSep < eol && !sep(text[nextSep])) nextSep++;
                    if (hasExt || nextSep >= eol) break;
                } else if (!pathChar(text[j])) {
                    break;
                }
                j++;
            }
            while (j > i && (text[j - 1] == '.' || text[j - 1] == ',' || text[j - 1] == ':' || text[j - 1] == ';' || text[j - 1] == ')')) j--;  // ponctuation qui suit
            out += "<path>";
            i = j;
            continue;
        }
        out += c;
        i++;
    }
    return out;
}

void crashLog(const std::wstring& line) {
    std::string s = scrubPaths(U8(line));
    if (s.size() > 300) s.resize(300);
    std::lock_guard<std::mutex> lock(mu);
    lines.push_back(s);
    while (lines.size() > 60) lines.pop_front();
}

void crashSet(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mu);
    for (auto& kv : info)
        if (kv.first == key) {
            kv.second = value;
            return;
        }
    info.push_back({key, value});
}

std::string crashContext() {
    std::string out;
    std::unique_lock<std::mutex> lock(mu, std::try_to_lock);  // l'arret peut survenir pendant un verrou : on prend ce qu'on peut
    for (const auto& kv : info) out += kv.first + ": " + kv.second + "\r\n";
    out += "\r\nLog (last lines):\r\n";
    for (const std::string& l : lines) out += "  " + l + "\r\n";
    return out;
}
