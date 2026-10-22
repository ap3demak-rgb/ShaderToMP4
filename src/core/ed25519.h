// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Signature Ed25519 (RFC 8032) et SHA-512, ecrites pour le programme (adaptation de TweetNaCl, domaine public) : le programme ne fait que verifier
// les fichiers de licence ; la signature n'est utilisee que par le generateur de licences (hors depot public) et par l'auto-test.
#pragma once
#include <cstddef>
#include <cstdint>

namespace ed25519 {

void sha512(const uint8_t* data, size_t len, uint8_t out[64]);
// Cle publique (32 octets) et cle secrete (64 octets = graine 32 octets + cle publique) deduites d'une graine de 32 octets.
void keypairFromSeed(const uint8_t seed[32], uint8_t pub[32], uint8_t sec[64]);
void sign(const uint8_t* msg, size_t len, const uint8_t sec[64], uint8_t sig[64]);
bool verify(const uint8_t* msg, size_t len, const uint8_t sig[64], const uint8_t pub[32]);

}  // namespace ed25519
