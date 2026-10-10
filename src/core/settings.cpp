#include "settings.h"
#include "util.h"

bool sameSettings(const Settings& a, const Settings& b) {
    return a.width == b.width && a.height == b.height && a.fps == b.fps && a.duration == b.duration &&
           a.start == b.start && a.crf == b.crf && a.preset == b.preset && a.codec == b.codec &&
           a.container == b.container && a.ss == b.ss && a.outdir == b.outdir &&
           a.bitrate == b.bitrate && a.color == b.color && a.blur == b.blur && a.shutter == b.shutter &&
           a.loop == b.loop && a.audio == b.audio && a.fadein == b.fadein && a.fadeout == b.fadeout &&
           a.encoder == b.encoder && a.pbo == b.pbo && a.chunk == b.chunk && a.gpu == b.gpu;
}

static void applyKV(Settings& s, const std::string& kIn, const std::string& vIn) {
    std::string k = lower(trim(kIn)), v = trim(vIn);
    if (k == "outdir") { s.outdir = W(v); return; }  // chemin complet (peut contenir des espaces)
    if (k == "audio") { s.audio = W(v); return; }
    // ne garder que le premier mot de la valeur (ignore un commentaire derriere)
    size_t sp = v.find_first_of(" \t");
    if (sp != std::string::npos) v = v.substr(0, sp);
    if (v.empty()) return;
    try {
        if (k == "width") s.width = std::stoi(v);
        else if (k == "height") s.height = std::stoi(v);
        else if (k == "fps") s.fps = std::stod(v);
        else if (k == "duration") s.duration = std::stod(v);
        else if (k == "start") s.start = std::stod(v);
        else if (k == "crf") s.crf = std::stoi(v);
        else if (k == "preset") s.preset = lower(v);
        else if (k == "ss" || k == "supersample") s.ss = std::stoi(v);
        else if (k == "bitrate") s.bitrate = std::stoi(v);
        else if (k == "blur") s.blur = std::stoi(v);
        else if (k == "shutter") s.shutter = std::stod(v);
        else if (k == "loop") s.loop = std::stod(v);
        else if (k == "fadein") s.fadein = std::stod(v);
        else if (k == "fadeout") s.fadeout = std::stod(v);
        else if (k == "pbo") s.pbo = std::stoi(v);
        else if (k == "chunk") s.chunk = std::stod(v);
        else if (k == "encoder") {
            std::string c = lower(v);
            s.encoder = (c == "auto" || c == "nvenc" || c == "amf" || c == "qsv") ? c : "cpu";
        } else if (k == "gpu") {
            std::string c = lower(v);
            s.gpu = (c == "high" || c == "low") ? c : "auto";
        }
        else if (k == "color") {
            std::string c = lower(v);
            s.color = (c == "bt709full" || c == "srgb" || c == "bt601") ? c : "bt709";
        }
        else if (k == "container") s.container = (v == "mov" || v == "mkv") ? lower(v) : "mp4";
        else if (k == "codec") {
            std::string c = lower(v);
            s.codec = (c == "h265" || c == "hevc" || c == "x265") ? "h265" : "h264";
        }
    } catch (...) {}
}

void parseLines(Settings& s, const std::string& text, bool onlySlashComments) {
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (onlySlashComments) {
            if (line.compare(0, 2, "//") != 0) continue;
            line = trim(line.substr(2));
        } else if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        applyKV(s, line.substr(0, eq), line.substr(eq + 1));
    }
}

void writeIni(const std::wstring& path, const Settings& s) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    fprintf(f,
            "; ShaderToMP4 - reglages (enregistres par la fenetre a chaque rendu)\r\n"
            "; Chaque valeur peut etre remplacee dans un shader, en commentaire, ex:  // duration=30\r\n"
            "width=%d\r\n"
            "height=%d\r\n"
            "fps=%g\r\n"
            "duration=%g      ; secondes (90 = une boucle complete de ce shader)\r\n"
            "start=%g          ; temps de depart (iTime) en secondes\r\n"
            "crf=%d           ; qualite : 0 = sans perte, 12 = quasi parfait, 18 = tres bon\r\n"
            "preset=%s      ; ultrafast ... medium ... slow ... veryslow (plus lent = fichier plus petit)\r\n"
            "codec=%s       ; h264 ou h265\r\n"
            "container=%s   ; mp4, mov ou mkv\r\n"
            "ss=%d           ; lissage (supersampling) : 1 = aucun, 2 = x2, 3 = x3, 4 = x4\r\n"
            "outdir=%s\r\n"
            "bitrate=%d      ; kb/s ; 0 = qualite constante (crf)\r\n"
            "color=%s    ; bt709, bt709full (pleine plage), srgb ou bt601\r\n"
            "blur=%d          ; flou de mouvement : sous-images par image (1 = aucun, 4, 8, 16)\r\n"
            "shutter=%g      ; obturateur du flou, en fraction d'une image (0.5 = 180 degres)\r\n"
            "loop=%g          ; periode du shader en secondes : la duree devient un multiple entier (0 = libre)\r\n"
            "audio=%s\r\n"
            "fadein=%g        ; fondu d'entree en secondes (image et son)\r\n"
            "fadeout=%g       ; fondu de sortie en secondes\r\n"
            "encoder=%s    ; cpu, auto, nvenc, amf ou qsv (encodage materiel si disponible)\r\n"
            "pbo=%d           ; profondeur de la file de lecture GPU (2 a 8)\r\n"
            "chunk=%g         ; rendu par parties de N secondes, reprenable apres interruption (0 = jamais)\r\n"
            "gpu=%s         ; auto, high ou low : GPU prefere de Windows (PC a deux cartes, relance auto)\r\n",
            s.width, s.height, s.fps, s.duration, s.start, s.crf, s.preset.c_str(), s.codec.c_str(), s.container.c_str(), s.ss,
            U8(s.outdir).c_str(), s.bitrate, s.color.c_str(), s.blur, s.shutter, s.loop, U8(s.audio).c_str(), s.fadein,
            s.fadeout, s.encoder.c_str(), s.pbo, s.chunk, s.gpu.c_str());
    fclose(f);
}
