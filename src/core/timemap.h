// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Courbe de temps : à quel instant du shader correspond chaque instant de la vidéo. Sert au ralenti, à l'accélération, à l'arrêt sur
// image, à la marche arrière (`timemap=`), à la vitesse constante (`speed=`) et à l'aller-retour (`loopmode=pingpong`).
// Le shader est une fonction du temps : en ralenti, chaque image est calculée à l'instant exact voulu (jamais d'interpolation).
#pragma once
#include "common.h"

// Echantillons de la courbe (decalage du temps du shader, en secondes, pour n instants regulierement repartis sur [0, duration]) tels que les
// interfaces les dessinent : courbe `timemap`, vitesse `speed`, aller-retour. Faux si le texte de la courbe est invalide.
bool timeCurveSamples(const std::string& timemap, double speed, bool pingpong, double duration, int n, std::vector<double>& out);

class TimeMap {
public:
    // « 0:0;4:2;8:10 » : points « instant de la vidéo : instant du shader » en secondes (le shader compté depuis `start`), séparés par « ; » ou « , ».
    // Entre deux points la courbe est lisse (cubique monotone par morceaux) : pas de saut de vitesse. Avant le premier point la courbe part de
    // 0:0 ; après le dernier elle continue à la vitesse atteinte. "" = identité. Faux et message dans `err` si le texte est invalide.
    static bool parse(const std::string& spec, TimeMap& out, std::wstring& err);
    // Aller-retour : le temps monte de 0 à `half` puis redescend à 0 (période 2 * half), pour une boucle sans couture.
    static TimeMap pingPong(double half);

    void setSpeed(double speed) { speed_ = speed; }  // facteur appliqué à toute la courbe (1 = rien, 0.25 = quatre fois plus lent)
    double speed() const { return speed_; }

    double at(double u) const;        // décalage du temps du shader (s) à l'instant u de la vidéo
    double speedAt(double u) const;   // vitesse (dérivée, positive ou négative) à l'instant u
    bool identity() const { return !pingpong_ && pts_.empty() && speed_ == 1.0; }
    bool pingpong() const { return pingpong_; }
    bool monotone() const;            // jamais décroissante : les passes à historique l'acceptent
    bool hasCurve() const { return !pts_.empty(); }
    // Vitesses extrêmes sur [0, duration] (échantillonnées), pour le journal et l'estimation
    void speedRange(double duration, double& lo, double& hi) const;
    std::string canonical() const;    // texte stable, entre dans la signature des parties

private:
    struct Pt {
        double x = 0, y = 0, m = 0;   // instant de la vidéo, instant du shader, pente
    };
    std::vector<Pt> pts_;
    bool pingpong_ = false;
    double half_ = 0, speed_ = 1.0;

    double base(double u) const;
    double baseSlope(double u) const;
};
