// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "fuzz.h"
#include "encoder.h"
#include "exr.h"
#include "expr.h"
#include "filesettings.h"
#include "json.h"
#include "keyboard.h"
#include "locate.h"
#include "lut.h"
#include "montage.h"
#include "mouse.h"
#include "params.h"
#include "postfx.h"
#include "publish.h"
#include "quality.h"
#include "queue.h"
#include "project.h"
#include "history.h"
#include "keys.h"
#include "rhythm.h"
#include "settings.h"
#include "shadertoy_import.h"
#include "tifffloat.h"
#include "timemap.h"
#include "util.h"
#include "platform/platform.h"
#include <atomic>
#include <chrono>
#include <thread>

namespace {

std::vector<FuzzTarget>& registry() {
    static std::vector<FuzzTarget> r;
    return r;
}

uint64_t next(uint64_t& s) {  // xorshift64*
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ULL;
}

size_t below(uint64_t& rng, size_t n) { return n ? (size_t)(next(rng) % n) : 0; }

const char* kTokens[] = {"0", "-1", "99999999999999999999", "1e308", "-1e308", "1e-320", "nan", "inf", "-inf", "2147483648", "4294967295", "65536", "%s%n", "\\", "\"", "'",
                         "[[[[[[[[[[[[[[[[", "{{{{{{{{{{{{{{{{", "]]]]", "}}}}", "\r\n", "\n\n\n", "\t", "=", ":", ";", "#", "//", "..", "0x7fffffff", "00000000000000000000000000"};

std::string tempFile(const FuzzEnv& env, const std::string& target, const std::wstring& ext) { return U8(env.dir) + "fuzz_last_" + target + U8(ext.empty() ? std::wstring(L".bin") : ext); }

bool writeBytes(const std::wstring& path, const std::string& data) {
    FILE* f = platform::openFile(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

std::string repeatStr(const std::string& s, int n) {
    std::string out;
    for (int i = 0; i < n; i++) out += s;
    return out;
}

// --- Cibles -------------------------------------------------------------------------------------------------------------------------------

std::vector<FuzzTarget> builtins() {
    std::vector<FuzzTarget> t;
    t.push_back({"json", L"", {"{\"a\":1,\"b\":[true,false,null,\"x\\u00e9\\n\"],\"c\":{\"d\":-1.5e3}}", "[1,2,[3,[4,[5]]]]", "{\"info\":{\"name\":\"t\",\"username\":\"u\",\"id\":\"abc\"},\"renderpass\":[{\"type\":\"image\",\"code\":\"void mainImage(out vec4 c,in vec2 p){c=vec4(1);}\",\"inputs\":[]}]}", "\"just a string\"", "  123  "}, nullptr,
                 [](const std::string& in, const FuzzEnv&) {
                     JsonValue j;
                     std::string e;
                     if (parseJson(in, j, e)) {
                         j.str("a");
                         j.num("b");
                         if (const JsonValue* c = j.get("c")) c->str("d");
                     }
                 }});
    t.push_back({"ini", L"", {"width=1280\nheight=720\nfps=30\ncodec=h264 ; commentaire\nduration=1:30\noutdir=C:\\videos\nssaa=2\n", "// fps=60\n// duration=6\n// iChannel0=noise\n// iChannel1=bufferA\n// uniform float k = 1.0 [0..2]\n", "; jobs\ncodec=h265\ncrf=18\nmotion=on\n"}, nullptr,
                 [](const std::string& in, const FuzzEnv&) {
                     Settings a, b;
                     parseLines(a, in, false);
                     parseLines(b, in, true);
                     sameSettings(a, b);
                 }});
    t.push_back({"jobs", L"", {"; C:\\shaders\\a.glsl\r\nwidth=1920\r\nheight=1080\r\nfps=30\r\ncodec=h264\r\n", "crf=18\r\npreset=slow\r\nvariant=draft\r\n"}, nullptr,
                 [](const std::string& in, const FuzzEnv& env) {  // jobs/<nom>-<empreinte>.ini : reglages propres a un shader
                     const std::wstring shader = L"C:/fuzz/shader.glsl";
                     const std::wstring path = fileSettingsPath(env.dir, shader);
                     platform::makeDir(path.substr(0, path.find_last_of(L"\\/")));
                     writeBytes(path, in);
                     Settings s = settingsForFile(Settings(), env.dir, shader);
                     (void)s;
                 }});
    t.push_back({"queue", L"", {"C:\\a\\one.glsl\nC:\\a\\two.glsl\n", "\n\n/home/x/y.glsl\r\n"}, nullptr, [](const std::string& in, const FuzzEnv& env) {
                     writeBytes(env.dir + L"queue.txt", in);
                     loadQueue(env.dir);
                 }});
    t.push_back({"project", L".s2mproj", {"{\"format\":\"ShaderToMP4 project\",\"version\":1,\"name\":\"p\",\"general\":[\"width=1280\",\"height=720\",\"outdir=videos\"],\"shaders\":[{\"path\":\"a.glsl\",\"batch\":2,\"settings\":[\"fps=24\"],\"passes\":[\"a.bufferA.glsl\"],\"media\":[\"tex/x.png\"]},{\"path\":\"C:/s/b.glsl\"}]}", "{\"shaders\":[\"a.glsl\",\"b.glsl\"],\"settings\":{\"width\":640,\"fps\":30,\"alpha\":true}}", "{\"version\":99,\"shaders\":[]}"}, nullptr,
                 [](const std::string& in, const FuzzEnv& env) {
                     Project p;
                     std::string err;
                     if (projectFromJson(in, env.dir, p, err)) {
                         projectMissing(p);
                         const std::string again = projectToJson(p, env.dir);
                         Project q;
                         if (!projectFromJson(again, env.dir, q, err)) throw std::runtime_error("project: written file is not readable");
                         projectSettings(p.general, Settings());
                     }
                 }});
    t.push_back({"shortcuts", L".ini", {"; raccourcis\nstart=F5\nproject_save=Ctrl+S\nundo=Ctrl+Shift+Z\nhelp=\n", "a=Ctrl+A\nb=Ctrl+A\nc=Hyper+X\nsans egal\n=F1\nstart=Ctrl+\n"}, nullptr,
                 [](const std::string& in, const FuzzEnv&) {
                     const std::vector<KeyAction> acts = {{"start", "F5"}, {"project_save", "Ctrl+S"}, {"undo", "Ctrl+Z"}, {"help", "F1"}, {"a", ""}, {"b", ""}};
                     std::vector<std::string> problems;
                     const KeyMap m = parseKeys(in, acts, problems);
                     if (!keyConflicts(m).empty()) throw std::runtime_error("keys: conflict kept");
                     std::vector<std::string> again;
                     if (parseKeys(keysTemplate(m), acts, again).binding != m.binding) throw std::runtime_error("keys: template not stable");
                     filterNames({L"Ouvrir", L"Fermer"}, W(in));
                 }});
    t.push_back({"history", L".jsonl", {"{\"time\":\"2026-10-24 10:00:00\",\"shader\":\"C:/a.glsl\",\"output\":\"C:/a.mp4\",\"seconds\":3.5,\"hash\":\"abc\",\"result\":\"ok\",\"settings\":[\"width=640\",\"fps=30\"]}\n", "{\"shader\":\"x\",\"result\":\"failed\"}\ngarbage\n[1,2]\n"}, nullptr,
                 [](const std::string& in, const FuzzEnv& env) {
                     writeBytes(env.dir + L"history.jsonl", in);
                     loadHistory(env.dir);
                     HistoryEntry e;
                     parseHistoryLine(in, e);
                 }});
    t.push_back({"montage", L".montage",{"# montage\nset width=64\nset height=64\nset fps=10\nset codec=h264\nduration=2\na.glsl\n\"a.glsl\" 2 crossfade 0.5\na.glsl 2 cut\na.glsl 2 fade 1\n", "set audio=song.mp3\nset beatsync=bar\na.glsl 4\n"}, nullptr,
                 [](const std::string&, const FuzzEnv& env) {
                     MontageDef def;
                     std::wstring err;
                     parseMontage(env.path, 5.0, def, err);
                 }});
    t.push_back({"cube", L".cube", {"# LUT 1D\nLUT_1D_SIZE 3\n0 0 0\n0.25 0.25 0.25\n1 1 1\n", "TITLE \"x\"\nLUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n"}, nullptr,
                 [](const std::string&, const FuzzEnv& env) {
                     Lut l;
                     std::wstring err;
                     loadCubeLut(env.path, l, err);
                 }});
    {
        FuzzTarget x{"exr", L".exr", {}, [](const FuzzEnv& env) {
                         std::vector<std::string> out;
                         for (exr::Compression c : {exr::Compression::None, exr::Compression::Zips, exr::Compression::Zip}) {
                             exr::Image img;
                             img.width = 5;
                             img.height = 4;
                             for (const char* n : {"R", "G", "B", "depth.Z"}) {
                                 exr::Channel ch;
                                 ch.name = n;
                                 ch.full = std::string(n) == "depth.Z";
                                 for (int i = 0; i < 20; i++) ch.pixels.push_back(0.05f * (float)i);
                                 img.channels.push_back(ch);
                             }
                             img.text.push_back({"comment", "fuzz"});
                             img.numbers.push_back({"depthNear", 0.1f});
                             std::wstring e;
                             const std::wstring p = env.dir + L"fuzz_seed.exr";
                             std::string raw;
                             if (exr::write(p, img, c, e) && readFile(p, raw)) out.push_back(raw);
                         }
                         return out;
                     }, [](const std::string&, const FuzzEnv& env) {
                         exr::Image img;
                         std::wstring err;
                         exr::read(env.path, img, err);
                     }};
        t.push_back(x);
    }
    {
        FuzzTarget x{"tifffloat", L".tif", {}, [](const FuzzEnv& env) {
                         std::vector<std::string> out;
                         for (int samples : {3, 4}) {
                             tifffloat::Image img;
                             img.width = 4;
                             img.height = 3;
                             img.samples = samples;
                             img.premultiplied = samples == 4;
                             img.pixels.assign((size_t)img.width * img.height * samples, 0.5f);
                             std::wstring e;
                             const std::wstring p = env.dir + L"fuzz_seed.tif";
                             std::string raw;
                             if (tifffloat::write(p, img, e) && readFile(p, raw)) out.push_back(raw);
                         }
                         return out;
                     }, [](const std::string&, const FuzzEnv& env) {
                         tifffloat::Image img;
                         std::wstring err;
                         tifffloat::read(env.path, img, err);
                     }};
        t.push_back(x);
    }
    {
        FuzzTarget x{"beats", L"", {}, [](const FuzzEnv& env) {  // fiche <son>.beats.json : valide pour le fichier de son factice ci-dessous
                         std::string audio(600, 'x');
                         for (size_t i = 0; i < audio.size(); i++) audio[i] = (char)(i * 7);
                         writeBytes(env.dir + L"fuzz_audio.wav", audio);
                         const std::string key = std::to_string(audio.size()) + "-" + fnvHex(audio);
                         std::string cols;
                         const char* names[] = {"peak", "energy", "bass", "mid", "treble", "flux_bass", "flux_mid", "flux_treble"};
                         for (int i = 0; i < 8; i++) {
                             std::string raw;
                             for (int k = 0; k < 24; k++) raw += (char)((k * (i + 3)) % 200);
                             cols += std::string("    \"") + names[i] + "\": \"" + base64Encode(raw) + (i < 7 ? "\",\n" : "\"\n");
                         }
                         return std::vector<std::string>{"{\n  \"shadertomp4_beats\": 1,\n  \"key\": \"" + key + "\",\n  \"duration\": 2.4,\n  \"user\": {\"added\": [0.5], \"removed\": [1.0, 1.5]},\n  \"features\": {\n" + cols + "  }\n}\n"};
                     }, [](const std::string& in, const FuzzEnv& env) {
                         writeBytes(env.dir + L"fuzz_audio.wav.beats.json", in);
                         Rhythm r;
                         std::wstring err;
                         r.load(L"fuzz_no_ffmpeg.exe", env.dir + L"fuzz_audio.wav", 0, 4, err);
                         if (r.loaded()) {
                             r.at(0.7);
                             r.signature();
                         }
                     }};
        t.push_back(x);
    }
    t.push_back({"params", L"", {"// uniform float speed = 1.0 [0..4]\n// uniform vec3 tint = vec3(1.0, 0.5, 0.0)\n// uniform bool on = true\nvoid mainImage(out vec4 c, in vec2 p) { c = vec4(1); }\n",
                                  "// uniform float k = iBass*0.5+0.2 attack=0.02 release=0.3 [0..1]\n// uniform float t = smooth 0:1 ; 4:3\n", "// uniform int n = 3 [1..9]\n// uniform color c = #ff8800\n"}, nullptr,
                 [](const std::string& in, const FuzzEnv&) {
                     ParamSet p;
                     Settings s;
                     std::wstring err;
                     p.load(in, s, L"", err);
                 }});
    t.push_back({"expr", L"", {"iBass*0.5+0.2", "max(iBeat, 0.5) - smoothstep(0, 1, iEnergy) * 2 ^ 2 / 4", "sin(iTime*pi)+clamp(iOnset,0,1)", "mix(0, 1, fract(iTime))"}, nullptr,
                 [](const std::string& in, const FuzzEnv&) {
                     Expr e;
                     std::string err;
                     if (e.parse(in, err)) {
                         ExprEnv env;
                         env.v[0] = 1.5;
                         e.eval(env);
                     }
                 }});
    t.push_back({"timemap", L"", {"0:0;4:2;8:10", "0:5;4:0", "hermite 0:0@1 ; 2:1@0 ; 4:3", "0:0;2:1;4:1;6:3"}, nullptr, [](const std::string& in, const FuzzEnv&) {
                     TimeMap tm;
                     std::wstring err;
                     if (TimeMap::parse(in, tm, err)) {
                         tm.at(1.0);
                         tm.speedAt(2.0);
                         tm.monotone();
                     }
                 }});
    t.push_back({"post", L"", {"bloom 0.5; grain 0.2; vignette 0.3", "blur 2.5 ; sharpen 1", "lut=curve.cube"}, nullptr, [](const std::string& in, const FuzzEnv&) {
                     std::vector<PostEffect> v;
                     std::wstring unknown;
                     parsePostEffects(in, v, unknown);
                 }});
    t.push_back({"targets", L"", {"[mine]\nlabel=Ma cible\nwidth=720\nheight=1280\nmaxduration=30\nmaxsize=8\ncodec=h264\ncontainer=mp4\n", "[a]\n[b]\nwidth=-1\n"}, nullptr, [](const std::string& in, const FuzzEnv&) {
                     std::vector<Target> v;
                     parseTargets(in, v);
                     double mb = 0;
                     parseSizeMB(in, mb);
                 }});
    t.push_back({"keys", L"", {"2:a;3.5:Space;4:Left", "1:ctrl+shift+A"}, nullptr, [](const std::string& in, const FuzzEnv&) {
                     std::vector<KeyPress> k;
                     parseKeyPresses(in, k);
                     std::vector<MouseKey> m;
                     parseMouseKeys(in, m);
                 }});
    t.push_back({"check", L"", {"flash,nan,flicker,alias,portability", "all", "nan"}, nullptr, [](const std::string& in, const FuzzEnv&) {
                     CheckOptions o;
                     parseCheckList(in, o);
                 }});
    t.push_back({"loudnorm", L"", {"{\n\t\"input_i\" : \"-23.5\",\n\t\"input_tp\" : \"-1.0\",\n\t\"input_lra\" : \"5.2\",\n\t\"input_thresh\" : \"-34.0\",\n\t\"target_offset\" : \"0.1\"\n}\n"}, nullptr,
                 [](const std::string& in, const FuzzEnv&) {
                     LoudnormMeasure m;
                     parseLoudnormMeasure(in, m);
                 }});
    t.push_back({"location", L"", {"C:\\a\\b.glsl:12: error: x", "  /home/u/s.glsl:3: oops", "0(12) : error C1008", "ERROR: 0:12: 'x'"}, nullptr, [](const std::string& in, const FuzzEnv&) {
                     std::wstring path;
                     int line = 0;
                     parseLocation(W(in), path, line);
                     glslErrorLine(W(in));
                     locateGlslErrors(W(in), L"f.glsl", 10);
                 }});
    t.push_back({"shadertoy", L".json", {"{\"Shader\":{\"info\":{\"id\":\"XsX3Rn\",\"name\":\"Test\",\"username\":\"someone\",\"description\":\"d\",\"tags\":[\"a\"]},\"renderpass\":[{\"inputs\":[{\"channel\":0,\"ctype\":\"texture\",\"src\":\"/media/a/x.jpg\"}],\"outputs\":[],\"code\":\"void mainImage(out vec4 c,in vec2 p){c=vec4(1.0);}\",\"name\":\"Image\",\"description\":\"\",\"type\":\"image\"}]}}"}, nullptr,
                 [](const std::string&, const FuzzEnv& env) {
                     ShadertoyImport r;
                     std::wstring err;
                     importShadertoyJson(env.path, env.dir + L"imp", r, err);
                 }});
    return t;
}

std::vector<FuzzTarget>& all() {
    static bool done = false;
    if (!done) {
        done = true;
        for (FuzzTarget& t : builtins()) registry().push_back(std::move(t));
    }
    return registry();
}

std::atomic<long long> g_iterStart{0};
long long nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

}  // namespace

void registerFuzzTarget(FuzzTarget t) { all().push_back(std::move(t)); }

std::vector<std::string> fuzzTargetNames() {
    std::vector<std::string> n;
    for (const FuzzTarget& t : all()) n.push_back(t.name);
    return n;
}

std::string fuzzMutate(const std::vector<std::string>& seeds, uint64_t& rng) {
    std::string s = seeds.empty() ? std::string() : seeds[below(rng, seeds.size())];
    const int ops = 1 + (int)below(rng, 4);
    for (int k = 0; k < ops; k++) {
        const int op = (int)below(rng, 10);
        const size_t n = s.size();
        switch (op) {
            case 0:
                if (n) s[below(rng, n)] ^= (char)(1 << below(rng, 8));
                break;
            case 1:
                if (n) s[below(rng, n)] = (char)next(rng);
                break;
            case 2: {
                std::string ins;
                for (size_t i = 0, m = 1 + below(rng, 8); i < m; i++) ins += (char)next(rng);
                s.insert(below(rng, n + 1), ins);
                break;
            }
            case 3:
                if (n) {
                    const size_t a = below(rng, n), len = 1 + below(rng, std::min<size_t>(64, n - a));
                    s.erase(a, len);
                }
                break;
            case 4:
                if (n) {
                    const size_t a = below(rng, n), len = 1 + below(rng, std::min<size_t>(64, n - a));
                    s.insert(below(rng, s.size() + 1), s.substr(a, len));
                }
                break;
            case 5:
                if (n) s.resize(below(rng, n));
                break;
            case 6:
                s.insert(below(rng, n + 1), kTokens[below(rng, sizeof(kTokens) / sizeof(kTokens[0]))]);
                break;
            case 7:
                s.insert(below(rng, n + 1), repeatStr(std::string(1, n ? s[below(rng, n)] : 'a'), 1 + (int)below(rng, 3000)));
                break;
            case 8:  // un champ numerique devient enorme ou negatif
                for (size_t i = 0; i < n; i++)
                    if (isdigit((unsigned char)s[i]) && below(rng, 12) == 0) {
                        s.insert(i, below(rng, 2) ? "-" : "99999999");
                        break;
                    }
                break;
            default:  // deux entrees collees l'une a l'autre
                if (!seeds.empty()) s.insert(below(rng, s.size() + 1), seeds[below(rng, seeds.size())]);
                break;
        }
        if (s.size() > (1u << 20)) s.resize(1u << 20);
    }
    return s;
}

int runFuzzCli(const std::wstring& exeDir, const std::string& target, int count, unsigned seed, const std::function<void(const std::string&, bool)>& write) {
    std::vector<FuzzTarget*> chosen;
    for (FuzzTarget& t : all())
        if (target == "all" || target == t.name) chosen.push_back(&t);
    if (chosen.empty()) {
        write("unknown fuzz target: " + target + "\n", true);
        return 1;
    }
    FuzzEnv env;
    env.dir = exeDir + L"fuzz_tmp";
    platform::makeDir(env.dir);
    env.dir += platform::pathSeparator();
    std::thread([]() {  // un lecteur qui boucle sans fin est un defaut : le processus s'arrete sur un code particulier
        for (;;) {
            platform::sleepMs(250);
            const long long st = g_iterStart.load();
            if (st && nowMs() - st > 8000) std::_Exit(97);
        }
    }).detach();
    long long total = 0;
    for (FuzzTarget* t : chosen) {
        std::vector<std::string> seeds = t->seeds;
        if (t->makeSeeds)
            for (std::string& s : t->makeSeeds(env)) seeds.push_back(std::move(s));
        {  // corpus du depot : tests/fuzz/<cible>/* (cas de depart et cas trouves)
            for (const wchar_t* rel : {L"tests/fuzz/", L"../tests/fuzz/"}) {
                const std::wstring dir = exeDir + rel + W(t->name);
                std::vector<platform::DirEntry> files;
                if (!platform::listDir(dir, files)) continue;
                for (const auto& f : files) {
                    std::string raw;
                    if (!f.isDir && readFile(dir + L"/" + f.name, raw)) seeds.push_back(raw);
                }
                break;
            }
        }
        if (seeds.empty()) seeds.push_back("");
        uint64_t rng = 0x9E3779B97F4A7C15ULL ^ ((uint64_t)seed * 0xD1B54A32D192ED03ULL) ^ std::hash<std::string>()(t->name);
        const std::wstring file = env.dir + L"fuzz_last_" + W(t->name) + (t->ext.empty() ? std::wstring(L".bin") : t->ext);
        for (int i = 0; i < count; i++) {
            const std::string input = i < (int)seeds.size() ? seeds[(size_t)i] : fuzzMutate(seeds, rng);
            writeBytes(file, input);  // apres un plantage, ce fichier est le cas fautif
            FuzzEnv e = env;
            if (!t->ext.empty()) e.path = file;
            g_iterStart = nowMs();
            try {
                t->run(input, e);
            } catch (const std::exception& ex) {
                write("exception in " + t->name + " (input saved in " + U8(file) + "): " + ex.what() + "\n", true);
                return 98;
            } catch (...) {
                write("exception in " + t->name + " (input saved in " + U8(file) + ")\n", true);
                return 98;
            }
            g_iterStart = 0;
            total++;
        }
        write(t->name + ": " + std::to_string(count) + " inputs ok\n", false);
    }
    write("fuzz: " + std::to_string(total) + " inputs, no crash\n", false);
    return 0;
}
