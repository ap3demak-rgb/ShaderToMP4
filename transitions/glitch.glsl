// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Transition « glitch » : bandes decalees qui laissent place a B.
// Entrees : clip A (iChannel0), clip B (iChannel1), iProgress de 0 (A) a 1 (B).
// Usage : ShaderToMP4 --render transitions/glitch.glsl --source=a.mp4 --source2=b.mp4 [--transitionduration=1]
vec4 A(vec2 uv) { return texture(iChannel0, uv); }
vec4 B(vec2 uv) { return texture(iChannel1, uv); }
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float p = iProgress;
    float asp = iResolution.x / iResolution.y;
    float k = sin(3.14159265 * p);
    float band = floor(uv.y * 24.0);
    float h = hash(vec2(band, floor(p * 14.0)));
    vec2 q = uv + vec2((h - 0.5) * 0.25 * k, 0.0);
    fragColor = hash(vec2(band, 7.0)) < p ? B(q) : A(q);
}
