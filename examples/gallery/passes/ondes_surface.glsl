// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Ondes à la surface de l'eau — Le tampon A garde la hauteur et la vitesse de chaque point (équation des ondes) ; des gouttes tombent à intervalles réguliers.
// en: Water surface waves — Buffer A keeps the height and speed of each point (wave equation); drops fall at regular intervals.

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    ivec2 p = ivec2(fragCoord);
    float h = texelFetch(iChannel0, p, 0).r;
    vec2 g = vec2(texelFetch(iChannel0, p + ivec2(1, 0), 0).r - texelFetch(iChannel0, p - ivec2(1, 0), 0).r,
                  texelFetch(iChannel0, p + ivec2(0, 1), 0).r - texelFetch(iChannel0, p - ivec2(0, 1), 0).r);
    vec3 n = normalize(vec3(-g * 4.0, 1.0));
    vec3 col = mix(vec3(0.0, 0.15, 0.3), vec3(0.4, 0.8, 0.95), 0.5 + 0.5 * h * 3.0);
    col *= 0.5 + 0.7 * max(dot(n, normalize(vec3(-0.4, 0.5, 0.8))), 0.0);
    col += pow(max(dot(reflect(vec3(0, 0, -1), n), normalize(vec3(-0.4, 0.5, 0.8))), 0.0), 40.0);
    fragColor = vec4(col, 1.0);
}
