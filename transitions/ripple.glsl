// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Transition « ripple » : ondulation de l'eau pendant le fondu.
// Entrees : clip A (iChannel0), clip B (iChannel1), iProgress de 0 (A) a 1 (B).
// Usage : ShaderToMP4 --render transitions/ripple.glsl --source=a.mp4 --source2=b.mp4 [--transitionduration=1]
vec4 A(vec2 uv) { return texture(iChannel0, uv); }
vec4 B(vec2 uv) { return texture(iChannel1, uv); }
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float p = iProgress;
    float asp = iResolution.x / iResolution.y;
    float k = sin(3.14159265 * p);
    vec2 d = (uv - 0.5) * vec2(asp, 1.0);
    float r = length(d);
    vec2 off = normalize(d + 1e-5) * sin(r * 40.0 - p * 20.0) * 0.02 * k / vec2(asp, 1.0);
    fragColor = mix(A(uv + off), B(uv + off), p);
}
