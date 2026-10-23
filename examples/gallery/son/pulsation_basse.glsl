// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Pulsation sur les basses — Le rayon du disque suit iBass, la teinte suit iBeat. Mettre un fichier dans le réglage audio ; sans son, une pulsation de secours à 2 Hz s'affiche.
// en: Bass pulse — Disc radius follows iBass, hue follows iBeat. Put a file in the audio setting; without sound a fallback 2 Hz pulse is shown.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float bass = max(iBass, 0.4 + 0.4 * sin(iTime * 12.566) * step(iEnergy + iBass, 0.001));
    float r = 0.15 + 0.25 * bass;
    vec3 col = 0.5 + 0.5 * cos(6.2831853 * (iBeat + vec3(0.0, 0.33, 0.67)));
    float d = length(uv) - r;
    vec3 o = vec3(0.02, 0.02, 0.06) + col * smoothstep(0.01, 0.0, d) + col * 0.02 / (abs(d) + 0.02);
    fragColor = vec4(o, 1.0);
}
