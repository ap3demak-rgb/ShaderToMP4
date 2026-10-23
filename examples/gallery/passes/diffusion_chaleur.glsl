// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Diffusion de la chaleur — Chaque pixel du tampon A tend vers la moyenne de ses voisins (équation de la chaleur) pendant que deux sources chaudes se déplacent.
// en: Heat diffusion — Each pixel of buffer A drifts towards the mean of its neighbours (heat equation) while two hot sources move.

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float t = texelFetch(iChannel0, ivec2(fragCoord), 0).r;
    vec3 col = vec3(smoothstep(0.0, 0.5, t), smoothstep(0.3, 0.8, t), smoothstep(0.7, 1.0, t));
    fragColor = vec4(col, 1.0);
}
