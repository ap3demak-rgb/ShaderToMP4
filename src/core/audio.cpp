// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "audio.h"
#include "i18n.h"
#include "util.h"
#include "platform/platform.h"
#include <cmath>
#include <complex>

namespace {

constexpr int kFft = 1024;  // fenetre de l'analyse : 512 raies de spectre

void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {  // permutation par inversion de bits
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * 3.14159265f / (float)len;
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; k++) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

}  // namespace

bool AudioAnalyzer::load(const std::wstring& ffmpeg, const std::wstring& path, std::wstring& err) {
    if (!fileExists(path)) {
        err = TXF("error.audio_missing", path.c_str());
        return false;
    }
    auto proc = platform::Process::start(
        platform::splitCommand(L"\"" + ffmpeg + L"\" -hide_banner -loglevel quiet -nostdin -i \"" + path +
                               L"\" -vn -ac 1 -ar 44100 -f s16le -"),
        false, true);
    if (!proc) {
        err = TX("error.ffmpeg_launch");
        return false;
    }
    std::vector<uint8_t> raw;
    char buf[65536];
    size_t got;
    const size_t cap = (size_t)44100 * 60 * 20 * 2;
    while ((got = proc->readOutput(buf, sizeof(buf))) > 0 && raw.size() < cap) raw.insert(raw.end(), (uint8_t*)buf, (uint8_t*)buf + got);
    proc->wait();
    if (raw.size() < 2) {
        err = TXF("error.channel_read", path.c_str());
        return false;
    }
    pcm_.resize(raw.size() / 2);
    memcpy(pcm_.data(), raw.data(), pcm_.size() * 2);
    return true;
}

void AudioAnalyzer::rows(double t, uint8_t out[1024]) const {
    const long long end = (long long)std::floor(t * 44100.0);
    std::vector<std::complex<float>> x(kFft);
    for (int i = 0; i < kFft; i++) {
        const float w = 0.42f - 0.5f * std::cos(2.0f * 3.14159265f * i / (kFft - 1)) + 0.08f * std::cos(4.0f * 3.14159265f * i / (kFft - 1));  // Blackman
        x[i] = sample(end - kFft + i) / 32768.0f * w;
    }
    fft(x);
    for (int k = 0; k < 512; k++) {
        const float mag = std::abs(x[k]) / (kFft * 0.42f);  // normalise par la fenetre
        const float db = 20.0f * std::log10(std::max(mag, 1e-12f));
        out[k] = (uint8_t)std::min(255.0f, std::max(0.0f, (db + 100.0f) / 70.0f * 255.0f));  // -100 dB .. -30 dB, comme un analyseur web
        out[512 + k] = (uint8_t)std::min(255.0f, std::max(0.0f, (sample(end - 512 + k) / 32768.0f * 0.5f + 0.5f) * 255.0f));
    }
}

AudioAnalyzer::Levels AudioAnalyzer::levels(double t) const {
    uint8_t row[1024];
    rows(t, row);
    Levels l;
    const long long end = (long long)std::floor(t * 44100.0);
    double sum = 0;
    for (int i = 0; i < kFft; i++) {
        const double v = sample(end - kFft + i) / 32768.0;
        sum += v * v;
    }
    l.level = (float)std::min(1.0, std::sqrt(sum / kFft) * 1.41421356);
    // une raie couvre 22050 / 512 = 43 Hz
    auto peak = [&](int lo, int hi) {
        int m = 0;
        for (int k = lo; k < hi; k++) m = std::max(m, (int)row[k]);
        return m / 255.0f;
    };
    l.bass = peak(0, 6);      // 0 a 250 Hz
    l.mid = peak(6, 47);      // 250 Hz a 2 kHz
    l.treble = peak(47, 233);  // 2 kHz a 10 kHz
    return l;
}
