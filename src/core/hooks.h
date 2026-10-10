// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Rappels du moteur de rendu vers l'appelant (interface graphique ou auto-test).
#pragma once
#include "common.h"


struct Hooks {
    std::function<void(const std::wstring&)> log;
    std::function<void(long long n, long long total, double speed, double remaining)> progress;
    const std::atomic<bool>* cancel = nullptr;
    const std::atomic<bool>* pause = nullptr;
    bool snapshot = false;  // une seule image PNG (au temps "start") au lieu de la video
    // apercu : une image BGRA (haut en bas) au temps previewTime, renvoyee a l'appelant, sans ffmpeg
    std::function<void(int w, int h, std::vector<uint8_t>&& bgra)> previewFrame;
    double previewTime = 0;
    bool cancelled() const { return cancel && cancel->load(); }
    bool paused() const { return pause && pause->load(); }
};

enum class Result { Ok, Failed, Cancelled };
