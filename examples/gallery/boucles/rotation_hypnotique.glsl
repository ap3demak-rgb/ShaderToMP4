// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Rotation hypnotique — Des carrés emboîtés tournent chacun d'un quart de tour de plus que le précédent ; la boucle dure un quart de tour.
// en: Hypnotic rotation — Nested squares each turn a quarter turn more than the previous one; the loop lasts one quarter turn.

// duration=6
mat2 rot(float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float ph = fract(iTime / 6.0);
    vec3 col = vec3(0.0);
    for (int i = 0; i < 12; i++) {
        float f = float(i);
        vec2 p = rot(1.5707963 * (ph + f * 0.08)) * uv;
        float d = max(abs(p.x), abs(p.y)) - (0.9 - f * 0.07);
        col = mix(col, 0.5 + 0.5 * cos(f * 0.5 + vec3(0.0, 2.0, 4.0)), smoothstep(0.004, 0.0, d));
    }
    fragColor = vec4(col, 1.0);
}
