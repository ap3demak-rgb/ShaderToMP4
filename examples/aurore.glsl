// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Aurore : rubans de lumiere ondulants sur un ciel etoile
float band(vec2 uv, float k, float t) {
    float y = 0.18 * sin(uv.x * 2.2 + t * k) + 0.10 * sin(uv.x * 5.1 - t * 0.7 * k) + 0.12 * k;
    return exp(-pow((uv.y - y) * 5.0, 2.0));
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float t = iTime * 0.5;
    vec3 sky = mix(vec3(0.01, 0.02, 0.07), vec3(0.03, 0.05, 0.14), fragCoord.y / iResolution.y);
    vec3 col = sky;
    col += vec3(0.1, 0.9, 0.5) * band(uv, 1.0, t) * 0.9;
    col += vec3(0.5, 0.3, 1.0) * band(uv, 1.6, t + 2.0) * 0.7;
    col += vec3(0.1, 0.6, 1.0) * band(uv, 2.2, t + 4.0) * 0.5;
    vec2 g = floor(fragCoord / 3.0);
    float h = fract(sin(dot(g, vec2(12.9898, 78.233))) * 43758.5453);
    col += step(0.9985, h) * (0.5 + 0.5 * sin(iTime * 2.0 + h * 40.0));
    fragColor = vec4(col, 1.0);
}
