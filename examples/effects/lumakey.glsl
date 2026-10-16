// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Incrustation par la luminance : les zones sombres (ou claires avec `invert`) deviennent transparentes.
// Usage : ShaderToMP4 --render examples/effects/lumakey.glsl --source=fumee_sur_noir.mp4 --codec=prores_4444 --container=mov
// alpha=1
// uniform float threshold = 0.2 [0..1]
// uniform float softness = 0.15 [0..1]
// uniform bool invert = false
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c = texture(iChannel0, uv).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float a = smoothstep(threshold, threshold + max(softness, 1e-4), l);
    fragColor = vec4(c, invert ? 1.0 - a : a);
}
