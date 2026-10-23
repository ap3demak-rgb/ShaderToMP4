// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Trame d'impression — Une grille de points tournée dont le rayon suit la luminosité : l'image devient une trame d'imprimerie.
// en: Halftone print — A rotated dot grid whose radius follows the brightness: the image becomes a printer's screen.

vec3 scene(vec2 uv) {
    float a = iTime * 0.5;
    vec3 col = mix(vec3(0.1, 0.15, 0.4), vec3(0.9, 0.5, 0.3), uv.y * 0.5 + 0.5);
    for (int i = 0; i < 4; i++) {
        float f = float(i);
        vec2 c = 0.35 * vec2(cos(a + f * 1.57), sin(a * 1.3 + f * 1.57));
        col = mix(col, 0.5 + 0.5 * cos(f + vec3(0.0, 2.0, 4.0)), smoothstep(0.17, 0.15, length(uv - c)));
    }
    col = mix(col, vec3(1.0), smoothstep(0.012, 0.0, abs(uv.x - 0.5 * sin(a))) * 0.6);
    return col;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float c = cos(0.5), s = sin(0.5);
    vec2 g = mat2(c, -s, s, c) * uv * 40.0;
    vec2 cell = floor(g) + 0.5;
    vec2 centre = mat2(c, s, -s, c) * cell / 40.0;
    float lum = dot(scene(centre), vec3(0.299, 0.587, 0.114));
    float d = length(fract(g) - 0.5);
    float dot_ = smoothstep(0.02, 0.0, d - 0.55 * sqrt(lum));
    vec3 col = mix(vec3(0.97, 0.94, 0.88), vec3(0.1, 0.15, 0.3), dot_);
    fragColor = vec4(col, 1.0);
}
