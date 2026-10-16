// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Son et rythme : analyse hors ligne d'un fichier son (decode par ffmpeg, calcul propre, aucune bibliotheque en plus) -> tempo, temps forts, mesures,
// attaques par bande, energie lissee, sections. Les caracteristiques sont gardees dans une fiche <fichier son>.beats.json a cote de la musique ; la
// grille (temps, mesures, sections) en est derivee a chaque lecture, avec les corrections manuelles de l'utilisateur. Toutes les valeurs sont des
// fonctions du temps : deux rendus donnent la meme video.
#pragma once
#include "common.h"

constexpr int kRhythmRate = 44100;  // frequence de l'analyse (Hz)
constexpr int kRhythmHop = 512;     // echantillons entre deux colonnes de caracteristiques (11,6 ms)
constexpr double kRhythmFrameRate = (double)kRhythmRate / kRhythmHop;

// Une colonne par pas de kRhythmHop echantillons, valeurs de 0 a 255 (normalisees sur la partie forte du morceau)
struct RhythmFeatures {
    double duration = 0;  // secondes
    std::vector<uint8_t> peak;                           // crete de l'onde (affichage)
    std::vector<uint8_t> energy, bass, mid, treble;      // niveau global et des trois bandes (graves, mediums, aigus)
    std::vector<uint8_t> fluxBass, fluxMid, fluxTreble;  // attaques : augmentation du spectre, par bande
    size_t frames() const { return energy.size(); }
};

// Calcule les caracteristiques de `pcm` (mono, 44100 Hz, 16 bits)
void analyzeRhythmSamples(const std::vector<int16_t>& pcm, RhythmFeatures& out);

// Corrections de l'utilisateur sur la grille detectee : temps ajoutes, temps supprimes (instants de la grille detectee)
struct RhythmEdits {
    std::vector<double> added, removed;
    bool empty() const { return added.empty() && removed.empty(); }
};

// Ce que le shader peut lire au temps t
struct RhythmValues {
    float beat = 0;      // phase 0 a 1 entre deux temps
    float bpm = 0;       // tempo local
    float bass = 0, mid = 0, treble = 0, energy = 0;  // 0 a 1
    float onset = 0;     // impulsion a chaque attaque (1 puis decroissance rapide)
    int beatCount = 0;   // temps ecoules depuis le debut du fichier
};

struct RhythmGrid {
    double bpm = 0;                 // tempo moyen ; 0 = aucun tempo trouve
    std::vector<double> beats;      // temps forts, de 0 a la fin du fichier
    std::vector<double> bars;       // premiers temps de mesure
    std::vector<double> sections;   // debuts de section (le premier est 0)
    std::vector<double> onsets;     // attaques
    std::vector<float> onsetStrength;
    std::vector<float> bass, mid, treble, energy;  // courbes lisses, une valeur par colonne
};

// Derive la grille des caracteristiques. forcedBpm > 0 impose le tempo (la phase reste choisie d'apres les attaques) ; beatsPerBar : temps par mesure.
void deriveRhythmGrid(const RhythmFeatures& f, double forcedBpm, int beatsPerBar, const RhythmEdits& edits, RhythmGrid& out);

class Rhythm {
public:
    // Relit la fiche <audio>.beats.json si elle correspond au fichier, sinon decode, analyse et l'ecrit. Faux + err si le son est illisible.
    // forcedBpm > 0 : tempo impose ; beatsPerBar : temps par mesure (2 a 12).
    bool load(const std::wstring& ffmpeg, const std::wstring& audioPath, double forcedBpm, int beatsPerBar, std::wstring& err);
    bool loaded() const { return loaded_; }
    bool fromCache() const { return fromCache_; }
    const std::wstring& cacheFile() const { return file_; }

    RhythmValues at(double t) const;
    double duration() const { return feat_.duration; }
    const RhythmGrid& grid() const { return grid_; }
    const RhythmFeatures& features() const { return feat_; }
    const RhythmEdits& edits() const { return edits_; }
    double forcedBpm() const { return forcedBpm_; }
    int beatsPerBar() const { return beatsPerBar_; }

    // Texte qui change des que la grille change (signature du rendu par parties)
    std::string signature() const;

    // Corrections manuelles : un clic pres d'un temps le supprime, ailleurs il en ajoute un (aimante sur l'attaque la plus proche). Les corrections
    // sont ecrites dans la fiche. Rend vrai si la grille a change.
    bool toggleBeat(double t, double tolerance);
    bool clearEdits();
    // Recalcule la grille avec un autre tempo impose (0 = automatique) et un autre nombre de temps par mesure, sans relire le son
    void retune(double forcedBpm, int beatsPerBar);
    // Crete de l'onde pour l'affichage : `buckets` valeurs de 0 a 1 entre from et to (secondes)
    void waveform(double from, double to, int buckets, std::vector<float>& peaks) const;

private:
    bool save();
    RhythmFeatures feat_;
    RhythmEdits edits_;
    RhythmGrid grid_;
    std::wstring file_;
    std::string key_;
    double forcedBpm_ = 0;
    int beatsPerBar_ = 4;
    bool loaded_ = false, fromCache_ = false;
};

// Plus proche point de `grid` (trie) de t ; -1 si la grille est vide
double nearestOf(const std::vector<double>& grid, double t);

// --- Marqueurs ---------------------------------------------------------------------------------------------------------------------------------
struct RhythmMarker {
    double time = 0;     // secondes de la video
    std::string kind;    // "beat" | "bar" | "section"
    std::wstring label;
};
// Marqueurs de la video qui montre le son de `audioStart` a `audioStart + videoSeconds * speed` (vitesse constante positive)
std::vector<RhythmMarker> rhythmMarkers(const Rhythm& r, double audioStart, double videoSeconds, double speed, bool beats, bool sections);
// Fichier de metadonnees ffmpeg (-f ffmetadata) dont les chapitres sont les sections ; vide s'il n'y en a pas
std::string chaptersMetadata(const std::vector<RhythmMarker>& markers, double videoSeconds);
// Ecrit <video sans extension>.markers.txt (etiquettes : debut<tab>fin<tab>nom, lues par les editeurs audio) et .markers.csv (type, numero, secondes,
// code temporel a `fps` images par seconde, nom)
bool writeMarkerFiles(const std::wstring& video, const std::vector<RhythmMarker>& markers, double fps, double videoSeconds);
// Ajoute les chapitres a un fichier video fini (copie des flux, aucun nouvel encodage). Faux et message si ffmpeg echoue.
bool addChapters(const std::wstring& ffmpeg, const std::wstring& video, const std::vector<RhythmMarker>& markers, double videoSeconds, std::wstring& err);

// Compte rendu d'une analyse, ligne par ligne, pour --beats et les fenetres
std::vector<std::wstring> rhythmSummary(const Rhythm& r);

// Ecrit les marqueurs demandes par le reglage `markers` (chapters | file | all) pour `video`, qui montre le son a partir de `audioStart` ; `log` recoit
// le compte rendu (une ligne par fichier ecrit, ou la raison de l'echec). Jamais bloquant.
void applyMarkerSetting(const std::string& mode, const Rhythm& r, const std::wstring& ffmpeg, const std::wstring& video, double audioStart, double seconds,
                        double speed, double fps, const std::function<void(const std::wstring&)>& log);
