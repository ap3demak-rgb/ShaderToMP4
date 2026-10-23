// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Courbes de Lissajous — Distance à une courbe paramétrique, éclat obtenu par 1/d ; les fréquences dérivent avec le temps.
// en: Lissajous curves — Distance to a parametric curve, glow from 1/d; the frequencies drift with time.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float a = 3.0 + 0.5 * sin(iTime * 0.2), b = 2.0;
    float d = 1e9;
    for (int i = 0; i < 160; i++) {
        float t = float(i) / 160.0 * 6.2831853;
        vec2 p = 0.38 * vec2(sin(a * t + iTime * 0.4), sin(b * t));
        d = min(d, length(uv - p));
    }
    vec3 col = vec3(0.02, 0.03, 0.06) + vec3(0.2, 0.7, 1.0) * 0.004 / (d + 0.002);
    fragColor = vec4(col, 1.0);
}
