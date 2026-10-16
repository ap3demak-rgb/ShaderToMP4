// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Incrustation par difference : iChannel0 est la prise avec le sujet, iChannel1 la meme prise sans lui (plan de reference fixe) ; ce qui ne change pas
// devient transparent.
// Usage : ShaderToMP4 --render examples/effects/differencekey.glsl --source=avec_sujet.mp4 --source2=fond_seul.mp4 --codec=prores_4444 --container=mov
// alpha=1
// uniform float threshold = 0.08 [0..1]
// uniform float softness = 0.1 [0..1]
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c = texture(iChannel0, uv).rgb;
    float d = length(c - texture(iChannel1, uv).rgb);
    fragColor = vec4(c, smoothstep(threshold, threshold + max(softness, 1e-4), d));
}
