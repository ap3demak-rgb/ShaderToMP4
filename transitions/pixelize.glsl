// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Transition « pixelize » : les images se pixelisent puis se defont.
// Entrees : clip A (iChannel0), clip B (iChannel1), iProgress de 0 (A) a 1 (B).
// Usage : ShaderToMP4 --render transitions/pixelize.glsl --source=a.mp4 --source2=b.mp4 [--transitionduration=1]
vec4 A(vec2 uv) { return texture(iChannel0, uv); }
vec4 B(vec2 uv) { return texture(iChannel1, uv); }
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float p = iProgress;
    float asp = iResolution.x / iResolution.y;
    float k = sin(3.14159265 * p);
    vec2 q = uv;
    if (k > 0.001) {
        vec2 cells = mix(iResolution.xy, vec2(28.0 * asp, 28.0), k);
        q = (floor(uv * cells) + 0.5) / cells;
    }
    fragColor = mix(A(q), B(q), p);
}
