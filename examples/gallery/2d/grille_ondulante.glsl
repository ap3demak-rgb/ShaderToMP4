// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Grille ondulante — Les lignes d'une grille sont déplacées par deux ondes croisées ; l'épaisseur du trait est constante à l'écran grâce à fwidth.
// en: Waving grid — The lines of a grid are displaced by two crossing waves; the line width stays constant on screen thanks to fwidth.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y * 8.0;
    uv += 0.35 * vec2(sin(uv.y * 1.3 + iTime), cos(uv.x * 1.1 - iTime * 0.8));
    vec2 g = abs(fract(uv) - 0.5);
    float dl = 0.5 - max(g.x, g.y);
    float w = fwidth(dl) * 1.5;
    vec3 col = mix(vec3(0.2, 0.8, 1.0), vec3(0.03, 0.04, 0.1), smoothstep(0.02, 0.02 + w, dl));
    fragColor = vec4(col, 1.0);
}
