// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Pixelisation animée — Les coordonnées sont arrondies à une grille dont la taille varie avec le temps ; la scène est lue au centre de chaque bloc.
// en: Animated pixelation — Coordinates are rounded to a grid whose size changes with time; the scene is read at the centre of each block.

vec3 scene(vec2 uv) {
    float a = iTime * 0.5;
    vec3 col = mix(vec3(0.1, 0.15, 0.4), vec3(0.9, 0.5, 0.3), uv.y * 0.5 + 0.5);
    for (int i = 0; i < 4; i++) {
        float f = float(i);
        vec2 c = 0.35 * vec2(cos(a + f * 1.57), sin(a * 1.3 + f * 1.57));
        col = mix(col, 0.5 + 0.5 * cos(f + vec3(0.0, 2.0, 4.0)), smoothstep(0.17, 0.15, length(uv - c)));
    }
    col = mix(col, vec3(1.0), smoothstep(0.012, 0.0, abs(uv.x - 0.5 * sin(a))) * 0.6);
    return col;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float n = floor(8.0 + 56.0 * (0.5 + 0.5 * sin(iTime * 0.7)));
    vec2 cell = floor(fragCoord / iResolution.y * n);
    vec2 uv = (cell + 0.5) / n - 0.5 * iResolution.xy / iResolution.y;
    vec3 col = scene(uv);
    col = floor(col * 6.0 + 0.5) / 6.0;
    fragColor = vec4(col, 1.0);
}
