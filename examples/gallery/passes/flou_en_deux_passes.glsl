// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Flou séparable en deux passes — Common définit la scène ; A la dessine, B la floute horizontalement, l'image finale floute verticalement et ajoute l'original net.
// en: Two-pass separable blur — Common defines the scene; A draws it, B blurs it horizontally, the final image blurs vertically and adds the sharp original.

// iChannel0=bufferB
// iChannel1=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 px = 1.0 / iResolution.xy;
    vec2 uv = fragCoord * px;
    vec3 blur = vec3(0.0);
    float wsum = 0.0;
    for (int i = -8; i <= 8; i++) {
        float w = exp(-float(i * i) / 24.0);
        blur += w * texture(iChannel0, uv + vec2(0.0, float(i)) * px * 2.0).rgb;
        wsum += w;
    }
    blur /= wsum;
    vec3 sharp = texture(iChannel1, uv).rgb;
    float split = 0.5 + 0.3 * sin(iTime * 0.5);
    fragColor = vec4(uv.x < split ? sharp : blur + sharp * 0.3, 1.0);
}
