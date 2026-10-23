// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Tuiles de Truchet — Chaque case tire au hasard l'un des deux arcs de cercle ; les arcs voisins se raccordent en labyrinthes continus.
// en: Truchet tiles — Each cell randomly picks one of two circle arcs; neighbouring arcs join into continuous mazes.

float h21(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y * 8.0 + vec2(iTime * 0.5, 0.0);
    vec2 id = floor(uv), f = fract(uv);
    if (h21(id) > 0.5) f.x = 1.0 - f.x;
    float d = min(abs(length(f) - 0.5), abs(length(f - 1.0) - 0.5));
    float line = smoothstep(0.1, 0.08, d);
    vec3 col = mix(vec3(0.95, 0.9, 0.8), vec3(0.15, 0.2, 0.35), line);
    col = mix(col, vec3(0.9, 0.35, 0.25), line * step(0.5, h21(id + 11.0)));
    fragColor = vec4(col, 1.0);
}
