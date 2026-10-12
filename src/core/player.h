// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Apercu en temps reel : lecture, pause, position, rechargement automatique du shader quand il change sur le disque.
// Un thread dedie garde son contexte OpenGL, le programme et les tampons ; il envoie des images BGRA reduites.
#pragma once
#include "common.h"
#include "settings.h"

// Cadence reelle de l'apercu : images recues par seconde, mesuree sur des fenetres d'au moins une demi-seconde.
class FpsMeter {
public:
    void reset() { count_ = 0; fps_ = 0; start_ = Clock::now(); }
    void tick() {
        const auto now = Clock::now();
        if (count_ == 0) start_ = now;
        count_++;
        const double dt = std::chrono::duration<double>(now - start_).count();
        if (dt >= 0.5) {
            fps_ = (count_ - 1) / dt;
            count_ = 1;
            start_ = now;
        }
    }
    double fps() const { return fps_; }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_ = Clock::now();
    int count_ = 0;
    double fps_ = 0;
};

class PreviewPlayer {
public:
    PreviewPlayer();
    ~PreviewPlayer();  // arrete le thread

    // Une image : largeur, hauteur, pixels BGRA (haut en bas), temps du shader en secondes. Appelee depuis le thread du lecteur.
    using FrameFn = std::function<void(int w, int h, std::vector<uint8_t>&& bgra, double t)>;

    // Demarre la lecture du shader a `startTime` (secondes, dans [start, start + duree]). ready() est interrogee avant de
    // lire une image sur le GPU : elle renvoie false quand l'interface n'a pas fini d'afficher la precedente (l'image est
    // alors sautee). log recoit les erreurs de compilation et les rechargements.
    void start(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, double startTime, FrameFn onFrame,
               std::function<bool()> ready, std::function<void(const std::wstring&)> log);
    void stop();
    bool running() const;

    void setPlaying(bool playing);  // pause / reprise
    bool playing() const;
    void seek(double t);            // saute a ce temps (une image est rendue meme en pause)
    double time() const;            // temps courant du shader

    struct Impl;  // detail interne (thread, etat partage)

private:
    std::unique_ptr<Impl> impl_;
};
