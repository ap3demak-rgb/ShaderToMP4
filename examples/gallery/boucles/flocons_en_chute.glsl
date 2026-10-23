// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Flocons en chute — Chaque flocon traverse l'écran en exactement une boucle : sa position est fract de (temps/durée + décalage).
// en: Falling snow — Each flake crosses the screen in exactly one loop: its position is fract of (time/length + offset).

// duration=8
float h(float n) { return fract(sin(n * 91.345) * 43758.5453); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float ph = iTime / 8.0;
    vec3 col = mix(vec3(0.02, 0.05, 0.15), vec3(0.1, 0.15, 0.3), uv.y + 0.5);
    for (int i = 0; i < 90; i++) {
        float f = float(i);
        float sp = 1.0 + floor(h(f) * 3.0);
        float y = 0.7 - mod(ph * sp + h(f + 1.0), 1.0) * 1.4;
        float x = (h(f + 2.0) - 0.5) * 1.8 + 0.05 * sin(6.2831853 * ph * sp + f);
        float s = 0.004 + 0.006 * h(f + 3.0);
        col += vec3(1.0) * smoothstep(s, 0.0, length(uv - vec2(x, y))) * (0.5 + 0.5 * h(f + 4.0));
    }
    fragColor = vec4(col, 1.0);
}
