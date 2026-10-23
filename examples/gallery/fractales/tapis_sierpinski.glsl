// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Tapis de Sierpinski — Chaque niveau retire le carré central de chaque carré ; le test se fait chiffre par chiffre en base trois.
// en: Sierpinski carpet — Each level removes the central square of every square; the test is made digit by digit in base three.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float zoom = pow(3.0, mod(iTime * 0.5, 3.0));
    vec2 p = ((fragCoord - 0.5 * iResolution.xy) / iResolution.y) * 1.2 / zoom + 0.5;
    float hole = 0.0;
    for (int i = 0; i < 7; i++) {
        vec2 d = floor(mod(p * 3.0, 3.0));
        if (d.x == 1.0 && d.y == 1.0) hole += 1.0;
        p *= 3.0;
    }
    vec3 col = hole > 0.0 ? 0.5 + 0.5 * cos(hole * 0.9 + iTime + vec3(0.0, 2.0, 4.0)) * 0.7 : vec3(0.02);
    fragColor = vec4(col, 1.0);
}
