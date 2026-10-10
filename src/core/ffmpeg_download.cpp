#include "ffmpeg_download.h"
#include "process.h"
#include "util.h"

static const wchar_t* kFfmpegUrl = L"https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip";
static const wchar_t* kFfmpegShaUrl = L"https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip.sha256";

bool ffmpegPresent(const std::wstring& exeDir) {
    if (fileExists(exeDir + L"ffmpeg.exe")) return true;
    return SearchPathW(nullptr, L"ffmpeg.exe", nullptr, 0, nullptr, nullptr) > 0;
}

static bool sha256File(const std::wstring& path, std::string& hex) {
    HCRYPTPROV prov = 0;
    HCRYPTHASH h = 0;
    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return false;
    bool ok = false;
    if (CryptCreateHash(prov, CALG_SHA_256, 0, 0, &h)) {
        if (FILE* f = _wfopen(path.c_str(), L"rb")) {
            std::vector<uint8_t> buf(1 << 20);
            size_t n;
            ok = true;
            while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
                if (!CryptHashData(h, buf.data(), (DWORD)n, 0)) { ok = false; break; }
            fclose(f);
        }
        BYTE d[32];
        DWORD len = sizeof(d);
        if (ok && CryptGetHashParam(h, HP_HASHVAL, d, &len, 0)) {
            char b[3];
            hex.clear();
            for (DWORD i = 0; i < len; i++) {
                snprintf(b, sizeof(b), "%02x", d[i]);
                hex += b;
            }
        } else {
            ok = false;
        }
        CryptDestroyHash(h);
    }
    CryptReleaseContext(prov, 0);
    return ok;
}

static bool httpGet(const std::wstring& url, const std::function<bool(const uint8_t*, size_t, unsigned long long)>& sink,
                    std::wstring& err) {
    HINTERNET net = InternetOpenW(L"ShaderToMP4/" L"" , INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) { err = L"Connexion Internet indisponible."; return false; }
    HINTERNET req = InternetOpenUrlW(net, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!req) {
        err = L"Impossible de joindre " + url;
        InternetCloseHandle(net);
        return false;
    }
    DWORD status = 0, sz = sizeof(status);
    HttpQueryInfoW(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &sz, nullptr);
    if (status != 200) {
        err = wfmt(L"Le serveur a repondu %lu pour %ls", (unsigned long)status, url.c_str());
        InternetCloseHandle(req);
        InternetCloseHandle(net);
        return false;
    }
    wchar_t lenBuf[32] = {0};
    DWORD lsz = sizeof(lenBuf), idx = 0;
    unsigned long long total = 0;
    if (HttpQueryInfoW(req, HTTP_QUERY_CONTENT_LENGTH, lenBuf, &lsz, &idx)) total = _wcstoui64(lenBuf, nullptr, 10);
    std::vector<uint8_t> buf(1 << 16);
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!InternetReadFile(req, buf.data(), (DWORD)buf.size(), &got)) { err = L"Telechargement interrompu."; ok = false; break; }
        if (got == 0) break;
        if (!sink(buf.data(), got, total)) { err = L"Telechargement annule."; ok = false; break; }
    }
    InternetCloseHandle(req);
    InternetCloseHandle(net);
    return ok;
}

bool downloadFfmpeg(const std::wstring& exeDir, const std::function<void(int, const std::wstring&)>& progress,
                           const std::atomic<bool>* cancel, std::wstring& err) {
    const std::wstring zip = exeDir + L"ffmpeg_download.zip", tmp = exeDir + L"ffmpeg_tmp";
    progress(0, L"Telechargement de l'empreinte SHA-256...");
    std::string shaText;
    if (!httpGet(kFfmpegShaUrl, [&](const uint8_t* d, size_t n, unsigned long long) {
            shaText.append((const char*)d, n);
            return shaText.size() < 4096;
        }, err))
        return false;
    std::string expected = lower(trim(shaText).substr(0, 64));
    if (expected.size() != 64) { err = L"Empreinte SHA-256 illisible."; return false; }

    FILE* f = _wfopen(zip.c_str(), L"wb");
    if (!f) { err = L"Impossible d'ecrire " + zip; return false; }
    unsigned long long got = 0;
    bool ok = httpGet(kFfmpegUrl, [&](const uint8_t* d, size_t n, unsigned long long total) {
        if (cancel && cancel->load()) return false;
        fwrite(d, 1, n, f);
        got += n;
        if (total) progress((int)(got * 90 / total), wfmt(L"Telechargement de ffmpeg : %.0f / %.0f Mo", got / 1048576.0, total / 1048576.0));
        return true;
    }, err);
    fclose(f);
    if (!ok) { DeleteFileW(zip.c_str()); return false; }

    progress(92, L"Verification de l'empreinte SHA-256...");
    std::string actual;
    if (!sha256File(zip, actual) || actual != expected) {
        DeleteFileW(zip.c_str());
        err = L"L'empreinte SHA-256 du fichier telecharge ne correspond pas : fichier rejete.";
        return false;
    }

    progress(95, L"Extraction...");
    removeTree(tmp);
    CreateDirectoryW(tmp.c_str(), nullptr);
    std::wstring tarLog;
    int rc = runProcess(L"tar.exe -xf \"" + zip + L"\" -C \"" + tmp + L"\"", [&](const std::wstring& l) { tarLog += l + L" "; });
    bool installed = false;
    if (rc == 0) {  // le zip contient ffmpeg-<version>-essentials_build\bin\ffmpeg.exe
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((tmp + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring cand = tmp + L"\\" + fd.cFileName + L"\\bin\\ffmpeg.exe";
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fileExists(cand))
                    installed = MoveFileExW(cand.c_str(), (exeDir + L"ffmpeg.exe").c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
            } while (!installed && FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    DeleteFileW(zip.c_str());
    removeTree(tmp);
    if (!installed) {
        err = rc != 0 ? L"Extraction impossible (tar.exe) : " + tarLog : L"ffmpeg.exe introuvable dans l'archive.";
        return false;
    }
    progress(100, L"ffmpeg.exe installe.");
    return true;
}
