// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Ondulations bouclées — Des ondes dont la fréquence temporelle est un entier de la boucle : la dernière image enchaîne sans saut sur la première.
// en: Looping ripples — Waves whose time frequency is a whole number per loop: the last frame joins the first without a jump.

// duration=4
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float ph = 6.2831853 * iTime / 4.0;
    float v = sin(length(uv - vec2(0.2, 0.1)) * 30.0 - 2.0 * ph) + sin(length(uv + vec2(0.25, 0.0)) * 24.0 - 3.0 * ph);
    vec3 col = 0.5 + 0.5 * cos(v * 0.8 + vec3(0.0, 1.0, 2.0) + ph);
    fragColor = vec4(col * (0.5 + 0.25 * v), 1.0);
}
