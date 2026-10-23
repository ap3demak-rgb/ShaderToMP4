// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Horizon marin — Soleil couchant, mer qui reflète le ciel et ciel dégradé ; la direction du rayon donne l'angle d'élévation.
// en: Sea horizon — Setting sun, sea reflecting the sky and a graded sky; the ray direction gives the elevation angle.

void mainVR(out vec4 fragColor, in vec2 fragCoord, in vec3 ro, in vec3 rd) {
    vec3 sun = normalize(vec3(0.0, 0.05 + 0.03 * sin(iTime * 0.2), -1.0));
    vec3 d = rd;
    vec3 col;
    if (d.y < 0.0) {
        float w = 0.5 + 0.5 * sin(d.x * 200.0 / (-d.y + 0.1) + iTime * 2.0);
        vec3 refl = vec3(d.x, -d.y, d.z);
        col = mix(vec3(0.02, 0.08, 0.15), vec3(0.8, 0.4, 0.2), pow(max(dot(refl, sun), 0.0), 20.0)) + 0.05 * w;
    } else {
        col = mix(vec3(1.0, 0.55, 0.25), vec3(0.1, 0.2, 0.5), pow(d.y, 0.4));
        col += vec3(1.0, 0.8, 0.5) * pow(max(dot(d, sun), 0.0), 200.0) * 3.0;
    }
    fragColor = vec4(col, 1.0);
}
