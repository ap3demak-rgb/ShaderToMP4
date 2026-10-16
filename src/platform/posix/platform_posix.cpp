// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Couche plateforme, version Linux et macOS : fichiers, système, processus, OpenGL (GLFW).
#include "platform/platform.h"
#include "platform/posix/egl_headless.h"
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

bool fileSize(const std::wstring& path, uint64_t& bytes) {
    struct stat st;
    if (stat(U8(path).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    bytes = (uint64_t)st.st_size;
    return true;
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

bool createExclusive(const std::wstring& path, const std::string& data) {
    const int fd = open(U8(path).c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) return false;
    const bool ok = data.empty() || write(fd, data.data(), data.size()) == (ssize_t)data.size();
    close(fd);
    if (!ok) unlink(U8(path).c_str());
    return ok;
}

bool fileModTime(const std::wstring& path, long long& unixSeconds) {
    struct stat st;
    if (stat(U8(path).c_str(), &st) != 0) return false;
    unixSeconds = (long long)st.st_mtime;
    return true;
}

std::wstring hostName() {
    char b[256] = {0};
    if (gethostname(b, sizeof(b) - 1) != 0 || !b[0]) return L"pc";
    return W(b);
}

long processId() { return (long)getpid(); }

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

void applyGpuPreference(const std::string& pref) {
#ifdef __APPLE__
    (void)pref;  // macOS choisit le GPU lui-meme
#else
    if (pref == "high") {  // GPU dedie : Mesa (PRIME) et le pilote NVIDIA proprietaire (offload)
        setenv("DRI_PRIME", "1", 1);
        setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
        setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
    } else if (pref == "low") {  // GPU integre
        setenv("DRI_PRIME", "0", 1);
    }
#endif
}

DateTime localTime() {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    return {lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec};
}

std::wstring systemFontFile() {
    static const char* const kCandidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf", "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf"};
    for (const char* p : kCandidates)
        if (fileExists(W(p))) return W(p);
    return L"";
}

std::wstring systemCjkFontFile(const std::string& lang, int& fontNo) {
    // Noto Sans CJK (.ttc : JP = 0, KR = 1, SC = 2, TC = 3), WenQuanYi, Droid, IPA / Takao, et les polices de macOS
    struct Candidate { const char* path; int jp, sc; };
    static const Candidate kCandidates[] = {
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0, 2}, {"/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", 0, 2},
        {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0, 2}, {"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", 0, 2},
        {"/usr/share/fonts/google-noto/NotoSansCJK-Regular.ttc", 0, 2}, {"/usr/share/fonts/OTF/NotoSansCJK-Regular.ttc", 0, 2},
        {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", 0, 0}, {"/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc", 0, 0},
        {"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", 0, 0}, {"/usr/share/fonts/truetype/takao-gothic/TakaoGothic.ttf", 0, 0},
        {"/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf", 0, 0}, {"/System/Library/Fonts/PingFang.ttc", 0, 0},
        {"/System/Library/Fonts/Hiragino Sans GB.ttc", 0, 0}, {"/System/Library/Fonts/STHeiti Medium.ttc", 0, 0},
        {"/System/Library/Fonts/\xE3\x83\x92\xE3\x83\xA9\xE3\x82\xAE\xE3\x83\x8E\xE8\xA7\x92\xE3\x82\xB4\xE3\x82\xB7\xE3\x83\x83\xE3\x82\xAF W3.ttc", 0, 0},
        {"/Library/Fonts/Arial Unicode.ttf", 0, 0}};
    for (const Candidate& c : kCandidates) {
        if (!fileExists(W(c.path))) continue;
        fontNo = lang == "ja" ? c.jp : c.sc;
        return W(c.path);
    }
    fontNo = 0;
    return L"";
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

// --- OpenGL (GLFW ; EGL sans serveur d'affichage quand GLFW ne peut pas s'ouvrir) -------------------------------------
namespace {
GLFWwindow* g_shared = nullptr;
bool g_useEgl = false;  // les contextes viennent d'EGL (aucun serveur d'affichage) : les fonctions OpenGL aussi
std::string g_glfwError;  // dernier message de GLFW, joint aux erreurs de création de contexte
void onGlfwError(int, const char* description) { g_glfwError = description ? description : ""; }
struct PosixGL {
    GLFWwindow* win = nullptr;
    bool owned = false;
    void* eglContext = nullptr;  // contexte EGL de ce thread (aucun serveur d'affichage)
};
bool useEglContext(PosixGL* c, std::wstring& err) {
    c->eglContext = egl::createContext(err);
    if (!c->eglContext) return false;
    g_useEgl = true;
    return egl::makeCurrent(c->eglContext, err);
}
}  // namespace

void setSharedGLWindow(void* glfwWindow) { g_shared = (GLFWwindow*)glfwWindow; }

bool createGLContext(void*& ctx, std::wstring& err) {
    auto* c = new PosixGL;
    ctx = c;
    if (g_shared) {
        c->win = g_shared;
    } else if (egl::available()) {
        return useEglContext(c, err);
    } else {
        glfwSetErrorCallback(onGlfwError);
        if (!glfwInit()) {  // ni X11 ni Wayland : contexte EGL sans fenetre (ferme de rendu, conteneur)
            std::wstring eglErr;
            if (egl::open(eglErr)) return useEglContext(c, err);
            err = TX("error.gl_window") + L" (" + W(g_glfwError) + L" ; " + eglErr + L")";
            return false;
        }
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

namespace {
GLFWwindow* g_ownedShared = nullptr;  // fenetre creee par prepareOffThreadContexts
}  // namespace

bool multiContext() { return false; }

bool prepareOffThreadContexts() {
    if (g_shared) return true;  // l'interface en a deja confie une
    if (egl::available()) return true;
    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) {  // sans serveur d'affichage : le contexte EGL unique sert a tous les threads de rendu
        std::wstring eglErr;
        return egl::open(eglErr);
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    g_ownedShared = glfwCreateWindow(64, 64, "ShaderToMP4", nullptr, nullptr);
    if (!g_ownedShared) return false;
    g_shared = g_ownedShared;
    return true;
}

void releaseOffThreadContexts() {
    if (!g_ownedShared) return;
    if (g_shared == g_ownedShared) g_shared = nullptr;
    glfwDestroyWindow(g_ownedShared);
    g_ownedShared = nullptr;
}

void destroyGLContext(void*& ctx) {
    auto* c = (PosixGL*)ctx;
    if (!c) return;
    if (c->eglContext) {
        egl::release();
        egl::destroyContext(c->eglContext);
        delete c;
        ctx = nullptr;
        return;
    }
    glfwMakeContextCurrent(nullptr);
    if (c->owned && c->win) {
        glfwDestroyWindow(c->win);
        glfwTerminate();
    }
    delete c;
    ctx = nullptr;
}

void* glProc(const char* name) { return g_useEgl ? egl::proc(name) : (void*)glfwGetProcAddress(name); }

}  // namespace platform
