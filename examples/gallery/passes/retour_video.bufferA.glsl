// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iChannel0=bufferA
mat2 rot(float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    vec2 q = rot(0.02 * sin(iTime * 0.5)) * uv * 0.985;
    vec3 prev = texture(iChannel0, q * vec2(iResolution.y / iResolution.x, 1.0) + 0.5).rgb;
    prev = prev * 0.97 + 0.02 * prev.gbr;
    vec2 c = 0.25 * vec2(cos(iTime), sin(iTime * 1.3));
    vec3 ink = 0.5 + 0.5 * cos(iTime * 0.7 + vec3(0.0, 2.0, 4.0));
    fragColor = vec4(prev + ink * smoothstep(0.03, 0.0, length(uv - c)), 1.0);
}
