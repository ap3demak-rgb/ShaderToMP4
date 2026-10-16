// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "parts.h"
#include "gl.h"
#include "i18n.h"
#include "util.h"
#include "platform/platform.h"
#include <set>

namespace {

double steadyNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::wstring partPath(const PartsPlan& p, long long i) { return p.partsDir + L"/" + wfmt(L"%05lld", i) + p.ext; }
std::wstring lockPath(const PartsPlan& p, long long i) { return p.farmDir + L"/locks/" + wfmt(L"%05lld", i) + L".lock"; }
std::wstring infoPath(const PartsPlan& p, long long i) { return p.farmDir + L"/info/" + wfmt(L"%05lld", i) + L".txt"; }

std::map<std::string, std::string> parseKV(const std::string& text) {
    std::map<std::string, std::string> m;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        const size_t eq = line.find('=');
        if (eq != std::string::npos) m[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return m;
}

// Heure du systeme de fichiers du dossier de la ferme (les ordinateurs n'ont pas forcement la meme horloge) : l'ecart avec l'horloge locale est mesure en
// ecrivant un fichier et en lisant sa date, puis garde 20 secondes
long long serverNow(const std::wstring& dir) {
    static std::mutex m;
    static double lastCheck = -1e9;
    static long long offset = 0;
    static std::wstring lastDir;
    std::lock_guard<std::mutex> l(m);
    if (steadyNow() - lastCheck > 20 || lastDir != dir) {
        const std::wstring probe = dir + L"/clock-" + farmMachineId() + L".tmp";
        long long t = 0;
        offset = writeFileAtomic(probe, "x") && platform::fileModTime(probe, t) ? t - (long long)time(nullptr) : 0;
        platform::removeFile(probe);
        lastCheck = steadyNow();
        lastDir = dir;
    }
    return (long long)time(nullptr) + offset;
}

std::string whoText() { return "machine=" + U8(farmMachineId()) + "\n"; }

// Un verrou est repris quand sa derniere ecriture date de plus de `timeout` secondes (horloge du dossier partage)
bool lockIsStale(const std::wstring& farmDir, const std::wstring& lock, double timeout, long long* ageOut = nullptr) {
    long long mt = 0;
    if (!platform::fileModTime(lock, mt)) return false;
    const long long age = serverNow(farmDir) - mt;
    if (ageOut) *ageOut = age;
    return (double)age > timeout;
}

// Renomme le verrou perime sous un nom unique (un seul ordinateur y parvient), le supprime, puis le recree : vrai si c'est celui-ci qui l'a repris
bool takeOverLock(const std::wstring& lock, const std::string& content) {
    const std::wstring gone = lock + L".stale." + farmMachineId();
    if (!platform::moveFile(lock, gone, true)) return false;
    platform::removeFile(gone);
    return platform::createExclusive(lock, content);
}

}  // namespace

std::wstring farmMachineId() { return platform::hostName() + L"-" + std::to_wstring(platform::processId()); }

// Renouvelle (reecrit) les verrous tenus par ce processus tant qu'il tourne : un verrou qui ne bouge plus est celui d'un ordinateur eteint
struct LockKeeper {
    std::mutex m;
    std::set<std::wstring> paths;
    std::atomic<bool> stop{false};
    std::thread th;
    std::string who;
    double interval;
    LockKeeper(double iv, std::string w) : who(std::move(w)), interval(iv) { th = std::thread([this] { loop(); }); }
    ~LockKeeper() {
        stop = true;
        if (th.joinable()) th.join();
    }
    void add(const std::wstring& p) {
        std::lock_guard<std::mutex> l(m);
        paths.insert(p);
    }
    void remove(const std::wstring& p) {
        std::lock_guard<std::mutex> l(m);
        paths.erase(p);
    }
    void loop() {
        double last = steadyNow();
        while (!stop) {
            platform::sleepMs(50);
            if (steadyNow() - last < interval) continue;
            last = steadyNow();
            std::lock_guard<std::mutex> l(m);
            for (const auto& p : paths) writeFileAtomic(p, who + "renewed=" + std::to_string((long long)time(nullptr)) + "\n");
        }
    }
};

namespace {

// --- File des parties ---------------------------------------------------------------------------------------------------------------------------

struct Board {
    virtual ~Board() {}
    // Prochaine partie a rendre ; -1 si aucune n'est a prendre pour l'instant (finished = vrai : il n'y en a plus du tout)
    virtual long long acquire(int worker, bool& finished) = 0;
    // Vrai si ce rendu est le premier a finir la partie (son fichier doit etre mis en place)
    virtual bool complete(long long i) = 0;
    virtual void finalize(long long i, int worker, double seconds, long long frames) = 0;
    virtual void release(long long i) = 0;     // la partie n'a pas ete rendue : elle redevient libre
    virtual bool superseded(long long i) = 0;  // un autre rendu a fini cette partie : celui-ci peut s'arreter
};

// Parties de ce processus seulement : file en memoire, une partie lente est aussi confiee a un contexte libre (le plus rapide gagne)
struct LocalBoard : Board {
    const PartsPlan& plan;
    double specAfter;
    std::function<void(long long)> onSpec;
    std::mutex m;
    std::vector<char> st, spec;
    std::vector<double> t0, durs;
    std::vector<int> att;
    LocalBoard(const PartsPlan& p, double after, std::function<void(long long)> cb) : plan(p), specAfter(after), onSpec(std::move(cb)) {
        const size_t n = (size_t)p.nChunks;
        st.assign(n, 0);
        spec.assign(n, 0);
        t0.assign(n, 0.0);
        att.assign(n, 0);
        for (size_t i = 0; i < n; i++)
            if (fileExists(partPath(p, (long long)i))) st[i] = 2;
    }
    long long acquire(int, bool& finished) override {
        std::lock_guard<std::mutex> l(m);
        finished = false;
        bool all = true;
        for (size_t i = 0; i < st.size(); i++) {
            if (st[i] == 0) {
                st[i] = 1;
                att[i] = 1;
                t0[i] = steadyNow();
                return (long long)i;
            }
            if (st[i] != 2) all = false;
        }
        if (all) {
            finished = true;
            return -1;
        }
        double med = 0;
        if (!durs.empty()) {
            std::vector<double> d = durs;
            std::nth_element(d.begin(), d.begin() + (long)d.size() / 2, d.end());
            med = d[d.size() / 2];
        }
        const double limit = std::max(specAfter, 2.5 * med), now = steadyNow();
        long long best = -1;
        double bestAge = 0;
        for (size_t i = 0; i < st.size(); i++)
            if (st[i] == 1 && !spec[i] && now - t0[i] > limit && now - t0[i] > bestAge) {
                best = (long long)i;
                bestAge = now - t0[i];
            }
        if (best >= 0) {
            spec[(size_t)best] = 1;
            att[(size_t)best]++;
            onSpec(best);
        }
        return best;
    }
    bool complete(long long i) override {
        std::lock_guard<std::mutex> l(m);
        if (st[(size_t)i] == 2) return false;
        st[(size_t)i] = 2;
        durs.push_back(steadyNow() - t0[(size_t)i]);
        return true;
    }
    void finalize(long long, int, double, long long) override {}
    void release(long long i) override {
        std::lock_guard<std::mutex> l(m);
        if (att[(size_t)i] > 0) att[(size_t)i]--;
        if (st[(size_t)i] == 1 && att[(size_t)i] == 0) {
            st[(size_t)i] = 0;
            spec[(size_t)i] = 0;
        }
    }
    bool superseded(long long i) override {
        std::lock_guard<std::mutex> l(m);
        return st[(size_t)i] == 2;
    }
};

// Parties d'un dossier partage : une partie est reservee par la creation atomique de locks/NNNNN.lock
struct FarmBoard : Board {
    const PartsPlan& plan;
    LockKeeper& keeper;
    std::function<void(const std::wstring&)> log;
    std::string who;
    FarmBoard(const PartsPlan& p, LockKeeper& k, std::function<void(const std::wstring&)> lg) : plan(p), keeper(k), log(std::move(lg)), who(whoText()) {}
    long long acquire(int, bool& finished) override {
        finished = false;
        bool all = true;
        for (long long i = 0; i < plan.nChunks; i++) {
            if (fileExists(partPath(plan, i))) continue;
            all = false;
            const std::wstring lock = lockPath(plan, i);
            bool got = platform::createExclusive(lock, who);
            long long age = 0;
            if (!got && lockIsStale(plan.farmDir, lock, plan.lockTimeout, &age)) {
                got = takeOverLock(lock, who);
                if (got) log(TXF("log.farm_takeover", i, (int)age));
            }
            if (!got) continue;
            if (fileExists(partPath(plan, i))) {  // terminee entre-temps par un autre ordinateur
                platform::removeFile(lock);
                continue;
            }
            keeper.add(lock);
            return i;
        }
        finished = all;
        return -1;
    }
    bool complete(long long i) override { return !fileExists(partPath(plan, i)); }
    void finalize(long long i, int worker, double seconds, long long frames) override {
        writeFileAtomic(infoPath(plan, i), who + "worker=" + std::to_string(worker) + "\nseconds=" + std::to_string(seconds) + "\nframes=" + std::to_string(frames) +
                                               "\nfinished=" + std::to_string(serverNow(plan.farmDir)) + "\n");
        release(i);
    }
    void release(long long i) override {
        keeper.remove(lockPath(plan, i));
        platform::removeFile(lockPath(plan, i));
    }
    bool superseded(long long) override { return false; }
};

}  // namespace

// --- Dossier de la ferme : travail commun ---------------------------------------------------------------------------------------------------------

static bool openJob(const PartsPlan& plan, std::wstring& err) {
    for (const wchar_t* sub : {L"", L"/parts", L"/locks", L"/info"}) {
        const std::wstring d = plan.farmDir + sub;
        if (!platform::makeDir(d) && !platform::dirExists(d)) {
            err = TXF("error.farm_folder", d.c_str());
            return false;
        }
    }
    std::string mine;
    for (const auto& kv : plan.jobInfo) mine += kv.first + "=" + kv.second + "\n";
    mine += "frames_total=" + std::to_string(plan.totalFrames) + "\nchunk_frames=" + std::to_string(plan.chunkFrames) + "\nparts=" + std::to_string(plan.nChunks) + "\n";
    const std::wstring job = plan.farmDir + L"/job.txt";
    if (platform::createExclusive(job, mine + "created=" + std::to_string(serverNow(plan.farmDir)) + "\ncreated_by=" + U8(farmMachineId()) + "\n")) return true;
    std::string theirs;
    for (int t = 0; t < 30 && trim(theirs).empty(); t++) {
        if (!readFile(job, theirs)) theirs.clear();
        if (trim(theirs).empty()) platform::sleepMs(100);
    }
    const std::map<std::string, std::string> a = parseKV(mine), b = parseKV(theirs);
    if (b.empty()) {
        err = TXF("error.farm_folder", job.c_str());
        return false;
    }
    // l'ordre des controles donne le message le plus utile : un autre travail, puis une autre version, puis un autre ffmpeg, puis un autre encodeur
    static const char* const kOrder[] = {"signature", "version", "ffmpeg", "encoder", "format", "frames_total", "chunk_frames", "parts"};
    for (const char* k : kOrder) {
        const auto mineIt = a.find(k), jobIt = b.find(k);
        if (mineIt == a.end() || jobIt == b.end() || mineIt->second == jobIt->second) continue;
        const std::string key = k;
        if (key == "signature") err = TXF("error.farm_signature", W(mineIt->second).c_str(), W(jobIt->second).c_str());
        else if (key == "version") err = TXF("error.farm_version", W(jobIt->second).c_str(), W(mineIt->second).c_str());
        else if (key == "ffmpeg") err = TXF("error.farm_ffmpeg", W(jobIt->second).c_str(), W(mineIt->second).c_str());
        else err = TXF("error.farm_codec", W(jobIt->second).c_str(), W(mineIt->second).c_str());
        return false;
    }
    return true;
}

// Assemblage : le premier ordinateur qui voit toutes les parties prend assemble.lock ; les autres attendent assembled.txt
static Result claimAssembly(const PartsPlan& plan, const Hooks& H, PartsStats& stats, const std::shared_ptr<LockKeeper>& keeper, const std::function<void(const std::wstring&)>& log) {
    const std::wstring lock = plan.farmDir + L"/assemble.lock", done = plan.farmDir + L"/assembled.txt";
    const std::string who = whoText();
    double lastLog = 0;
    for (;;) {
        std::string text;
        if (readFile(done, text)) {
            const std::map<std::string, std::string> m = parseKV(text);
            stats.assembler = false;
            stats.assembledBy = m.count("machine") ? W(m.at("machine")) : L"?";
            return Result::Ok;
        }
        if (platform::createExclusive(lock, who)) {
            keeper->add(lock);
            stats.assembler = true;
            stats.keeper = keeper;
            return Result::Ok;
        }
        if (lockIsStale(plan.farmDir, lock, plan.lockTimeout) && takeOverLock(lock, who)) {
            keeper->add(lock);
            stats.assembler = true;
            stats.keeper = keeper;
            return Result::Ok;
        }
        if (H.cancelled()) return Result::Cancelled;
        if (steadyNow() - lastLog > 30) {
            lastLog = steadyNow();
            log(TX("log.farm_waiting_assembly"));
        }
        platform::sleepMs(500);
    }
}

void farmAbandon(const PartsPlan& plan, PartsStats& stats) {
    if (!stats.keeper) return;
    stats.keeper->remove(plan.farmDir + L"/assemble.lock");
    platform::removeFile(plan.farmDir + L"/assemble.lock");
    stats.keeper.reset();
}

void farmAssembled(const PartsPlan& plan, PartsStats& stats, const std::wstring& output, const std::function<void(const std::wstring&)>& log) {
    if (plan.farmDir.empty()) return;
    // compte rendu agrege : ce que chaque ordinateur a rendu (fiches info/NNNNN.txt)
    struct Machine {
        int parts = 0;
        double seconds = 0;
        long long frames = 0;
    };
    std::map<std::string, Machine> machines;
    double totalSeconds = 0;
    long long totalFrames = 0;
    for (long long i = 0; i < plan.nChunks; i++) {
        std::string text;
        if (!readFile(infoPath(plan, i), text)) continue;
        const std::map<std::string, std::string> m = parseKV(text);
        const std::string who = m.count("machine") ? m.at("machine") : "?";
        Machine& mc = machines[who];
        mc.parts++;
        mc.seconds += m.count("seconds") ? atof(m.at("seconds").c_str()) : 0.0;
        mc.frames += m.count("frames") ? atoll(m.at("frames").c_str()) : 0;
        totalSeconds += m.count("seconds") ? atof(m.at("seconds").c_str()) : 0.0;
        totalFrames += m.count("frames") ? atoll(m.at("frames").c_str()) : 0;
    }
    std::string json = "{\n  \"assembled_by\": \"" + U8(farmMachineId()) + "\",\n  \"output\": \"";
    for (char c : U8(output)) json += c == '\\' || c == '"' ? std::string("\\") + c : std::string(1, c);
    json += "\",\n  \"parts\": " + std::to_string(plan.nChunks) + ",\n  \"frames\": " + std::to_string(plan.totalFrames) + ",\n  \"render_seconds_total\": " + std::to_string(totalSeconds) + ",\n  \"machines\": [";
    bool first = true;
    for (const auto& kv : machines) {
        char b[256];
        snprintf(b, sizeof(b), "%s\n    {\"machine\": \"%s\", \"parts\": %d, \"render_seconds\": %.2f, \"frames\": %lld}", first ? "" : ",", kv.first.c_str(), kv.second.parts, kv.second.seconds, kv.second.frames);
        json += b;
        first = false;
    }
    json += "\n  ]\n}\n";
    const std::wstring report = plan.farmDir + L"/farm-report.json";
    if (writeFileAtomic(report, json)) log(TXF("log.farm_report", report.c_str()));
    (void)totalFrames;
    writeFileAtomic(plan.farmDir + L"/assembled.txt", "machine=" + U8(farmMachineId()) + "\noutput=" + U8(output) + "\nfinished=" + std::to_string(serverNow(plan.farmDir)) + "\n");
    farmAbandon(plan, stats);
}

bool farmStatus(const std::wstring& farmDir, double lockTimeout, std::vector<std::wstring>& lines) {
    lines.clear();
    std::string jobText;
    if (!readFile(farmDir + L"/job.txt", jobText)) return false;
    const std::map<std::string, std::string> job = parseKV(jobText);
    const long long parts = job.count("parts") ? atoll(job.at("parts").c_str()) : 0, frames = job.count("chunk_frames") ? atoll(job.at("chunk_frames").c_str()) : 0;
    if (parts <= 0) return false;
    PartsPlan plan;
    plan.farmDir = farmDir;
    plan.partsDir = farmDir + L"/parts";
    plan.nChunks = parts;
    plan.ext = L"." + W(job.count("format") ? job.at("format") : std::string("mkv"));
    // l'extension des parties est celle des fichiers presents (mkv par defaut)
    std::vector<platform::DirEntry> files;
    platform::listDir(plan.partsDir, files);
    for (const auto& f : files) {
        const size_t dot = f.name.find_last_of(L'.');
        if (dot != std::wstring::npos && f.name.size() > 5 && f.name.find(L".part") == std::wstring::npos && iswdigit(f.name[0])) {
            plan.ext = f.name.substr(dot);
            break;
        }
    }
    lines.push_back(TXF("farm.status_title", farmDir.c_str()));
    lines.push_back(TXF("farm.status_job", parts, frames, W(job.count("version") ? job.at("version") : std::string("?")).c_str()));
    struct Machine {
        int parts = 0;
        double seconds = 0;
        long long frames = 0;
    };
    std::map<std::string, Machine> machines;
    std::vector<std::wstring> running;
    std::set<std::string> active;
    int done = 0, busy = 0;
    double sumSeconds = 0;
    int counted = 0;
    for (long long i = 0; i < parts; i++) {
        if (fileExists(partPath(plan, i))) {
            done++;
            std::string text;
            if (readFile(infoPath(plan, i), text)) {
                const std::map<std::string, std::string> m = parseKV(text);
                Machine& mc = machines[m.count("machine") ? m.at("machine") : "?"];
                mc.parts++;
                const double sec = m.count("seconds") ? atof(m.at("seconds").c_str()) : 0.0;
                mc.seconds += sec;
                mc.frames += m.count("frames") ? atoll(m.at("frames").c_str()) : 0;
                sumSeconds += sec;
                counted++;
            }
            continue;
        }
        std::string text;
        long long age = 0;
        if (readFile(lockPath(plan, i), text)) {
            const std::map<std::string, std::string> m = parseKV(text);
            platform::fileModTime(lockPath(plan, i), age);
            age = serverNow(farmDir) - age;
            const std::wstring who = m.count("machine") ? W(m.at("machine")) : L"?";
            const bool stale = (double)age > lockTimeout;
            running.push_back(TXF(stale ? "farm.status_part_stale" : "farm.status_part_running", i, who.c_str(), (int)age));
            if (!stale) {
                busy++;
                active.insert(m.count("machine") ? m.at("machine") : "?");
            }
        }
    }
    const int freeParts = (int)parts - done - busy;
    lines.push_back(TXF("farm.status_counts", done, busy, freeParts));
    for (const auto& r : running) lines.push_back(r);
    for (const auto& kv : machines)
        lines.push_back(TXF("farm.status_machine", W(kv.first).c_str(), kv.second.parts, kv.second.seconds, kv.second.seconds > 0 ? (double)kv.second.frames / kv.second.seconds : 0.0));
    std::string assembled;
    if (readFile(farmDir + L"/assembled.txt", assembled)) {
        const std::map<std::string, std::string> m = parseKV(assembled);
        lines.push_back(TXF("farm.status_assembled", W(m.count("machine") ? m.at("machine") : std::string("?")).c_str()));
    } else if (done < parts && counted > 0) {
        const double perPart = sumSeconds / counted;
        const int workers = std::max<int>(1, (int)active.size());
        lines.push_back(TXF("farm.status_eta", fmtTime(perPart * (double)(parts - done) / workers).c_str(), (int)active.size()));
    } else if (done < parts) {
        lines.push_back(TX("farm.status_eta_unknown"));
    }
    return true;
}

// --- Rendu des parties -------------------------------------------------------------------------------------------------------------------------

Result runParts(const PartsPlan& plan, const Hooks& H, const RenderPartFn& render, std::wstring& err, PartsStats& stats) {
    stats = PartsStats();
    platform::makeDir(plan.partsDir);
    const bool farm = !plan.farmDir.empty();
    std::mutex logM;
    auto plog = [&](const std::wstring& t) {
        std::lock_guard<std::mutex> l(logM);
        if (H.log) H.log(t);
    };
    if (farm) {
        if (!openJob(plan, err)) return Result::Failed;
        std::string text;
        if (readFile(plan.farmDir + L"/assembled.txt", text)) {  // ce travail est deja fini
            const std::map<std::string, std::string> m = parseKV(text);
            stats.assembler = false;
            stats.alreadyAssembled = true;
            stats.assembledBy = m.count("machine") ? W(m.at("machine")) : L"?";
            return Result::Ok;
        }
        plog(TXF("log.farm_joined", plan.farmDir.c_str(), farmMachineId().c_str(), plan.nChunks));
    }
    for (long long i = 0; i < plan.nChunks; i++)
        if (fileExists(partPath(plan, i))) stats.resumed++;
    const int workers = std::max(1, std::min<int>(plan.workers, farm ? 16 : (int)std::max<long long>(1, plan.nChunks - stats.resumed)));
    std::shared_ptr<LockKeeper> keeper;
    std::unique_ptr<Board> board;
    std::atomic<int> redistributed{0};
    if (farm) {
        keeper = std::make_shared<LockKeeper>(std::max(0.2, std::min(5.0, plan.lockTimeout / 4)), whoText());
        board.reset(new FarmBoard(plan, *keeper, plog));
    } else {
        board.reset(new LocalBoard(plan, H.speculateAfter, [&](long long i) {
            redistributed++;
            plog(TXF("log.parts_redistributed", i + 1));
        }));
    }
    if (workers > 1 || farm) plog(TXF("log.parts_start", workers, plan.nChunks, plan.chunkFrames));

    // progression d'ensemble : parties finies + images deja rendues de chaque partie en cours
    std::mutex progM;
    std::atomic<long long> doneFrames{0};
    std::vector<std::atomic<long long>> inprog((size_t)workers);
    for (auto& a : inprog) a = 0;
    for (long long i = 0; i < plan.nChunks; i++)
        if (fileExists(partPath(plan, i))) doneFrames += std::min(plan.totalFrames, (i + 1) * plan.chunkFrames) - i * plan.chunkFrames;
    const long long startFrames = doneFrames;
    const double startTime = steadyNow();
    auto report = [&]() {
        if (!H.progress) return;
        std::lock_guard<std::mutex> l(progM);
        long long cur = doneFrames;
        for (auto& a : inprog) cur += a;
        const double el = std::max(0.001, steadyNow() - startTime), fps = (double)(cur - startFrames) / el;
        H.progress(std::min(cur, plan.totalFrames), plan.totalFrames, fps, fps > 0 ? (double)(plan.totalFrames - cur) / fps : 0.0);
    };
    report();

    std::unique_ptr<std::atomic<bool>[]> wcancel(new std::atomic<bool>[(size_t)workers]);
    std::vector<std::atomic<long long>> wchunk((size_t)workers);
    for (int w = 0; w < workers; w++) {
        wcancel[(size_t)w] = false;
        wchunk[(size_t)w] = -1;
    }
    std::mutex errM;
    std::wstring lastErr;
    std::atomic<int> retired{0}, done{0};
    const std::wstring tag = farmMachineId();

    auto retire = [&](int w, const std::wstring& why) {
        retired++;
        plog(TXF("log.parts_retired", w + 1, why.c_str()));
    };
    auto workerMain = [&](int w) {
        Hooks Hw;
        Hw.log = [](const std::wstring&) {};
        Hw.cancel = &wcancel[(size_t)w];
        Hw.pause = H.pause;
        Hw.progress = [&, w](long long k, long long, double, double) {
            inprog[(size_t)w] = k;
            report();
        };
        for (;;) {
            if (H.cancelled()) break;
            bool finished = false;
            const long long i = board->acquire(w, finished);
            if (i < 0) {
                if (finished) break;
                platform::sleepMs(farm ? 400 : 60);
                continue;
            }
            wchunk[(size_t)w] = i;
            wcancel[(size_t)w] = false;
            const long long n0 = i * plan.chunkFrames, n1 = std::min(plan.totalFrames, n0 + plan.chunkFrames);
            const std::wstring tmp = plan.partsDir + L"/" + wfmt(L"%05lld", i) + L"." + tag + wfmt(L".w%d", w) + L".part" + plan.ext;
            const double t0 = steadyNow();
            if (H.injectFailure && H.injectFailure(w, i)) {
                wchunk[(size_t)w] = -1;
                board->release(i);
                retire(w, TX("log.parts_injected"));
                break;
            }
            if (H.injectDelay) {
                int ms = H.injectDelay(w, i);
                while (ms > 0 && !wcancel[(size_t)w] && !H.cancelled()) {
                    platform::sleepMs(20);
                    ms -= 20;
                }
            }
            std::wstring e;
            Result r = wcancel[(size_t)w] ? Result::Cancelled : render(w, i, n0, n1, tmp, Hw, e);
            wchunk[(size_t)w] = -1;
            inprog[(size_t)w] = 0;
            if (H.cancelled()) {
                platform::removeFile(tmp);
                board->release(i);
                break;
            }
            if (r == Result::Cancelled) {  // une autre copie de cette partie a fini avant : rien a garder
                platform::removeFile(tmp);
                board->release(i);
                continue;
            }
            if (r != Result::Ok) {
                platform::removeFile(tmp);
                board->release(i);
                {
                    std::lock_guard<std::mutex> l(errM);
                    lastErr = e;
                }
                retire(w, e);
                break;
            }
            if (board->complete(i)) {
                if (!platform::moveFile(tmp, partPath(plan, i), true)) {
                    platform::removeFile(tmp);
                    board->release(i);
                    {
                        std::lock_guard<std::mutex> l(errM);
                        lastErr = TXF("error.part_finalize", plan.partsDir.c_str());
                    }
                    retire(w, TXF("error.part_finalize", plan.partsDir.c_str()));
                    break;
                }
                doneFrames += n1 - n0;
                done++;
                board->finalize(i, w, steadyNow() - t0, n1 - n0);
                report();
            } else {
                platform::removeFile(tmp);
            }
        }
    };

    std::atomic<bool> stopMonitor{false};
    std::thread monitor([&] {  // une partie finie par un autre contexte arrete les copies plus lentes ; l'annulation de l'appelant arrete tout
        while (!stopMonitor) {
            for (int w = 0; w < workers; w++) {
                const long long i = wchunk[(size_t)w];
                wcancel[(size_t)w] = H.cancelled() || (i >= 0 && board->superseded(i));
            }
            platform::sleepMs(40);
        }
    });
    std::vector<std::thread> threads;
    for (int w = 1; w < workers; w++)
        threads.emplace_back([&, w] {
            GLContext gl;
            std::wstring e;
            if (!createGLContext(gl, e)) {
                retired++;
                plog(TXF("log.parts_context_failed", w + 1, e.c_str()));
                return;
            }
            workerMain(w);
            destroyGLContext(gl);
        });
    workerMain(0);
    for (auto& t : threads) t.join();
    stopMonitor = true;
    monitor.join();
    stats.partsDone = done;
    stats.retired = retired;
    stats.redistributed = redistributed;
    if (H.cancelled()) return Result::Cancelled;
    bool all = true;
    for (long long i = 0; i < plan.nChunks; i++)
        if (!fileExists(partPath(plan, i))) all = false;
    if (!all) {
        std::lock_guard<std::mutex> l(errM);
        err = TXF("error.parts_failed", lastErr.empty() ? TX("log.parts_no_context").c_str() : lastErr.c_str());
        return Result::Failed;
    }
    plog(TXF("log.parts_summary", stats.partsDone, stats.resumed, stats.retired, stats.redistributed));
    if (farm) {
        const Result r = claimAssembly(plan, H, stats, keeper, plog);
        if (r != Result::Ok) return r;
        if (stats.assembler) plog(TX("log.farm_assembling"));
        else plog(TXF("log.farm_assembled_by", stats.assembledBy.c_str()));
    }
    return Result::Ok;
}
