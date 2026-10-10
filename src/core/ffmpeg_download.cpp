// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "ffmpeg_download.h"
#include "i18n.h"
#include "process.h"
#include "util.h"
#include "platform/platform.h"

// Source du téléchargement selon le système (builds statiques, empreinte SHA-256 publiée par la source).
struct Source {
    const wchar_t* url;
    const wchar_t* shaUrl;    // fichier contenant l'empreinte (une seule, ou une liste « empreinte  nom »)
    const wchar_t* archive;   // nom de l'archive dans la liste d'empreintes
    const wchar_t* ffmpegIn;  // chemin de ffmpeg dans l'archive, sous le dossier racine
};

#if defined(_WIN32)
static const Source kSource = {L"https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip",
                               L"https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip.sha256",
                               L"ffmpeg-release-essentials.zip", L"bin/ffmpeg.exe"};
static const bool kSupported = true;
#elif defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
static const Source kSource = {L"https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linuxarm64-gpl.tar.xz",
                               L"https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/checksums.sha256",
                               L"ffmpeg-master-latest-linuxarm64-gpl.tar.xz", L"bin/ffmpeg"};
static const bool kSupported = true;
#elif defined(__linux__)
static const Source kSource = {L"https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linux64-gpl.tar.xz",
                               L"https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/checksums.sha256",
                               L"ffmpeg-master-latest-linux64-gpl.tar.xz", L"bin/ffmpeg"};
static const bool kSupported = true;
#else  // macOS : aucune source publiant une empreinte SHA-256 vérifiable, donc pas de téléchargement automatique
static const Source kSource = {L"", L"", L"", L""};
static const bool kSupported = false;
#endif

using platform::httpGet;
using platform::sha256File;

bool ffmpegDownloadSupported() { return kSupported; }

bool ffmpegPresent(const std::wstring& exeDir) {
    if (fileExists(exeDir + platform::exeFileName(L"ffmpeg"))) return true;
    return !platform::findInPath(platform::exeFileName(L"ffmpeg")).empty();
}

// Empreinte de `name` dans `text` : liste « empreinte  nom » ou empreinte seule
static std::string findHash(const std::string& text, const std::string& name) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.size() >= 64 && line.find(name) != std::string::npos) return lower(line.substr(0, 64));
    }
    const std::string t = trim(text);
    return t.size() >= 64 && t.find('\n') == std::string::npos ? lower(t.substr(0, 64)) : std::string();
}

bool downloadFfmpeg(const std::wstring& exeDir, const std::function<void(int, const std::wstring&)>& progress,
                           const std::atomic<bool>* cancel, std::wstring& err) {
    if (!kSupported) { err = TX("error.download_unsupported"); return false; }
    const std::wstring zip = exeDir + L"ffmpeg_download.tmp", tmp = exeDir + L"ffmpeg_tmp";
    const std::wstring exeName = platform::exeFileName(L"ffmpeg");
    progress(0, TX("dl.sha_download"));
    std::string shaText;
    if (!httpGet(kSource.shaUrl, [&](const uint8_t* d, size_t n, unsigned long long) {
            shaText.append((const char*)d, n);
            return shaText.size() < (1u << 20);
        }, err))
        return false;
    std::string expected = findHash(shaText, U8(kSource.archive));
    if (expected.size() != 64) { err = TX("error.sha_unreadable"); return false; }

    FILE* f = platform::openFile(zip, "wb");
    if (!f) { err = TXF("error.write_failed", zip.c_str()); return false; }
    unsigned long long got = 0;
    bool ok = httpGet(kSource.url, [&](const uint8_t* d, size_t n, unsigned long long total) {
        if (cancel && cancel->load()) return false;
        fwrite(d, 1, n, f);
        got += n;
        if (total) progress((int)(got * 90 / total), TXF("dl.progress", got / 1048576.0, total / 1048576.0));
        return true;
    }, err);
    fclose(f);
    if (!ok) { platform::removeFile(zip); return false; }

    progress(92, TX("dl.verify"));
    std::string actual;
    if (!sha256File(zip, actual) || actual != expected) {
        platform::removeFile(zip);
        err = TX("error.sha_mismatch");
        return false;
    }

    progress(95, TX("dl.extract"));
    removeTree(tmp);
    platform::makeDir(tmp);
    std::wstring tarLog;
    int rc = runProcess(L"tar -xf \"" + zip + L"\" -C \"" + tmp + L"\"", [&](const std::wstring& l) { tarLog += l + L" "; });
    bool installed = false;
    if (rc == 0) {  // le zip contient ffmpeg-<version>-essentials_build\bin\ffmpeg.exe
        std::vector<platform::DirEntry> list;
        platform::listDir(tmp, list);
        for (const auto& en : list) {
            const std::wstring cand = tmp + platform::pathSeparator() + en.name + platform::pathSeparator() + kSource.ffmpegIn;
            if (en.isDir && fileExists(cand)) {
                installed = platform::moveFile(cand, exeDir + exeName, true);
                if (installed) break;
            }
        }
    }
    platform::removeFile(zip);
    removeTree(tmp);
    if (!installed) {
        err = rc != 0 ? TXF("error.extract_failed", tarLog.c_str()) : TX("error.ffmpeg_not_in_archive");
        return false;
    }
    progress(100, TX("dl.installed"));
    return true;
}
