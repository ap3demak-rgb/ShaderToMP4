// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Transition « zoomblur » : flou radial pendant le fondu.
// Entrees : clip A (iChannel0), clip B (iChannel1), iProgress de 0 (A) a 1 (B).
// Usage : ShaderToMP4 --render transitions/zoomblur.glsl --source=a.mp4 --source2=b.mp4 [--transitionduration=1]
vec4 A(vec2 uv) { return texture(iChannel0, uv); }
vec4 B(vec2 uv) { return texture(iChannel1, uv); }
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float p = iProgress;
    float asp = iResolution.x / iResolution.y;
    float k = sin(3.14159265 * p) * 0.25;
    vec4 a = vec4(0.0), b = vec4(0.0);
    for (int i = 0; i < 16; i++) {
        vec2 c = mix(uv, vec2(0.5), k * float(i) / 16.0);
        a += A(c);
        b += B(c);
    }
    fragColor = mix(a / 16.0, b / 16.0, p);
}
