// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Couche plateforme, version Linux et macOS : fichiers, système, processus, OpenGL (GLFW).
#include "platform/platform.h"
#include "platform/posix/glfw_bridge.h"
#include "core/util.h"
#include "core/i18n.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#endif

extern char** environ;

namespace platform {

// --- Données embarquées (produites par CMake : cmake/embed.cmake) ------------------------------------------------
struct EmbeddedFile {
    const char* name;
    const unsigned char* data;
    size_t size;
};
extern const EmbeddedFile kEmbeddedFiles[];
extern const size_t kEmbeddedFileCount;

bool embeddedData(const char* name, std::string& out) {
    for (size_t i = 0; i < kEmbeddedFileCount; i++) {
        if (!strcmp(kEmbeddedFiles[i].name, name)) {
            out.assign((const char*)kEmbeddedFiles[i].data, kEmbeddedFiles[i].size);
            return true;
        }
    }
    return false;
}

// --- Chemins et fichiers -------------------------------------------------------------------------------------------
std::wstring appDir() {
    std::string exe;
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string raw(size, '\0');
    if (_NSGetExecutablePath(&raw[0], &size) == 0) {
        char real[4096];
        exe = realpath(raw.c_str(), real) ? real : raw.c_str();
    }
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) exe.assign(buf, (size_t)n);
#endif
    size_t slash = exe.find_last_of('/');
    std::string dir = slash == std::string::npos ? "./" : exe.substr(0, slash + 1);
    // Dans un paquet .app, le dossier « de l'application » est celui qui contient le .app (config.ini, ffmpeg, lang/ à côté)
    const std::string marker = ".app/Contents/MacOS/";
    size_t m = dir.find(marker);
    if (m != std::string::npos) {
        size_t s = dir.find_last_of('/', m);
        dir = dir.substr(0, s == std::string::npos ? 0 : s + 1);
    }
    return W(dir);
}

bool fileExists(const std::wstring& path) {
    struct stat st;
    return stat(U8(path).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool dirExists(const std::wstring& path) {
    struct stat st;
    return stat(U8(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

FILE* openFile(const std::wstring& path, const char* mode) { return fopen(U8(path).c_str(), mode); }

bool readFile(const std::wstring& path, std::string& out) {
    FILE* f = openFile(path, "rb");
    if (!f) return false;
    out.clear();
    char buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    const bool ok = !ferror(f);
    fclose(f);
    return ok;
}

bool makeDir(const std::wstring& path) { return mkdir(U8(path).c_str(), 0777) == 0 || errno == EEXIST; }
bool removeFile(const std::wstring& path) { return unlink(U8(path).c_str()) == 0; }
bool removeDir(const std::wstring& path) { return rmdir(U8(path).c_str()) == 0; }

bool moveFile(const std::wstring& from, const std::wstring& to, bool overwrite) {
    if (!overwrite && access(U8(to).c_str(), F_OK) == 0) return false;
    return rename(U8(from).c_str(), U8(to).c_str()) == 0;
}

bool freeSpace(const std::wstring& path, uint64_t& bytes) {
    struct statvfs v;
    if (statvfs(U8(path).c_str(), &v) != 0) return false;
    bytes = (uint64_t)v.f_bavail * v.f_frsize;
    return true;
}

bool listDir(const std::wstring& path, std::vector<DirEntry>& out) {
    const std::string dir = U8(path);
    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        struct stat st;
        const bool isDir = stat((dir + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        out.push_back({W(n), isDir});
    }
    closedir(d);
    return true;
}

std::wstring homeDir() {
    const char* h = getenv("HOME");
    return h && *h ? W(h) : appDir();
}

wchar_t pathSeparator() { return L'/'; }

// --- Système -------------------------------------------------------------------------------------------------------
std::string systemLanguage() {
#ifdef __APPLE__
    std::string code;
    CFArrayRef langs = CFLocaleCopyPreferredLanguages();
    if (langs) {
        if (CFArrayGetCount(langs) > 0) {
            char b[64] = {};
            if (CFStringGetCString((CFStringRef)CFArrayGetValueAtIndex(langs, 0), b, sizeof(b), kCFStringEncodingUTF8)) code = b;
        }
        CFRelease(langs);
    }
    if (code.size() >= 2) return lower(code.substr(0, 2));
#endif
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* v = getenv(var);
        if (v && *v && strcmp(v, "C") != 0 && strcmp(v, "POSIX") != 0 && strlen(v) >= 2) return lower(std::string(v, 2));
    }
    return "en";
}

std::wstring exeFileName(const std::wstring& baseName) { return baseName; }

std::wstring findInPath(const std::wstring& fileName) {
    const char* p = getenv("PATH");
    std::string path = p ? p : "", name = U8(fileName);
#ifdef __APPLE__
    path += ":/opt/homebrew/bin:/usr/local/bin";  // une application lancée depuis le Finder n'hérite pas du PATH du terminal
#endif
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(':', start);
        if (end == std::string::npos) end = path.size();
        std::string dir = path.substr(start, end - start);
        if (dir.empty()) dir = ".";
        if (access((dir + "/" + name).c_str(), X_OK) == 0) return W(dir + "/" + name);
        start = end + 1;
    }
    return {};
}

// Lance un programme sans attendre ; un thread récupère son code de sortie pour ne pas laisser de zombie.
static void spawnDetached(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) == 0)
        std::thread([pid] {
            int st;
            waitpid(pid, &st, 0);
        }).detach();
}

void openWithDefault(const std::wstring& pathOrUrl) {
#ifdef __APPLE__
    spawnDetached({"open", U8(pathOrUrl)});
#else
    spawnDetached({"xdg-open", U8(pathOrUrl)});
#endif
}

void revealInFolder(const std::wstring& filePath) {
#ifdef __APPLE__
    spawnDetached({"open", "-R", U8(filePath)});
#else
    size_t s = filePath.find_last_of(L'/');
    openWithDefault(s == std::wstring::npos ? L"." : filePath.substr(0, s));
#endif
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

DateTime localTime() {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    return {lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec};
}

void showMessage(const std::wstring& title, const std::wstring& text, MessageKind) {
    fprintf(stderr, "%s\n%s\n", U8(title).c_str(), U8(text).c_str());
}

// --- Processus -----------------------------------------------------------------------------------------------------
struct Process::Impl {
    pid_t pid = -1;
    int in = -1;   // écriture vers stdin du programme
    int out = -1;  // lecture de stdout/stderr
    bool waited = false;
    int code = 1;
};

Process::Process(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

static void closeFd(int& fd) {
    if (fd >= 0) close(fd);
    fd = -1;
}

static int makePipe(int fds[2]) {
    if (pipe(fds) != 0) return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
}

std::unique_ptr<Process> Process::start(const std::vector<std::wstring>& argvW, bool pipeStdin, bool captureOutput) {
    if (argvW.empty()) return nullptr;
    signal(SIGPIPE, SIG_IGN);  // une écriture vers un programme terminé doit échouer, pas tuer le nôtre
    int inP[2] = {-1, -1}, outP[2] = {-1, -1};
    if (pipeStdin && makePipe(inP) != 0) return nullptr;
    if (captureOutput && makePipe(outP) != 0) {
        closeFd(inP[0]);
        closeFd(inP[1]);
        return nullptr;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (pipeStdin) posix_spawn_file_actions_adddup2(&fa, inP[0], 0);
    else posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (captureOutput) {
        posix_spawn_file_actions_adddup2(&fa, outP[1], 1);
        posix_spawn_file_actions_adddup2(&fa, outP[1], 2);
    }
    std::vector<std::string> args;
    for (const auto& a : argvW) args.push_back(U8(a));
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(&a[0]);
    argv.push_back(nullptr);
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    closeFd(inP[0]);
    closeFd(outP[1]);
    if (rc != 0) {
        closeFd(inP[1]);
        closeFd(outP[0]);
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->pid = pid;
    impl->in = inP[1];
    impl->out = outP[0];
    return std::unique_ptr<Process>(new Process(std::move(impl)));
}

Process::~Process() {
    if (!impl_) return;
    closeFd(impl_->in);
    if (!impl_->waited) {
        kill(impl_->pid, SIGKILL);
        int st;
        waitpid(impl_->pid, &st, 0);
    }
    closeFd(impl_->out);
}

bool Process::writeStdin(const uint8_t* data, size_t size) {
    if (impl_->in < 0) return false;
    while (size > 0) {
        ssize_t n = write(impl_->in, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        data += n;
        size -= (size_t)n;
    }
    return true;
}

void Process::closeStdin() { closeFd(impl_->in); }

size_t Process::readOutput(char* buf, size_t capacity) {
    if (impl_->out < 0) return 0;
    for (;;) {
        ssize_t n = read(impl_->out, buf, capacity);
        if (n < 0 && errno == EINTR) continue;
        return n > 0 ? (size_t)n : 0;
    }
}

void Process::terminate() {
    if (!impl_->waited) kill(impl_->pid, SIGKILL);
}

int Process::wait() {
    if (!impl_->waited) {
        int st = 0;
        while (waitpid(impl_->pid, &st, 0) < 0 && errno == EINTR) {
        }
        impl_->waited = true;
        impl_->code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }
    return impl_->code;
}

// --- Réseau --------------------------------------------------------------------------------------------------------
// Pas de bibliothèque réseau embarquée : on s'appuie sur curl, présent sur macOS et sur la quasi-totalité des Linux.
bool httpGet(const std::wstring& url, const std::function<bool(const uint8_t*, size_t, unsigned long long)>& sink, std::wstring& err) {
    auto p = Process::start({L"/bin/sh", L"-c", L"exec curl -fsSL --proto '=https' --tlsv1.2 \"$0\" 2>/dev/null", url}, false, true);
    if (!p) { err = TX("error.net_unavailable"); return false; }
    std::vector<char> buf(1 << 16);
    size_t n;
    while ((n = p->readOutput(buf.data(), buf.size())) > 0) {
        if (!sink((const uint8_t*)buf.data(), n, 0)) {
            err = TX("error.net_cancelled");
            return false;  // le destructeur arrête curl
        }
    }
    const int rc = p->wait();
    if (rc != 0) {
        err = rc == 127 ? TX("error.net_unavailable") : TXF("error.net_unreachable", url.c_str());
        return false;
    }
    return true;
}

// --- OpenGL (GLFW) -------------------------------------------------------------------------------------------------
namespace {
GLFWwindow* g_shared = nullptr;
std::string g_glfwError;  // dernier message de GLFW, joint aux erreurs de création de contexte
void onGlfwError(int, const char* description) { g_glfwError = description ? description : ""; }
struct PosixGL {
    GLFWwindow* win = nullptr;
    bool owned = false;
};
}  // namespace

void setSharedGLWindow(void* glfwWindow) { g_shared = (GLFWwindow*)glfwWindow; }

bool createGLContext(void*& ctx, std::wstring& err) {
    auto* c = new PosixGL;
    ctx = c;
    if (g_shared) {
        c->win = g_shared;
    } else {
        glfwSetErrorCallback(onGlfwError);
        if (!glfwInit()) { err = TX("error.gl_window") + L" (" + W(g_glfwError) + L")"; return false; }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        c->win = glfwCreateWindow(64, 64, "ShaderToMP4", nullptr, nullptr);
        c->owned = true;
        if (!c->win) { err = TX("error.gl_core") + L" (" + W(g_glfwError) + L")"; return false; }
    }
    glfwMakeContextCurrent(c->win);
    return true;
}

void destroyGLContext(void*& ctx) {
    auto* c = (PosixGL*)ctx;
    if (!c) return;
    glfwMakeContextCurrent(nullptr);
    if (c->owned && c->win) {
        glfwDestroyWindow(c->win);
        glfwTerminate();
    }
    delete c;
    ctx = nullptr;
}

void* glProc(const char* name) { return (void*)glfwGetProcAddress(name); }

}  // namespace platform
