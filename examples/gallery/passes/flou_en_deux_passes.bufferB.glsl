// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 px = 1.0 / iResolution.xy;
    vec2 uv = fragCoord * px;
    vec3 acc = vec3(0.0);
    float wsum = 0.0;
    for (int i = -8; i <= 8; i++) {
        float w = exp(-float(i * i) / 24.0);
        acc += w * texture(iChannel0, uv + vec2(float(i), 0.0) * px * 2.0).rgb;
        wsum += w;
    }
    fragColor = vec4(acc / wsum, 1.0);
}
