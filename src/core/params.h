// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Parametres animables d'un shader : `// uniform float vitesse = 1.0 [0..5]` declare un uniform reglable, ses valeurs sont enregistrees par shader
// dans le dossier de l'application, animees par images cles (`// key vitesse=0:1.0 ; 4:3.0`, `--param=`) ou liees au son
// (`// uniform float kick = audio.bass`).
#pragma once
#include "common.h"
#include "audio.h"
#include "expr.h"
#include "rhythm.h"
#include "gl.h"
#include "settings.h"

// Description d'un parametre pour les interfaces (panneau « Parametres »).
struct ParamInfo {
    std::string name;
    std::string type;       // float | int | bool | vec2 | vec3 | vec4 | color
    int n = 1;              // composantes (color : 3)
    bool hasRange = false;  // [min..max] ecrit dans la declaration
    double lo = 0, hi = 1;  // bornes du curseur (0..1 par defaut)
    std::string audio;      // "" ou level | bass | mid | treble : la valeur suit le son ; "expr" : une expression du rythme (expr)
    std::string expr;       // expression des variables du rythme (iBass * 0.5 + 0.2) ; "" = aucune
    double attack = 0, release = 0;  // lissage de l'expression : temps de montee et de descente (s), 0 = aucun
    double threshold = 0;   // seuil des variables du rythme (0 a 1) : en dessous la variable vaut 0
    bool animated = false;  // images cles dans le shader ou la ligne de commande : le curseur ne sert plus
    double def[4] = {0, 0, 0, 0};    // valeur de la declaration
    double value[4] = {0, 0, 0, 0};  // valeur de base : enregistree si elle existe, sinon la declaration
};

class ParamSet {
public:
    // Lit les declarations de `code` (image, passes et commun reunis), puis les valeurs enregistrees dans valuesFile, puis les
    // reglages `key nom=...` / `--param=` de `s.paramSpecs`. Faux + err si une declaration ou un reglage est invalide, ou si un nom est
    // deja pris. notes recoit les avertissements (nom inconnu...).
    bool load(const std::string& code, const Settings& s, const std::wstring& valuesFile, std::wstring& err, std::vector<std::wstring>* notes = nullptr);
    bool empty() const { return params_.empty(); }
    // Declarations GLSL a placer devant le code utilisateur (« uniform float vitesse;\n... »).
    const std::string& declarations() const { return decls_; }
    // Vrai s'il y a un parametre lie au son : setAudio doit alors etre appelee (fichier du reglage `audio`).
    bool needsAudio() const;
    // Vrai si le shader lit une variable du rythme (iBeat, iBass...) ou si un parametre est une expression du rythme : le fichier son est analyse
    bool needsRhythm() const;
    bool setAudio(const std::wstring& ffmpeg, const std::wstring& path, std::wstring& err);
    // Analyse du rythme demandee par autre chose (marqueurs, montage sur la musique) ; sans effet si deja faite
    bool loadRhythm(const std::wstring& ffmpeg, const std::wstring& path, std::wstring& err);
    // L'analyse du rythme, ou nullptr si elle n'a pas ete faite
    const Rhythm* rhythm() const { return rhythm_.loaded() ? &rhythm_ : nullptr; }
    // Renseigne les uniforms du programme (actif) pour le temps t (iTime).
    void apply(GLuint prog, double t) const;
    // Texte qui change des qu'un parametre, une cle ou une liaison change (signature du rendu par parties).
    std::string signature() const;
    std::vector<ParamInfo> infos() const;
    // Temps de toutes les images cles (curseur de temps de l'apercu), tries et sans doublon.
    std::vector<double> keyTimes() const;
    // Valeur au temps t, pour les tests et l'interface (out : n composantes).
    void valueAt(const std::string& name, double t, double out[4]) const;

private:
    struct Key {
        double t = 0;
        double v[4] = {0, 0, 0, 0};
    };
    enum class Interp { Linear, Smooth, Constant };
    struct Param {
        ParamInfo info;
        std::vector<Key> keys;
        Interp interp = Interp::Linear;
        Expr expr;                            // expression compilee (info.expr)
        mutable std::vector<float> series;    // expression lissee, echantillonnee a 100 Hz sur la duree du son (calculee a la demande)
        mutable bool seriesBuilt = false;
    };
    void evaluate(const Param& p, double t, double out[4]) const;
    double rawExpr(const Param& p, double t) const;
    double exprValue(const Param& p, double t) const;
    bool compileExpr(Param& p, std::wstring& err) const;
    std::vector<Param> params_;
    std::string decls_;
    AudioAnalyzer audio_;
    bool audioOk_ = false;
    Rhythm rhythm_;
    double bpm_ = 0;          // tempo impose (Settings::bpm)
    int beatsPerBar_ = 4;
    bool rhythmUsed_[EV_TIME] = {false, false, false, false, false, false, false, false};  // variables lues par le code du shader (declarees en uniform)
    bool hasExpr_ = false;
    bool progressUsed_ = false;       // le code lit iProgress (declare ici) : 0 a 1 pendant la transition, ou pendant tout le rendu sans transition
    double progStart_ = 0, progLen_ = 1;
    bool hasSpectrum_ = false;
};

// Bornes du curseur d'un parametre : [min..max] de la declaration, sinon de 0 (ou 2x la valeur si negative) a 2x la valeur par defaut, au moins 1.
void paramRange(const ParamInfo& p, double& lo, double& hi);

// Fichier des valeurs enregistrees d'un shader : <dossier de l'application>/params/<nom>-<empreinte du chemin>.ini
std::wstring paramsFile(const std::wstring& exeDir, const std::wstring& shaderPath);
// Enregistre (ou remplace) une valeur : « nom=valeur » (valeur au format des reglages : 2.5, 0.5,0.5, #ff8800, true).
bool saveParamValue(const std::wstring& file, const std::string& name, const std::string& value);
// Efface toutes les valeurs enregistrees (retour aux valeurs des declarations).
bool resetParamValues(const std::wstring& file);
// Texte d'une valeur, pour saveParamValue.
std::string formatParamValue(const ParamInfo& info, const double v[4]);
// Parametres declares par un shader (et ses passes) avec leurs valeurs de base pour les interfaces. Faux + err si une declaration est
// invalide. codeHash (facultatif) : empreinte du code lu, pour detecter qu'un fichier a change. keyTimes (facultatif) : temps de toutes
// les images cles (curseur de temps de l'apercu).
bool loadShaderParams(const std::wstring& exeDir, const std::wstring& shaderPath, std::vector<ParamInfo>& out, std::wstring& err,
                      std::string* codeHash = nullptr, std::vector<double>* keyTimes = nullptr);
// Code du shader et de ses passes (commun, tampons A-D, Cubemap A, post) reunis, sans autre traitement : sert a lire les declarations.
std::string collectShaderCode(const std::wstring& shaderPath);
