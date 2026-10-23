// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Phyllotaxie et angle d'or — Chaque graine k est placée à l'angle k fois 137,5 degrés et à la distance racine de k ; l'angle varie à peine pour montrer pourquoi celui-ci est spécial.
// en: Phyllotaxis and the golden angle — Each seed k sits at the angle k times 137.5 degrees and at the distance square root of k; the angle varies slightly to show why this one is special.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float ang = 2.39996323 + 0.002 * sin(iTime * 0.5);
    float best = 1e9, id = 0.0;
    for (int k = 1; k < 400; k++) {
        float fk = float(k);
        float r = 0.028 * sqrt(fk), th = fk * ang + iTime * 0.1;
        float d = length(uv - r * vec2(cos(th), sin(th)));
        if (d < best) { best = d; id = fk; }
    }
    float r0 = 0.012 + 0.00002 * id;
    vec3 col = 0.5 + 0.5 * cos(id * 0.03 + vec3(0.0, 2.0, 4.0));
    fragColor = vec4(mix(vec3(0.02), col, smoothstep(r0 + 0.002, r0 - 0.002, best)), 1.0);
}
