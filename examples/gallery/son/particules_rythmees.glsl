// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Particules rythmées — Des particules sur des orbites ; leur rayon est multiplié par 1 + iBass et leur taille par iMid.
// en: Rhythmic particles — Particles on orbits; their radius is multiplied by 1 + iBass and their size by iMid.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    vec3 col = vec3(0.01, 0.01, 0.04);
    for (int i = 0; i < 40; i++) {
        float f = float(i);
        float a = f * 2.399963 + iTime * (0.2 + 0.02 * f);
        float idle = step(iBass + iMid, 0.001) * (0.5 + 0.5 * sin(iTime * 3.0));
        float r = (0.08 + 0.01 * f) * (1.0 + max(iBass, idle));
        float s = 0.006 + 0.01 * max(iMid, idle);
        col += (0.5 + 0.5 * cos(f + vec3(0.0, 2.0, 4.0))) * smoothstep(s, 0.0, length(uv - r * vec2(cos(a), sin(a))));
    }
    fragColor = vec4(col, 1.0);
}
