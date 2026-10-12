// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Reglages de la video : config.ini (a cote du .exe), commentaires `// cle=valeur` des shaders, fenetre.
#pragma once
#include "common.h"
#include "keyboard.h"
#include "mouse.h"

struct Settings {
    int width = 1920, height = 1080;
    double fps = 60.0, duration = 90.0, start = 0.0;
    int crf = 12;
    std::string preset = "slow";
    std::string codec = "h264";  // h264 | h265 | vp9 | av1 | prores_* | dnxhr_* (voir formats.h)
    std::string container = "mp4";  // mp4 | mov | mkv | webm | gif | webp | png | tiff | exr
    int depth = 8;                  // bits par composante : 8 ou 10 (rendu GL en 16 bits)
    std::wstring name;              // modele du nom de sortie ("" = {shader}) : {shader} {w} {h} {fps} {date} {codec}
    std::string exists = "overwrite";  // fichier de sortie deja present : overwrite | number | skip
    int ss = 1;                     // supersampling 1..4
    std::wstring outdir;            // dossier de sortie ("" = a cote du shader)
    int bitrate = 0;                // kb/s ; 0 = qualite constante (CRF)
    std::string color = "bt709";    // bt709 | bt709full | srgb | bt601
    int blur = 1;                   // flou de mouvement : sous-images par image (1 = aucun)
    double shutter = 0.5;           // ouverture de l obturateur, en fraction de la duree d une image
    double loop = 0;                // periode du shader (s) : la duree devient un multiple entier ; 0 = libre
    std::string loopMode = "none";  // none | crossfade (fondu temporel vers le debut) | pingpong (le temps monte puis redescend) : boucle sans couture
    double loopFade = 1.0;          // duree du fondu de la boucle crossfade (s)
    std::string timemap;            // courbe de temps « 0:0;4:2;8:10 » (video:shader, en secondes) ; "" = temps reel
    bool audioFollow = false;       // le son suit la vitesse constante (`speed`) : atempo, hauteur conservee
    double speed = 1.0;             // vitesse du temps du shader (0.25 = ralenti x4) ; combinee a la courbe
    std::wstring audio;             // fichier son a mixer ("" = aucun)
    double fadein = 0, fadeout = 0; // fondus entree / sortie (s), image et son
    std::string encoder = "cpu";    // cpu | auto | nvenc | amf | qsv (encodage materiel)
    int pbo = 3;                    // profondeur de la file de lecture GPU (2..8)
    std::vector<std::string> variants;  // variantes du meme shader : "nom:cle=valeur|cle=valeur" (rendues apres le rendu de base)
    std::string endaction = "none"; // a la fin d'un lot reussi : none | folder | sleep (config.ini seulement)
    std::string gpu = "default";    // default | high | low : carte graphique sur un PC a deux GPU (Linux ; config.ini ou --gpu=)
    bool metadata = true;           // titre / commentaire ecrits dans le conteneur
    std::wstring channel[4];        // iChannel0-3 : chemin d'image, "noise" ou "grid" ("" = aucun)
    std::string chFilter[4];        // nearest | linear | mipmap ("" : mipmap pour une image, linear pour un tampon)
    std::string chWrap[4];          // clamp | repeat ("" : repeat pour une image, clamp pour un tampon)
    bool chVflip[4] = {true, true, true, true};                          // comme Shadertoy : haut de l'image en v = 1
    std::vector<MouseKey> mouseKeys;  // iMouse simule : images cles (vide = 0) ; positions en fractions de la taille (0,0 = bas gauche)
    double bufScale[4] = {0, 0, 0, 0};  // taille du tampon A-D : fraction de la taille du rendu (0 = taille du rendu)
    int bufW[4] = {0, 0, 0, 0}, bufH[4] = {0, 0, 0, 0};  // ou taille fixe en pixels (0 = taille du rendu)
    std::vector<std::string> paramSpecs;  // reglages de parametres « nom=valeur » ou « nom=0:1.0 ; 4:3.0 » (// key nom=..., --param=), dans l'ordre
    std::vector<KeyPress> keys;   // canal `keyboard` : touches pressees a des instants donnes
    std::string date;              // iDate fixe "AAAA-MM-JJ[THH:MM[:SS]]" ("" = date reelle)
    std::string vr = "auto";        // auto | off | mono | stereo : shader mainVR rendu en 360 equirectangulaire (stereo = haut / bas)
    double vrIpd = 0.064;           // ecart des yeux du rendu stereo, dans l'unite du shader
    bool alpha = false;             // transparence : fragColor.a est ecrit dans la sortie (ProRes 4444, VP9 WebM, PNG / TIFF / EXR, WebP)
    std::string hdr = "off";        // off | pq (HDR10 : BT.2020, PQ) | hlg : sortie HDR 10 bits (H.265, AV1, VP9)
    int hdrPeak = 1000;             // luminance maximale declaree (MaxCLL et volume d'affichage), en cd/m2
    int hdrFall = 400;              // luminance moyenne maximale declaree (MaxFALL)
    int hdrWhite = 203;             // cd/m2 d'un blanc de shader a 1.0 (blanc de reference de la BT.2408)
    double interp = 0;              // interpolation d'images : cadence de sortie (0 = aucune), le rendu se fait a `fps`
    std::string scale;              // mise a l'echelle de sortie : "" | "1920x1080" | "2" ou "2x" (facteur)
    std::string scaleFilter = "lanczos";  // lanczos | bicubic | bilinear | neighbor | area
    double loudnorm = 0;            // normalisation du son en LUFS (0 = aucune), ex. -16
    std::wstring title, credit;     // texte incruste au debut de la video (titre au centre, credit en bas a gauche)
    double titleDur = 3;            // duree de l'incrustation, en secondes
    std::string post;               // effets de post-traitement : « bloom:0.4,grain,vignette » (noms et quantites, voir postfx.h)
    std::wstring lut;               // LUT couleur .cube appliquee en dernier ("" = aucune)
    int warmup = 0;               // images simulees avant la premiere (tampons chauffes, iFrame decale de N) ; 0 = aucune
    double chunk = 30;             // rendu par parties de N secondes, reprenables (0 = jamais)
    std::string report = "off";    // rapport de rendu a cote de la video : off | json | text
    bool precheck = false;           // controle qualite rapide (flashs, valeurs invalides) avant chaque rendu video : avertit, ne bloque jamais
    std::string stillFormat = "png";  // image fixe : png | tiff (16 bits par composante avec depth=10)
    int tile = 0;                  // image fixe : cote des tuiles en pixels (0 = automatique : tuiles des que l'image depasse 8192 pixels ou la limite du GPU)
    int dpi = 0;                   // image fixe PNG : resolution ecrite dans le fichier (points par pouce), 0 = rien
    int samples = 1;               // image fixe (PNG) : moyenne de N echantillons independants du meme instant (iFrame different), 1..100000
};

bool sameSettings(const Settings& a, const Settings& b);
void parseLines(Settings& s, const std::string& text, bool onlySlashComments);  // ini (false) ou commentaires // (true)
void writeIni(const std::wstring& path, const Settings& s);
// Reglage nomme (ligne de commande : --width=1280) : isSettingKey dit si la cle (minuscules) existe, applySetting l'applique.
bool isSettingKey(const std::string& key);
// Reglages ecrits en commentaires `// cle=valeur` dans un shader (ils priment sur la fenetre), sous la forme « fps=30, width=3840 ».
// "" si le shader n'en a pas ou ne peut pas etre lu.
std::wstring shaderOverridesOf(const std::wstring& shaderPath);
// Reglages imposes par les commentaires `// cle=valeur` du shader (cles en minuscules, valeur telle que lue par applySetting) : les fenetres
// affichent ces valeurs, champs grises, a la place de celles de la fenetre.
struct Override {
    std::string key, value;
    bool operator==(const Override& o) const { return key == o.key && value == o.value; }
};
std::vector<Override> shaderOverrides(const std::wstring& shaderPath);
bool isOverridden(const std::vector<Override>& list, const char* key);
void applyOverrides(Settings& s, const std::vector<Override>& list);
void applySetting(Settings& s, const std::string& key, const std::string& value);
