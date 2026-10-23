// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Vignette et grain de pellicule — Assombrissement radial et bruit blanc renouvelé à chaque image, ajoutés à une scène simple.
// en: Vignette and film grain — Radial darkening and white noise renewed on every frame, added to a simple scene.

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
    vec3 col = scene(uv);
    col *= 1.0 - 0.9 * smoothstep(0.3, 0.95, length(uv * vec2(0.9, 1.0)));
    float g = fract(sin(dot(fragCoord + float(iFrame) * 17.0, vec2(12.9898, 78.233))) * 43758.5453);
    col += (g - 0.5) * 0.12;
    fragColor = vec4(col, 1.0);
}
