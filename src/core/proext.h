// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Points d'accroche de l'edition Pro : le moteur public ne connait que ces interfaces ; les fonctions Pro (scenes 3D, passes de geometrie...) vivent dans le
// depot prive, sont compilees avec ce depot et s'enregistrent au demarrage par registerProExtensions(). L'edition libre n'enregistre rien : aucun de ces
// points d'accroche n'est alors appele.
#pragma once
#include "common.h"
#include "gl.h"
#include "hooks.h"
#include "settings.h"

// Demande de scene 3D pour un rendu : le shader image, ses reglages, la taille du rendu.
struct SceneRequest {
    std::wstring shaderPath;  // shader image (fragments)
    std::wstring exeDir;
    const Settings* settings = nullptr;
    const std::string* code = nullptr;  // code du shader image apres adaptation (commentaires // compris)
    int width = 0, height = 0;          // taille du rendu (iResolution du programme principal)
    bool preview = false;               // image d'apercu ou lecteur temps reel
    const Hooks* hooks = nullptr;       // journal
};

// Scene 3D : maillage dessine par un etage sommets a la place du triangle plein ecran, devant le meme fragment shader que le mode image.
// Chaque methode est appelee dans le thread de rendu, contexte OpenGL courant.
class ProScene {
public:
    virtual ~ProScene() {}
    virtual std::string version() const { return "330"; }  // « 330 » ou « 430 » (passes de calcul)
    virtual std::string vertexSource() = 0;                // etage sommets complet (#version comprise) du programme principal
    virtual std::wstring vertexPath() const = 0;           // fichier de l'etage sommets (« » = etage par defaut), pour localiser les erreurs
    virtual int vertexLines() const = 0;                   // nombre de lignes de ce fichier
    // Apres la compilation du programme principal : tampons du maillage, cibles, passes. w x h : taille du rendu.
    virtual bool prepare(GLuint prog, int w, int h, std::wstring& err) = 0;
    // Dessine l'image (programme principal actif, uniform de temps deja regles) au lieu du triangle plein ecran : la cible liee, la fenetre, les
    // parametres de melange (accumulation) sont ceux de l'appelant et lui sont rendus intacts.
    struct Frame {
        double t = 0;           // temps du shader
        long long frame = 0;    // iFrame
        double dt = 0;          // iTimeDelta
        float weight = 1;       // poids de fondu et mode d'alpha que l'appelant a regles dans le programme (st_weight, st_premul)
        bool premul = false;
    };
    virtual void draw(const Frame& f) = 0;
    // Texte du fragment : declarations ajoutees avant le code du shader image (matrices de la scene, carte d'ombre, reflexion)
    virtual std::string fragmentPrelude() const { return ""; }
    // Tout ce qui change le resultat (maillage, camera, passes) : entre dans la signature des parties reprenables.
    virtual std::string signature() const = 0;
    // Apercu interactif : nouveaux reglages (camera, objet...) sans recompiler ; faux si le changement demande une reconstruction complete.
    virtual bool reconfigure(const Settings& s) { (void)s; return false; }
};

// Signaux exterieurs (edition Pro : MIDI, series de donnees) : des valeurs lues au temps exact de chaque image, jamais en temps reel, donc les memes a chaque
// rendu. Le moteur declare dans le shader les variables que le code lit, les regle a chaque image, et laisse les expressions des parametres
// (`// uniform float k = midi.cc1 * 2`) lire les signaux par leur nom.
class ProSignals {
public:
    virtual ~ProSignals() {}
    struct Var {
        std::string name;      // variable GLSL lisible sans declaration (iNote...)
        bool isInt = false;
    };
    virtual std::vector<Var> shaderVars() const = 0;
    virtual double shaderValue(size_t index, double t) const = 0;  // valeur de shaderVars()[index] au temps t du shader
    virtual bool mentions(const std::string& text) const = 0;      // le texte d'une expression lit-il un signal ?
    // Identifiant d'une expression : `midi.cc1` (argc = -1), `midi.note(60)` (argc = nombre d'arguments). -1 si inconnu.
    virtual int resolve(const std::string& name, int argc) const = 0;
    virtual double value(int handle, const double* args, int argc, double t) const = 0;
    virtual double duration() const = 0;      // etendue des donnees en secondes (lissage des expressions)
    virtual std::string signature() const = 0;  // tout ce qui change les valeurs : entre dans la signature des parties reprenables
};

// Image d'un canal d'entree fournie par une extension : « data:serie » (texture de lecture d'une serie de donnees) ; rgba : w x h x 4 flottants, premiere ligne en haut.
struct ProChannelImage {
    int w = 0, h = 0;
    std::vector<float> rgba;
};

// Outils d'edition de l'interface Windows (edition Pro) : annuler / retablir, editeurs de courbes, de temps et de montage. Le programme public ne fait
// qu'appeler ces fonctions quand elles existent (boutons et raccourcis absents de l'edition libre) ; la verification de la licence est faite par elles.
struct ProTools {
    // Un champ des reglages a change (le GUI regroupe les changements : appel 400 ms apres le dernier) : memoriser l'etat pour annuler.
    std::function<void()> settingsChanged;
    // Ctrl+Z (redo faux) ou Ctrl+Y / Ctrl+Maj+Z (redo vrai) dans la fenetre principale ; vrai si traite.
    std::function<bool(bool redo)> undoRedo;
    // Fenetre des parametres : editeur de la courbe d'images cles d'un parametre du shader (ecrit sa ligne `// key nom=...`) ; vrai si le shader a change.
    std::function<bool(HWND parent, const std::wstring& shaderPath, const std::string& param)> editCurve;
    // Fenetre Sortie : editeur de la courbe de temps ; current = texte actuel (`0:0;4:2`), duration = duree de la video ; vrai + result si applique.
    std::function<bool(HWND parent, const std::string& current, double duration, std::string& result)> editTimeMap;
    // Fichier .montage : editeur graphique ; vrai si traite (faux : l'editeur de texte est utilise).
    std::function<bool(HWND parent, const std::wstring& montagePath)> editMontage;
};

struct ProExt {
    ProTools tools;
    // Signaux du rendu (reglages `midi=`, `data=`...) : nullptr sans erreur s'il n'y en a pas, nullptr avec err si une source est invalide. shaderPath : chemins relatifs au shader.
    std::function<std::shared_ptr<const ProSignals>(const Settings&, const std::wstring& shaderPath, std::wstring& err)> makeSignals;
    // Reglages calcules par l'extension avant le rendu, l'apercu et la lecture (ex. une chaine de LUT cuite en un seul fichier, le son d'un canal video) :
    // modifie s ; faux + err si un reglage de l'extension est invalide.
    std::function<bool(Settings& s, const std::wstring& shaderPath, const std::wstring& exeDir, std::wstring& err)> prepareSettings;
    // Etage de couleur du programme image : code GLSL qui definit `vec3 st_color(vec3 c)`, applique a la couleur de l'image avant le fondu et l'ecriture
    // (courbe de tons, exposition). "" = aucun : le programme reste identique a celui de l'edition libre.
    std::function<std::string(const Settings&)> colorStage;
    // Sortie finie (image fixe ou sequence d'images : le premier fichier) : l'extension peut la completer (profil ICC). Appelee avant que le programme
    // ajoute lui-meme son bloc sRGB a un PNG.
    std::function<void(const Settings& s, const std::wstring& file, bool still, bool sequence)> finishOutput;
    // Sortie GIF : fin de la chaine de filtres ffmpeg (conversion, palette, tramage) d'une extension ; "" = celle du programme.
    std::function<std::wstring(const Settings&)> gifFilter;
    // Image fixe a echantillons (`samples=N`) : l'extension veut-elle debruiter ? Si oui le moteur accumule deux moities independantes des tirages (pairs et impairs)
    // et lui rend leurs moyennes pour estimer le bruit.
    std::function<bool(const Settings&)> denoiseWanted;
    // Debruitage : halfA et halfB sont les moyennes (RGBA flottant, w x h, premiere ligne en bas) des nA et nB tirages de chaque moitie ; out recoit l'image debruitee
    // (moyenne de tous les tirages, meme disposition). Appelee dans le thread de rendu.
    std::function<void(const Settings&, int w, int h, int nA, int nB, const std::vector<float>& halfA, const std::vector<float>& halfB, std::vector<float>& out)> denoise;
    // Canal d'entree d'une extension : vrai si `spec` (« data:serie ») est a elle ; faux + err si la source est invalide. Rendu en virgule flottante, lu en 1 x N ou W x H.
    std::function<bool(const Settings&, const std::wstring& spec, const std::wstring& shaderDir, ProChannelImage& out, std::wstring& err, bool& handled)> channelImage;
    // Canal image decode (fichier) : l'extension peut le convertir (espace de couleur) ; vrai si elle a change `rgba8` (w x h x 4 octets) en `out` (flottants, premiere ligne en haut).
    std::function<bool(const Settings&, int channel, int w, int h, const uint8_t* rgba8, ProChannelImage& out)> decodeChannel;
    // Canal image flottant (hdr, exr) : conversion en place d'un espace de couleur vers le lineaire ; rien a faire si l'extension ne le demande pas.
    std::function<void(const Settings&, int channel, int w, int h, float* rgba)> decodeChannelFloat;
    // Cree la scene d'un rendu. Renvoie nullptr sans erreur si le shader n'en demande pas ; nullptr avec err renseigne si la demande est invalide.
    std::function<std::unique_ptr<ProScene>(const SceneRequest&, std::wstring& err)> makeScene;
    // Apercu interactif : les reglages (shader compris) demandent-ils une scene 3D ?
    std::function<bool(const Settings&)> sceneRequested;
    // Geste de souris sur l'apercu : dx, dy en degres d'orbite autour de la cible, zoom = facteur sur la distance (1 = inchange), t = temps de l'apercu.
    // Renvoie les reglages (cle -> valeur, « off » efface) a appliquer pour que la camera fixe corresponde au point de vue obtenu.
    std::function<std::vector<std::pair<std::string, std::string>>(const Settings&, double t, double dx, double dy, double zoom)> orbitCamera;
    // Cas d'auto-test de l'edition Pro (contexte OpenGL courant) : ajoute au rapport, compte les cas et les echecs.
    std::function<void(const std::wstring& exeDir, const std::wstring& testsDir, std::wstring& report, int& total, int& fails)> selfTest;
};

// Cree la scene demandee par les reglages (nullptr sans erreur s'il n'y en a pas) et remplit le programme de scene (etage sommets, version, declarations) ;
// nullptr avec err renseigne si la demande est invalide. Utilise par le rendu et par le lecteur temps reel.
std::unique_ptr<ProScene> createProScene(const SceneRequest& rq, SceneProgram& program, std::wstring& err);

void registerProExt(const ProExt& ext);
const ProExt* proExt();  // nullptr dans l'edition libre

// Appelee au demarrage par les points d'entree : enregistre les extensions si l'edition Pro les fournit (registerProExtensions), sinon ne fait rien.
void initProExtensions();
