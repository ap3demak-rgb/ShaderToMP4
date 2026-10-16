// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Transition « fadeblack » : fondu par le noir.
// Entrees : clip A (iChannel0), clip B (iChannel1), iProgress de 0 (A) a 1 (B).
// Usage : ShaderToMP4 --render transitions/fadeblack.glsl --source=a.mp4 --source2=b.mp4 [--transitionduration=1]
vec4 A(vec2 uv) { return texture(iChannel0, uv); }
vec4 B(vec2 uv) { return texture(iChannel1, uv); }
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float p = iProgress;
    float asp = iResolution.x / iResolution.y;
    fragColor = p < 0.5 ? mix(A(uv), vec4(0.0, 0.0, 0.0, 1.0), p * 2.0) : mix(vec4(0.0, 0.0, 0.0, 1.0), B(uv), p * 2.0 - 1.0);
}
