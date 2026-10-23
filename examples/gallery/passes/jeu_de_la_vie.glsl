// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Jeu de la vie — Automate de Conway sur une grille de 4 pixels par cellule ; le tampon A lit ses huit voisines et se met à jour tous les 6 images.
// en: Game of life — Conway's automaton on a grid of 4 pixels per cell; buffer A reads its eight neighbours and updates every 6 frames.

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float a = texelFetch(iChannel0, ivec2(fragCoord / 4.0), 0).r;
    vec3 col = mix(vec3(0.03, 0.04, 0.08), vec3(0.4, 1.0, 0.6), a);
    col *= 0.85 + 0.15 * step(0.5, fract(fragCoord.x / 4.0)) * step(0.5, fract(fragCoord.y / 4.0));
    fragColor = vec4(col, 1.0);
}
