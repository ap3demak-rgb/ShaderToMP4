// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Série de Fourier d'un créneau — Somme des harmoniques impaires 4/(k pi) sin(k x) ; le nombre de termes augmente puis redescend.
// en: Fourier series of a square wave — Sum of the odd harmonics 4/(k pi) sin(k x); the number of terms rises then falls.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float x = uv.x * 14.0 + iTime;
    float n = 1.0 + floor(6.0 + 5.0 * sin(iTime * 0.5));
    float y = 0.0;
    for (int k = 1; k < 24; k++) {
        if (float(k) > n * 2.0) break;
        if (k % 2 == 1) y += 4.0 / (3.14159265 * float(k)) * sin(float(k) * x);
    }
    float sq = sign(sin(x));
    float w = 0.03 + fwidth(y);
    float d = abs(uv.y * 3.2 - y) / w;
    vec3 col = vec3(0.04, 0.05, 0.09);
    col = mix(col, vec3(0.3, 0.8, 1.0) * 0.5, smoothstep(1.5, 0.5, abs(uv.y * 3.2 - sq) / (0.03 + fwidth(sq))));
    col = mix(col, vec3(1.0, 0.7, 0.2), smoothstep(1.5, 0.5, d));
    fragColor = vec4(col, 1.0);
}
